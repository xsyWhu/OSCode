#include "fs/log.h"
#include "fs/bio.h"
#include "fs/fs.h"
#include "lib/lock.h"
#include "lib/print.h"
#include "lib/string.h"
#include "proc/proc.h"

struct logheader {
    int n;
    uint32 block[LOGSIZE];
};

struct log_state {
    spinlock_t lock;
    int start;
    int size;
    int outstanding;
    int committing;
    int dev;
    struct logheader lh;
};

static struct log_state log_state;

static void write_head(void);
static void read_head(void);
static void write_log(void);
static void install_trans(int recovering);
static void recover_from_log(void);
static void commit(void);

void log_init(int dev, const struct superblock *sb)
{
    if (sb->nlog > LOGSIZE)
        panic("log_init: nlog too large");
    if (sb->nlog < 2)
        panic("log_init: nlog too small");

    spinlock_init(&log_state.lock, "fslog");
    log_state.start = sb->logstart;
    log_state.size = sb->nlog;
    log_state.dev = dev;
    log_state.outstanding = 0;
    log_state.committing = 0;
    log_state.lh.n = 0;

    recover_from_log();
}

void begin_op(void)
{
    spinlock_acquire(&log_state.lock);
    for (;;) {
        if (log_state.committing) {
            sleep(&log_state, &log_state.lock);
        } else if (log_state.lh.n + (log_state.outstanding + 1) * MAXOPBLOCKS > log_state.size - 1) {
            sleep(&log_state, &log_state.lock);
        } else {
            log_state.outstanding++;
            spinlock_release(&log_state.lock);
            break;
        }
    }
}

void end_op(void)
{
    int do_commit = 0;

    spinlock_acquire(&log_state.lock);
    log_state.outstanding--;
    if (log_state.committing)
        panic("log_state committing");
    if (log_state.outstanding == 0) {
        do_commit = 1;
        log_state.committing = 1;
    } else {
        wakeup(&log_state);
    }
    spinlock_release(&log_state.lock);

    if (!do_commit)
        return;

    commit();

    spinlock_acquire(&log_state.lock);
    log_state.committing = 0;
    wakeup(&log_state);
    spinlock_release(&log_state.lock);
}

void log_write(struct buf *b)
{
    int i;

    spinlock_acquire(&log_state.lock);
    if (log_state.lh.n >= log_state.size - 1 || log_state.lh.n >= LOGSIZE) {
        spinlock_release(&log_state.lock);
        panic("log_write: log full");
    }
    if (log_state.outstanding < 1) {
        spinlock_release(&log_state.lock);
        panic("log_write outside transaction");
    }
    for (i = 0; i < log_state.lh.n; i++) {
        if (log_state.lh.block[i] == b->blockno) {
            break;
        }
    }
    log_state.lh.block[i] = b->blockno;
    if (i == log_state.lh.n) {
        log_state.lh.n++;
    }
    spinlock_release(&log_state.lock);

    bpin(b);
}

static void write_head(void)
{
    struct buf *bp = bread(log_state.dev, log_state.start);
    struct logheader *lh = (struct logheader*)bp->data;
    lh->n = log_state.lh.n;
    for (int i = 0; i < log_state.lh.n; i++) {
        lh->block[i] = log_state.lh.block[i];
    }
    bwrite(bp);
    brelse(bp);
}

static void read_head(void)
{
    struct buf *bp = bread(log_state.dev, log_state.start);
    struct logheader *lh = (struct logheader*)bp->data;
    log_state.lh.n = lh->n;
    for (int i = 0; i < log_state.lh.n; i++) {
        log_state.lh.block[i] = lh->block[i];
    }
    brelse(bp);
}

static void install_trans(int recovering)
{
    for (int i = 0; i < log_state.lh.n; i++) {
        struct buf *lbuf = bread(log_state.dev, log_state.start + 1 + i);
        struct buf *dbuf = bread(log_state.dev, log_state.lh.block[i]);
        memmove(dbuf->data, lbuf->data, BSIZE);
        bwrite(dbuf);
        if (!recovering) {
            bunpin(dbuf);
        }
        brelse(lbuf);
        brelse(dbuf);
    }
}

static void write_log(void)
{
    for (int i = 0; i < log_state.lh.n; i++) {
        struct buf *to = bread(log_state.dev, log_state.start + 1 + i);
        struct buf *from = bread(log_state.dev, log_state.lh.block[i]);
        memmove(to->data, from->data, BSIZE);
        bwrite(to);
        brelse(from);
        brelse(to);
    }
}

static void recover_from_log(void)
{
    read_head();
    install_trans(1);
    log_state.lh.n = 0;
    write_head();
}

static void commit(void)
{
    if (log_state.lh.n > 0) {
        write_log();
        write_head();
        install_trans(0);
        log_state.lh.n = 0;
        write_head();
    }
}
