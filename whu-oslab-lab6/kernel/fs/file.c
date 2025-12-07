#include "fs/file.h"
#include "dev/uart.h"

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
    spinlock_release(&ftable.lock);
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
    default:
        return -1;
    }
}
