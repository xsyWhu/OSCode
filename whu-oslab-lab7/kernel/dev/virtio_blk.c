#include "dev/virtio_blk.h"
#include "lib/lock.h"
#include "lib/print.h"
#include "lib/string.h"
#include "mem/pmem.h"
#include "riscv.h"

// Largely adapted from xv6's virtio disk driver.

#define R_BASE(reg, base) ((volatile uint32 *)((base) + (reg)))
#define R(reg) R_BASE((reg), virtio_mmio_base)

// MMIO registers
#define VIRTIO_MMIO_MAGIC_VALUE   0x000
#define VIRTIO_MMIO_VERSION       0x004
#define VIRTIO_MMIO_DEVICE_ID     0x008
#define VIRTIO_MMIO_VENDOR_ID     0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
#define VIRTIO_MMIO_GUEST_PAGE_SIZE 0x028
#define VIRTIO_MMIO_QUEUE_SEL     0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX 0x034
#define VIRTIO_MMIO_QUEUE_NUM     0x038
#define VIRTIO_MMIO_QUEUE_ALIGN   0x03c
#define VIRTIO_MMIO_QUEUE_PFN     0x040
#define VIRTIO_MMIO_QUEUE_READY   0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY  0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060
#define VIRTIO_MMIO_INTERRUPT_ACK 0x064

#define VIRTIO_MMIO_STATUS        0x070
#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER      2
#define VIRTIO_STATUS_DRIVER_OK   4
#define VIRTIO_STATUS_FEATURES_OK 8

// Virtio queue size
#define NUM 8

// Virtio descriptor flags
#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

// Virtio block request types
#define VIRTIO_BLK_T_IN   0
#define VIRTIO_BLK_T_OUT  1

struct virtq_desc {
    uint64 addr;
    uint32 len;
    uint16 flags;
    uint16 next;
};

struct virtq_avail {
    uint16 flags;
    uint16 idx;
    uint16 ring[NUM];
};

struct virtq_used_elem {
    uint32 id;
    uint32 len;
};

struct virtq_used {
    uint16 flags;
    uint16 idx;
    struct virtq_used_elem ring[NUM];
};

static struct disk {
    // memory for queue elements
    char pages[2 * PGSIZE] __attribute__((aligned(PGSIZE)));
    struct virtq_desc *desc;
    struct virtq_avail *avail;
    struct virtq_used *used;

    // our own book keeping
    char free[NUM];
    uint16 used_idx;

    struct {
        struct buf *b;
        char status;
        void *hdr;
    } info[NUM];

    spinlock_t vdisk_lock;
} disk;

static uint64 virtio_mmio_base = VIRTIO0;

static int probe_virtio_mmio(void)
{
    for (int i = 0; i < VIRTIO_MMIO_SLOTS; i++) {
        uint64 base = VIRTIO0 + i * VIRTIO_MMIO_STRIDE;
        uint32 magic = *R_BASE(VIRTIO_MMIO_MAGIC_VALUE, base);
        uint32 version = *R_BASE(VIRTIO_MMIO_VERSION, base);
        uint32 dev_id = *R_BASE(VIRTIO_MMIO_DEVICE_ID, base);
        if (magic == 0x74726976 && (version == 1 || version == 2) && dev_id == 2) {
            virtio_mmio_base = base;
            return 0;
        }
    }
    return -1;
}

static int alloc_desc(void)
{
    for (int i = 0; i < NUM; i++) {
        if (disk.free[i]) {
            disk.free[i] = 0;
            return i;
        }
    }
    return -1;
}

static void free_desc(int i)
{
    if (i >= NUM)
        panic("free_desc 1");
    if (disk.free[i])
        panic("free_desc 2");
    disk.desc[i].addr = 0;
    disk.desc[i].len = 0;
    disk.desc[i].flags = 0;
    disk.desc[i].next = 0;
    disk.free[i] = 1;
}

static void free_chain(int i)
{
    while (1) {
        int flag = disk.desc[i].flags;
        int nxt = disk.desc[i].next;
        free_desc(i);
        if (flag & VRING_DESC_F_NEXT)
            i = nxt;
        else
            break;
    }
}

void virtio_disk_init(void)
{
    spinlock_init(&disk.vdisk_lock, "virtio_disk");

    if (probe_virtio_mmio() < 0) {
        printf("virtio blk probe failed: no matching mmio slot\n");
        panic("could not find virtio disk");
    }

    uint32 magic = *R(VIRTIO_MMIO_MAGIC_VALUE);
    uint32 version = *R(VIRTIO_MMIO_VERSION);
    uint32 dev_id = *R(VIRTIO_MMIO_DEVICE_ID);
    uint32 vendor = *R(VIRTIO_MMIO_VENDOR_ID);
    printf("virtio blk: base=%p magic=%x version=%x dev_id=%x vendor=%x\n",
           (void*)virtio_mmio_base, magic, version, dev_id, vendor);

    *R(VIRTIO_MMIO_STATUS) = 0;
    *R(VIRTIO_MMIO_STATUS) |= VIRTIO_STATUS_ACKNOWLEDGE;
    *R(VIRTIO_MMIO_STATUS) |= VIRTIO_STATUS_DRIVER;

    // negotiate features (none for now)
    uint32 features = *R(VIRTIO_MMIO_DEVICE_FEATURES);
    *R(VIRTIO_MMIO_DRIVER_FEATURES) = features;
    *R(VIRTIO_MMIO_STATUS) |= VIRTIO_STATUS_FEATURES_OK;

    // page size
    *R(VIRTIO_MMIO_GUEST_PAGE_SIZE) = PGSIZE;

    // initialize queue 0
    *R(VIRTIO_MMIO_QUEUE_SEL) = 0;
    uint32 max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max == 0)
        panic("virtio disk has no queue 0");
    if (max < NUM)
        panic("virtio queue too short");
    *R(VIRTIO_MMIO_QUEUE_NUM) = NUM;
    memset(disk.pages, 0, sizeof(disk.pages));
    uint64 pa = (uint64)disk.pages;
    *R(VIRTIO_MMIO_QUEUE_ALIGN) = PGSIZE;
    *R(VIRTIO_MMIO_QUEUE_PFN) = pa >> 12;
    *R(VIRTIO_MMIO_QUEUE_READY) = 1;

    // desc, avail, used rings
    disk.desc = (struct virtq_desc *)pa;
    disk.avail = (struct virtq_avail *)(pa + NUM * sizeof(struct virtq_desc));
    disk.used = (struct virtq_used *)(pa + PGSIZE);

    for (int i = 0; i < NUM; i++)
        disk.free[i] = 1;

    *R(VIRTIO_MMIO_STATUS) |= VIRTIO_STATUS_DRIVER_OK;
}

// Start disk I/O for buf b. write=1 writes, write=0 reads.
void virtio_disk_rw(struct buf *b, int write)
{
    int idx[3];
    spinlock_acquire(&disk.vdisk_lock);

    // Allocate descriptors.
    for (int i = 0; i < 3; i++) {
        while ((idx[i] = alloc_desc()) < 0) {
            // busy wait; in practice NUM small so contention unlikely
        }
    }

    struct virtio_blk_req {
        uint32 type;
        uint32 reserved;
        uint64 sector;
    } *buf0;

    buf0 = (struct virtio_blk_req *)pmem_alloc(true);
    if (!buf0)
        panic("virtio: no mem");
    buf0->type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    buf0->reserved = 0;
    buf0->sector = b->blockno * (BSIZE / 512);

    // First descriptor: request header.
    disk.desc[idx[0]].addr = (uint64)buf0;
    disk.desc[idx[0]].len = sizeof(struct virtio_blk_req);
    disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;
    disk.desc[idx[0]].next = idx[1];

    // Second descriptor: data buffer.
    disk.desc[idx[1]].addr = (uint64)b->data;
    disk.desc[idx[1]].len = BSIZE;
    disk.desc[idx[1]].flags = (write ? 0 : VRING_DESC_F_WRITE) | VRING_DESC_F_NEXT;
    disk.desc[idx[1]].next = idx[2];

    // Third descriptor: status byte.
    disk.info[idx[0]].status = 0;
    disk.desc[idx[2]].addr = (uint64)&disk.info[idx[0]].status;
    disk.desc[idx[2]].len = 1;
    disk.desc[idx[2]].flags = VRING_DESC_F_WRITE;
    disk.desc[idx[2]].next = 0;

    disk.info[idx[0]].b = b;
    disk.info[idx[0]].hdr = buf0;

    // Add to avail ring.
    disk.avail->ring[disk.avail->idx % NUM] = idx[0];
    __sync_synchronize();
    disk.avail->idx += 1;
    __sync_synchronize();
    *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

    // Wait for completion.
    volatile uint16 *u_idx = &disk.used->idx;
    uint64 spin = 0;
    while (disk.used_idx == *u_idx) {
        spin++;
        if (spin > 100000000) {
            panic("virtio_disk_rw timeout");
        }
    }
    int id = disk.used->ring[disk.used_idx % NUM].id;
    disk.used_idx += 1;

    if (disk.info[id].status != 0)
        panic("virtio_disk_rw status");

    pmem_free((uint64)disk.info[id].hdr, true);
    free_chain(id);
    spinlock_release(&disk.vdisk_lock);
}
