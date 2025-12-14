#include "fs/file.h"
#include "fs/fs.h"
#include "fs/log.h"
#include "fs/pipe.h"
#include "dev/uart.h"
#include "lib/print.h"

#define NFILE 32

static struct {
    spinlock_t lock;
    struct file file[NFILE];
} ftable;

void fileinit(void)
{
    spinlock_init(&ftable.lock, "ftable");
    for (int i = 0; i < NFILE; i++) {
        ftable.file[i].type = FD_NONE;
        ftable.file[i].ref = 0;
        ftable.file[i].readable = 0;
        ftable.file[i].writable = 0;
        ftable.file[i].off = 0;
        ftable.file[i].ip = 0;
        ftable.file[i].pipe = 0;
    }
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
    return 0;
}

struct file* filedup(struct file *f)
{
    if (!f) {
        return 0;
    }
    spinlock_acquire(&ftable.lock);
    f->ref++;
    spinlock_release(&ftable.lock);
    return f;
}

void fileclose(struct file *f)
{
    if (!f)
        return;

    spinlock_acquire(&ftable.lock);
    if (--f->ref > 0) {
        spinlock_release(&ftable.lock);
        return;
    }
    f->type = FD_NONE;
    f->readable = 0;
    f->writable = 0;
    f->off = 0;
    struct inode *ip = f->ip;
    struct pipe *pi = f->pipe;
    f->ip = 0;
    f->pipe = 0;
    spinlock_release(&ftable.lock);

    if (ip) {
        iput(ip);
    }
    if (pi) {
        pipeclose(pi, f->writable);
    }
}

static int consoleread(char *dst, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        int c = uart_getc_sync();
        if (c < 0)
            break;
        dst[i] = (char)c;
        if (dst[i] == '\n') {
            i++;
            break;
        }
    }
    return i;
}

static int consolewrite(const char *src, int n)
{
    for (int i = 0; i < n; i++) {
        uart_putc_sync(src[i]);
    }
    return n;
}

int fileread(struct file *f, char *dst, int n)
{
    if (!f || !f->readable)
        return -1;

    switch (f->type) {
    case FD_CONSOLE:
        return consoleread(dst, n);
    case FD_INODE: {
        ilock(f->ip);
        int r = readi(f->ip, 0, (uint64)dst, f->off, n);
        if (r > 0)
            f->off += r;
        iunlock(f->ip);
        return r;
    }
    case FD_PIPE:
        return piperead(f->pipe, dst, n);
    default:
        return -1;
    }
}

int filewrite(struct file *f, const char *src, int n)
{
    if (!f || !f->writable)
        return -1;

    switch (f->type) {
    case FD_CONSOLE:
        return consolewrite(src, n);
    case FD_INODE: {
        int i = 0;
        int r = 0;
        int max = ((MAXOPBLOCKS - 1 - 1 - 2) / 2) * BSIZE;
        if (max < BSIZE)
            max = BSIZE;
        while (i < n) {
            int n1 = n - i;
            if (n1 > max)
                n1 = max;

            begin_op();
            ilock(f->ip);
            r = writei(f->ip, 0, (uint64)src + i, f->off, n1);
            if (r > 0)
                f->off += r;
            iunlock(f->ip);
            end_op();

            if (r < 0)
                break;
            if (r != n1)
                panic("filewrite: short");
            i += r;
        }
        return (i == n) ? n : -1;
    }
    case FD_PIPE:
        return pipewrite(f->pipe, src, n);
    default:
        return -1;
    }
}

int filestat(struct file *f, struct stat *st)
{
    if (!f || !st)
        return -1;
    switch (f->type) {
    case FD_INODE:
        ilock(f->ip);
        stati(f->ip, st);
        iunlock(f->ip);
        return 0;
    default:
        return -1;
    }
}
