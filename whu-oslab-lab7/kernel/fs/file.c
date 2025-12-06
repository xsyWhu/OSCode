#include "fs/file.h"
#include "fs/fs.h"
#include "lib/lock.h"
#include "lib/print.h"
#include "lib/string.h"
#include "riscv.h"

// Global file table.
static struct {
    spinlock_t lock;
    struct file file[NFILE];
} ftable;

void fileinit(void)
{
    spinlock_init(&ftable.lock, "ftable");
    memset(ftable.file, 0, sizeof(ftable.file));
}

struct file* filealloc(void)
{
    spinlock_acquire(&ftable.lock);
    for (int i = 0; i < NFILE; i++) {
        if (ftable.file[i].ref == 0) {
            ftable.file[i].ref = 1;
            spinlock_release(&ftable.lock);
            return &ftable.file[i];
        }
    }
    spinlock_release(&ftable.lock);
    return NULL;
}

struct file* filedup(struct file *f)
{
    spinlock_acquire(&ftable.lock);
    if (f->ref < 1)
        panic("filedup");
    f->ref++;
    spinlock_release(&ftable.lock);
    return f;
}

void fileclose(struct file *f)
{
    spinlock_acquire(&ftable.lock);
    if (f->ref < 1)
        panic("fileclose");
    f->ref--;
    if (f->ref > 0) {
        spinlock_release(&ftable.lock);
        return;
    }
    enum file_type type = f->type;
    struct inode *ip = f->ip;
    f->type = FD_NONE;
    f->ip = NULL;
    f->off = 0;
    f->readable = f->writable = 0;
    spinlock_release(&ftable.lock);

    if (type == FD_INODE && ip) {
        iput(ip);
    }
}

int fileread(struct file *f, uint64 addr, int n)
{
    if (f->readable == 0)
        return -1;
    if (f->type == FD_INODE) {
        ilock(f->ip);
        int r = readi(f->ip, 1, addr, f->off, n);
        if (r > 0)
            f->off += r;
        iunlock(f->ip);
        return r;
    }
    return -1;
}

int filewrite(struct file *f, uint64 addr, int n)
{
    if (f->writable == 0)
        return -1;
    if (f->type == FD_INODE) {
        int max = ((LOGSIZE-1-1-2) / 2) * BSIZE;
        int i = 0;
        while (i < n) {
            int n1 = n - i;
            if (n1 > max)
                n1 = max;

            ilock(f->ip);
            int r = writei(f->ip, 1, addr + i, f->off, n1);
            if (r > 0)
                f->off += r;
            iunlock(f->ip);

            if (r < 0)
                break;
            if (r != n1)
                panic("short filewrite");
            i += r;
        }
        return i == n ? n : -1;
    }
    return -1;
}
