#ifndef __FS_BIO_H__
#define __FS_BIO_H__

#include "common.h"
#include "fs/fs.h"
#include "lib/lock.h"

struct buf {
    int valid;
    int disk;
    uint32 dev;
    uint32 blockno;
    sleeplock_t lock;
    int refcnt;
    struct buf *prev;
    struct buf *next;
    uint8 data[BSIZE];
};

extern uint64 disk_read_count;
extern uint64 disk_write_count;

void binit(void);
struct buf* bread(uint32 dev, uint32 blockno);
void bwrite(struct buf *b);
void brelse(struct buf *b);
void bpin(struct buf *b);
void bunpin(struct buf *b);

#endif
