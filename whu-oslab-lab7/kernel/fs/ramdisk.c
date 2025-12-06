#include "fs/fs.h"
#include "lib/lock.h"
#include "lib/print.h"
#include "lib/string.h"

// Simple RAM-backed block device used as the single disk.
// This keeps the filesystem self-contained for the lab environment.

#define RAMDISK_BLOCKS 8192   // 8192 * 4KB = 32MB in-memory disk
static uint8 ramdisk[RAMDISK_BLOCKS][BSIZE];
static spinlock_t ramdisk_lk;
static uint64 rd_reads = 0;
static uint64 rd_writes = 0;

void ramdisk_init(void)
{
    spinlock_init(&ramdisk_lk, "ramdisk");
    memset(ramdisk, 0, sizeof(ramdisk));
}

static void ramdisk_rw(int write, uint blockno, void *data)
{
    if (blockno >= RAMDISK_BLOCKS) {
        panic("ramdisk: blockno out of range");
    }
    spinlock_acquire(&ramdisk_lk);
    if (write) {
        memmove(ramdisk[blockno], data, BSIZE);
        rd_writes++;
    } else {
        memmove(data, ramdisk[blockno], BSIZE);
        rd_reads++;
    }
    spinlock_release(&ramdisk_lk);
}

void ramdisk_read(uint blockno, void *data)
{
    ramdisk_rw(0, blockno, data);
}

void ramdisk_write(uint blockno, void *data)
{
    ramdisk_rw(1, blockno, data);
}

uint64 ramdisk_get_reads(void)  { return rd_reads; }
uint64 ramdisk_get_writes(void) { return rd_writes; }
