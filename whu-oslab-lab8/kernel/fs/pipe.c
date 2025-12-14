#include "fs/pipe.h"
#include "fs/file.h"
#include "lib/string.h"
#include "mem/pmem.h"
#include "proc/proc.h"

#define PIPESIZE 512

struct pipe {
    spinlock_t lock;
    char data[PIPESIZE];
    uint32 nread;
    uint32 nwrite;
    int readopen;
    int writeopen;
};

static struct pipe* pipealloc_struct(void)
{
    struct pipe *pi = (struct pipe*)pmem_alloc(true);
    if (!pi)
        return 0;
    memset(pi, 0, sizeof(*pi));
    spinlock_init(&pi->lock, "pipe");
    pi->readopen = 1;
    pi->writeopen = 1;
    return pi;
}

int pipealloc(struct file **f0, struct file **f1)
{
    struct file *rf = 0, *wf = 0;
    struct pipe *pi = 0;

    if ((*f0 = filealloc()) == 0 || (*f1 = filealloc()) == 0) {
        goto bad;
    }
    rf = *f0;
    wf = *f1;

    pi = pipealloc_struct();
    if (!pi) {
        goto bad;
    }

    rf->type = FD_PIPE;
    rf->readable = 1;
    rf->writable = 0;
    rf->pipe = pi;

    wf->type = FD_PIPE;
    wf->readable = 0;
    wf->writable = 1;
    wf->pipe = pi;

    return 0;

bad:
    if (rf) {
        fileclose(rf);
        *f0 = 0;
    }
    if (wf) {
        fileclose(wf);
        *f1 = 0;
    }
    if (pi) {
        pmem_free((uint64)pi, true);
    }
    return -1;
}

void pipeclose(struct pipe *pi, int writable)
{
    if (!pi)
        return;

    spinlock_acquire(&pi->lock);
    if (writable) {
        pi->writeopen = 0;
        wakeup(pi);
    } else {
        pi->readopen = 0;
        wakeup(pi);
    }
    int readopen = pi->readopen;
    int writeopen = pi->writeopen;
    spinlock_release(&pi->lock);
    if (!readopen && !writeopen) {
        pmem_free((uint64)pi, true);
    }
}

int pipewrite(struct pipe *pi, const char *addr, int n)
{
    int i = 0;
    spinlock_acquire(&pi->lock);
    while (i < n) {
        while (pi->nwrite == pi->nread + PIPESIZE) {
            if (!pi->readopen || myproc()->killed) {
                spinlock_release(&pi->lock);
                return -1;
            }
            wakeup(pi);
            sleep(pi, &pi->lock);
        }
        pi->data[pi->nwrite % PIPESIZE] = addr[i];
        pi->nwrite++;
        i++;
    }
    wakeup(pi);
    spinlock_release(&pi->lock);
    return i;
}

int piperead(struct pipe *pi, char *addr, int n)
{
    int i = 0;
    spinlock_acquire(&pi->lock);
    while (i < n) {
        while (pi->nread == pi->nwrite) {
            if (!pi->writeopen) {
                spinlock_release(&pi->lock);
                return i;
            }
            if (myproc()->killed) {
                spinlock_release(&pi->lock);
                return -1;
            }
            sleep(pi, &pi->lock);
        }
        addr[i] = pi->data[pi->nread % PIPESIZE];
        pi->nread++;
        i++;
    }
    wakeup(pi);
    spinlock_release(&pi->lock);
    return i;
}
