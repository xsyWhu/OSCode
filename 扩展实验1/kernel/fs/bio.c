#include "fs/bio.h"
#include "dev/virtio_disk.h"
#include "lib/print.h"

#define NBUF 32// number of buffer cache blocks,8 to test

uint64 disk_read_count = 0;
uint64 disk_write_count = 0;

static struct {
    spinlock_t lock;
    struct buf buf[NBUF];
    struct buf head;
} bcache;

static struct buf* bget(uint32 dev, uint32 blockno);

void binit(void)
{
    spinlock_init(&bcache.lock, "bcache");
    bcache.head.prev = &bcache.head;
    bcache.head.next = &bcache.head;

    for (int i = 0; i < NBUF; i++) {
        struct buf *b = &bcache.buf[i];
        b->valid = 0;
        b->disk = 0;
        b->dev = 0;
        b->blockno = 0;
        b->refcnt = 0;
        sleeplock_init(&b->lock, "buffer");

        b->next = bcache.head.next;
        b->prev = &bcache.head;
        bcache.head.next->prev = b;
        bcache.head.next = b;
    }
}

static struct buf* bget(uint32 dev, uint32 blockno)
{
    spinlock_acquire(&bcache.lock);
    for (struct buf *b = bcache.head.next; b != &bcache.head; b = b->next) {
        if (b->dev == dev && b->blockno == blockno) {
            b->refcnt++;
            spinlock_release(&bcache.lock);
            sleeplock_acquire(&b->lock);
            return b;
        }
    }

    for (struct buf *b = bcache.head.prev; b != &bcache.head; b = b->prev) {
        if (b->refcnt == 0) {
            b->dev = dev;
            b->blockno = blockno;
            b->valid = 0;
            b->disk = 0;
            b->refcnt = 1;
            spinlock_release(&bcache.lock);
            sleeplock_acquire(&b->lock);
            return b;
        }
    }

    panic("bget: no buffers");
    return 0;
}

struct buf* bread(uint32 dev, uint32 blockno)
{
    struct buf *b = bget(dev, blockno);
    if (!b->valid) {
        disk_read_count++;
        virtio_disk_rw(b, 0);
        b->valid = 1;
    }
    return b;
}

void bwrite(struct buf *b)
{
    if (!sleeplock_holding(&b->lock)) {
        panic("bwrite: buf not locked");
    }
    disk_write_count++;
    virtio_disk_rw(b, 1);
}

void brelse(struct buf *b)
{
    if (!sleeplock_holding(&b->lock)) {
        panic("brelse");
    }

    sleeplock_release(&b->lock);

    spinlock_acquire(&bcache.lock);
    b->refcnt--;
    if (b->refcnt == 0) {
        b->next->prev = b->prev;
        b->prev->next = b->next;

        b->next = bcache.head.next;
        b->prev = &bcache.head;
        bcache.head.next->prev = b;
        bcache.head.next = b;
    }
    spinlock_release(&bcache.lock);
}

void bpin(struct buf *b)
{
    spinlock_acquire(&bcache.lock);
    b->refcnt++;
    spinlock_release(&bcache.lock);
}

void bunpin(struct buf *b)
{
    spinlock_acquire(&bcache.lock);
    b->refcnt--;
    spinlock_release(&bcache.lock);
}
