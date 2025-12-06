#include "fs/fs.h"
#include "lib/lock.h"
#include "lib/print.h"
#include "lib/string.h"
#include "dev/virtio_blk.h"

// Simple buffer cache (no hashing). Keeps a small LRU list.

#define NBUF 32

static struct {
    spinlock_t lock;
    struct buf buf[NBUF];
    struct buf head; // dummy head of LRU list
} bcache;

static uint64 cache_hits = 0;
static uint64 cache_misses = 0;

void binit(void)
{
    spinlock_init(&bcache.lock, "bcache");

    // Create doubly-linked list of buffers
    bcache.head.prev = &bcache.head;
    bcache.head.next = &bcache.head;
    for (int i = 0; i < NBUF; i++) {
        struct buf *b = &bcache.buf[i];
        b->next = bcache.head.next;
        b->prev = &bcache.head;
        bcache.head.next->prev = b;
        bcache.head.next = b;
        b->refcnt = 0;
        b->valid = 0;
    }
}

// Remove b from LRU list.
static void bremove(struct buf *b)
{
    b->next->prev = b->prev;
    b->prev->next = b->next;
}

// Insert b at head of LRU list.
static void bpushfront(struct buf *b)
{
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
}

// Look through buffer cache for block on device dev.
// If not found, recycle an unused buffer.
// Caller must hold no locks.
static struct buf* bget(uint dev, uint blockno)
{
    spinlock_acquire(&bcache.lock);

    // Is the block already cached?
    for (struct buf *b = bcache.head.next; b != &bcache.head; b = b->next) {
        if (b->dev == dev && b->blockno == blockno) {
            b->refcnt++;
            if (b->valid)
                cache_hits++;
            spinlock_release(&bcache.lock);
            return b;
        }
    }

    // Not cached; recycle least-recently-used buffer.
    for (struct buf *b = bcache.head.prev; b != &bcache.head; b = b->prev) {
        if (b->refcnt == 0) {
            b->dev = dev;
            b->blockno = blockno;
            b->valid = 0;
            b->refcnt = 1;
            spinlock_release(&bcache.lock);
            return b;
        }
    }

    spinlock_release(&bcache.lock);
    panic("bget: no buffers");
    return NULL;
}

struct buf* bread(uint dev, uint blockno)
{
    struct buf *b = bget(dev, blockno);
    if (!b)
        return NULL;
    if (!b->valid) {
        virtio_disk_rw(b, 0);
        b->valid = 1;
        cache_misses++;
    }
    return b;
}

void bwrite(struct buf *b)
{
    if (!b)
        return;
    virtio_disk_rw(b, 1);
    b->valid = 1;
}

void brelse(struct buf *b)
{
    if (!b)
        return;
    spinlock_acquire(&bcache.lock);
    if (b->refcnt < 1)
        panic("brelse");
    b->refcnt--;
    if (b->refcnt == 0) {
        // Move to front to avoid immediate reuse churn.
        bremove(b);
        bpushfront(b);
    }
    spinlock_release(&bcache.lock);
}

void bzero(struct buf *b)
{
    if (!b)
        return;
    memset(b->data, 0, BSIZE);
}

uint64 bcache_get_hits(void)   { return cache_hits; }
uint64 bcache_get_misses(void) { return cache_misses; }
