#include "fs/fs.h"
#include "lib/lock.h"
#include "lib/print.h"

// Simplified write-ahead log:
// We don't persist across crashes (RAM disk), but the interface mirrors xv6
// so the rest of the FS code can stay structured.

struct logheader {
    int n;
    int block[LOGSIZE];
};

struct log {
    spinlock_t lock;
    int start;
    int size;
    int outstanding;
    int dev;
} log;

void initlog(int dev, struct superblock *sb)
{
    log.dev = dev;
    log.size = sb->nlog;
    log.start = sb->logstart;
    log.outstanding = 0;
    spinlock_init(&log.lock, "fslog");
    // No recovery needed for RAM-backed device
}

void begin_op(void)
{
    spinlock_acquire(&log.lock);
    log.outstanding += 1;
    spinlock_release(&log.lock);
}

void end_op(void)
{
    spinlock_acquire(&log.lock);
    log.outstanding -= 1;
    spinlock_release(&log.lock);
}

void log_write(struct buf *b)
{
    // Write-through for simplicity; ignore log size checks.
    bwrite(b);
}

void recover_from_log(void)
{
    // RAM disk cannot crash; nothing to do.
}
