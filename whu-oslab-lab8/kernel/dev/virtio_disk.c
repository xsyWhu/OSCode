#include "dev/virtio_disk.h"
#include "fs/bio.h"
#include "lib/lock.h"
#include "lib/print.h"
#include "lib/string.h"
#include "mem/pmem.h"
#include "memlayout.h"
#include "proc/proc.h"
#include "riscv.h"

#define R(reg) ((volatile uint32 *)(VIRTIO0 + (reg)))

#define VIRTIO_MMIO_MAGIC_VALUE   0x000
#define VIRTIO_MMIO_VERSION       0x004
#define VIRTIO_MMIO_DEVICE_ID     0x008
#define VIRTIO_MMIO_VENDOR_ID     0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES      0x010
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL  0x014
#define VIRTIO_MMIO_DRIVER_FEATURES      0x020
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL  0x024
#define VIRTIO_MMIO_QUEUE_SEL            0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX        0x034
#define VIRTIO_MMIO_QUEUE_NUM            0x038
#define VIRTIO_MMIO_QUEUE_ALIGN          0x03c
#define VIRTIO_MMIO_QUEUE_READY          0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY         0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS     0x060
#define VIRTIO_MMIO_INTERRUPT_ACK        0x064
#define VIRTIO_MMIO_STATUS               0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW       0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH      0x084
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW      0x090
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH     0x094
#define VIRTIO_MMIO_QUEUE_USED_LOW       0x0a0
#define VIRTIO_MMIO_QUEUE_USED_HIGH      0x0a4
#define VIRTIO_MMIO_QUEUE_PFN            0x040

#define VIRTIO_CONFIG_S_ACKNOWLEDGE 1
#define VIRTIO_CONFIG_S_DRIVER      2
#define VIRTIO_CONFIG_S_DRIVER_OK   4
#define VIRTIO_CONFIG_S_FEATURES_OK 8

#define VIRTIO_BLK_T_IN   0
#define VIRTIO_BLK_T_OUT  1

#define VIRTIO_BLK_F_RO           5
#define VIRTIO_BLK_F_SCSI         7
#define VIRTIO_BLK_F_CONFIG_WCE   11
#define VIRTIO_BLK_F_MQ           12
#define VIRTIO_F_ANY_LAYOUT       27
#define VIRTIO_RING_F_INDIRECT_DESC 28
#define VIRTIO_RING_F_EVENT_IDX   29
#define VIRTIO_F_VERSION_1        32

#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

#define VIRTIO_DESC_NUM 8
#define SECTOR_SIZE 512

struct virtio_blk_req {
    uint32 type;
    uint32 reserved;
    uint64 sector;
};

struct virtq_desc {
    uint64 addr;
    uint32 len;
    uint16 flags;
    uint16 next;
};

struct virtq_avail {
    uint16 flags;
    uint16 idx;
    uint16 ring[VIRTIO_DESC_NUM];
    uint16 unused;
};

struct virtq_used_elem {
    uint32 id;
    uint32 len;
};

struct virtq_used {
    uint16 flags;
    uint16 idx;
    struct virtq_used_elem ring[VIRTIO_DESC_NUM];
};

struct disk_info {
    struct buf *b;
    struct virtio_blk_req cmd;
    uint8 status;
};

struct disk_state {
    spinlock_t lock;
    struct virtq_desc *desc;
    struct virtq_avail *avail;
    struct virtq_used *used;
    struct disk_info info[VIRTIO_DESC_NUM];
    int free[VIRTIO_DESC_NUM];
    uint16 used_idx;
} disk;

static uint8 queue_mem[PGSIZE * 2] __attribute__((aligned(PGSIZE)));

static int alloc_desc(void);
static void free_desc(int i);
static void free_chain(int i);
static int alloc3_desc(int idx[3]);
static void virtio_process_used(void);
static inline uint64 kvaddr_to_pa(void *addr)
{
    return (uint64)addr;
}

static inline void virtio_fence(void)
{
    asm volatile("fence rw, rw");
}

void virtio_disk_init(void)
{
    spinlock_init(&disk.lock, "virtio_disk");

    uint32 magic = *R(VIRTIO_MMIO_MAGIC_VALUE);
    uint32 version = *R(VIRTIO_MMIO_VERSION);
    uint32 device_id = *R(VIRTIO_MMIO_DEVICE_ID);
    uint32 vendor_id = *R(VIRTIO_MMIO_VENDOR_ID);

    printf("[virtio] magic=0x%x version=%u device=%u vendor=0x%x\n",
           magic, version, device_id, vendor_id);

    if (magic != 0x74726976 || device_id != 2) {
        printf("[virtio] magic=0x%08x version=%u device=%u vendor=0x%08x\n",
               magic, version, device_id, vendor_id);
        panic("virtio_disk_init: unsupported device");
    }

    uint32 status = 0;
    status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
    *R(VIRTIO_MMIO_STATUS) = status;

    status |= VIRTIO_CONFIG_S_DRIVER;
    *R(VIRTIO_MMIO_STATUS) = status;

    uint64 features = 0;
    *R(VIRTIO_MMIO_DEVICE_FEATURES_SEL) = 0;
    features |= *R(VIRTIO_MMIO_DEVICE_FEATURES);
    *R(VIRTIO_MMIO_DEVICE_FEATURES_SEL) = 1;
    features |= ((uint64)*R(VIRTIO_MMIO_DEVICE_FEATURES) << 32);

    printf("[virtio] device features=0x%llx\n", features);
    uint64 driver_features = features;
    driver_features &= ~(1ULL << VIRTIO_BLK_F_RO);
    driver_features &= ~(1ULL << VIRTIO_BLK_F_SCSI);
    driver_features &= ~(1ULL << VIRTIO_BLK_F_CONFIG_WCE);
    driver_features &= ~(1ULL << VIRTIO_BLK_F_MQ);
    driver_features &= ~(1ULL << VIRTIO_F_ANY_LAYOUT);
    driver_features &= ~(1ULL << VIRTIO_RING_F_EVENT_IDX);
    driver_features &= ~(1ULL << VIRTIO_RING_F_INDIRECT_DESC);

    *R(VIRTIO_MMIO_DRIVER_FEATURES_SEL) = 0;
    *R(VIRTIO_MMIO_DRIVER_FEATURES) = (uint32)driver_features;
    *R(VIRTIO_MMIO_DRIVER_FEATURES_SEL) = 1;
    *R(VIRTIO_MMIO_DRIVER_FEATURES) = (uint32)(driver_features >> 32);

    printf("[virtio] driver features=0x%llx\n", driver_features);

    status |= VIRTIO_CONFIG_S_FEATURES_OK;
    *R(VIRTIO_MMIO_STATUS) = status;
    if (!(*R(VIRTIO_MMIO_STATUS) & VIRTIO_CONFIG_S_FEATURES_OK)) {
        panic("virtio_disk_init: FEATURES_OK not accepted");
    }

    *R(VIRTIO_MMIO_QUEUE_SEL) = 0;
    uint32 max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max == 0 || max < VIRTIO_DESC_NUM) {
        panic("virtio_disk_init: queue too small");
    }

    *R(VIRTIO_MMIO_QUEUE_NUM) = VIRTIO_DESC_NUM;
    if (version == 1) {
        *R(VIRTIO_MMIO_QUEUE_ALIGN) = PGSIZE;
    }

    *R(VIRTIO_MMIO_QUEUE_READY) = 0;

    memset(queue_mem, 0, sizeof(queue_mem));
    disk.desc = (struct virtq_desc*)queue_mem;
    disk.avail = (struct virtq_avail*)(queue_mem + sizeof(struct virtq_desc) * VIRTIO_DESC_NUM);
    disk.used = (struct virtq_used*)(queue_mem + PGSIZE);
    disk.used_idx = 0;

    printf("[virtio] desc=%p avail=%p used=%p\n", disk.desc, disk.avail, disk.used);

    uint64 desc_pa = kvaddr_to_pa(disk.desc);
    uint64 avail_pa = kvaddr_to_pa(disk.avail);
    uint64 used_pa = kvaddr_to_pa(disk.used);
    uint64 queue_pa = kvaddr_to_pa(queue_mem);
    uint32 queue_pfn = queue_pa >> PGSHIFT;

    *R(VIRTIO_MMIO_QUEUE_PFN) = queue_pfn;
    *R(VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint32)desc_pa;
    *R(VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint32)(desc_pa >> 32);
    *R(VIRTIO_MMIO_QUEUE_AVAIL_LOW) = (uint32)avail_pa;
    *R(VIRTIO_MMIO_QUEUE_AVAIL_HIGH) = (uint32)(avail_pa >> 32);
    *R(VIRTIO_MMIO_QUEUE_USED_LOW) = (uint32)used_pa;
    *R(VIRTIO_MMIO_QUEUE_USED_HIGH) = (uint32)(used_pa >> 32);

    for (int i = 0; i < VIRTIO_DESC_NUM; i++) {
        disk.free[i] = 1;
        disk.info[i].b = 0;
        disk.info[i].status = 0;
        memset(&disk.info[i].cmd, 0, sizeof(disk.info[i].cmd));
    }

    *R(VIRTIO_MMIO_QUEUE_READY) = 1;

    status |= VIRTIO_CONFIG_S_DRIVER_OK;
    *R(VIRTIO_MMIO_STATUS) = status;
}

static int alloc_desc(void)
{
    for (int i = 0; i < VIRTIO_DESC_NUM; i++) {
        if (disk.free[i]) {
            disk.free[i] = 0;
            return i;
        }
    }
    return -1;
}

static void free_desc(int i)
{
    if (i < 0 || i >= VIRTIO_DESC_NUM) {
        panic("free_desc");
    }
    if (disk.free[i]) {
        panic("free_desc: double free");
    }
    disk.free[i] = 1;
    wakeup(disk.free);
}

static void free_chain(int i)
{
    while (1) {
        uint16 flags = disk.desc[i].flags;
        uint16 next = disk.desc[i].next;
        free_desc(i);
        if (!(flags & VRING_DESC_F_NEXT)) {
            break;
        }
        i = next;
    }
}

static int alloc3_desc(int idx[3])
{
    for (int i = 0; i < 3; i++) {
        idx[i] = alloc_desc();
        if (idx[i] < 0) {
            for (int j = 0; j < i; j++) {
                free_desc(idx[j]);
            }
            return -1;
        }
    }
    return 0;
}

void virtio_disk_rw(struct buf *b, int write)
{
    int idx[3];

    spinlock_acquire(&disk.lock);
    while (alloc3_desc(idx) < 0) {
        if (myproc() == 0) {
            virtio_process_used();
        } else {
            sleep(disk.free, &disk.lock);
        }
    }

    struct disk_info *info = &disk.info[idx[0]];
    info->cmd.type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    info->cmd.reserved = 0;
    info->cmd.sector = b->blockno * (BSIZE / SECTOR_SIZE);

    disk.desc[idx[0]].addr = kvaddr_to_pa(&info->cmd);
    disk.desc[idx[0]].len = sizeof(info->cmd);
    disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;
    disk.desc[idx[0]].next = idx[1];

    disk.desc[idx[1]].addr = kvaddr_to_pa(b->data);
    disk.desc[idx[1]].len = BSIZE;
    disk.desc[idx[1]].flags = VRING_DESC_F_NEXT | (write ? 0 : VRING_DESC_F_WRITE);
    disk.desc[idx[1]].next = idx[2];

    info->status = 0xff;
    disk.desc[idx[2]].addr = kvaddr_to_pa(&info->status);
    disk.desc[idx[2]].len = 1;
    disk.desc[idx[2]].flags = VRING_DESC_F_WRITE;
    disk.desc[idx[2]].next = 0;

    b->disk = 1;
    printf("[virtio] submit block %u write=%d\n", b->blockno, write);
    info->b = b;

    disk.avail->ring[disk.avail->idx % VIRTIO_DESC_NUM] = idx[0];
    __sync_synchronize();
    disk.avail->idx++;
    __sync_synchronize();
    virtio_fence();
    *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

    int spins = 0;
    while (b->disk == 1) {
        if (myproc() == 0) {
            virtio_process_used();
        } else {
            sleep(b, &disk.lock);
        }
        if (++spins % 1000000 == 0) {
            uint32 device_status = *R(VIRTIO_MMIO_STATUS);
            uint32 interrupt_status = *R(VIRTIO_MMIO_INTERRUPT_STATUS);
            printf("[virtio] waiting block %u used_idx=%u device_idx=%u status=0x%x irq=0x%x\n",
                   b->blockno, disk.used_idx, disk.used->idx,
                   device_status, interrupt_status);
        }
    }
    printf("[virstio] complete block %u write=%d\n", b->blockno, write);

    info->b = 0;
    free_chain(idx[0]);
    spinlock_release(&disk.lock);
}

void virtio_disk_intr(void)
{
    spinlock_acquire(&disk.lock);

    uint32 status = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;
    if (status) {
        *R(VIRTIO_MMIO_INTERRUPT_ACK) = status;
    }
    virtio_process_used();

    spinlock_release(&disk.lock);
}

static void virtio_process_used(void)
{
    while (disk.used_idx != disk.used->idx) {
        printf("[virtio] used entry (used_idx=%u device_idx=%u)\n",
               disk.used_idx, disk.used->idx);
        uint16 id = disk.used->ring[disk.used_idx % VIRTIO_DESC_NUM].id;
        struct disk_info *info = &disk.info[id];
        if (info->b) {
            info->b->disk = 0;
            wakeup(info->b);
        }
        disk.used_idx++;
    }
}
