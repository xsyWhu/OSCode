#include "lib/lock.h"
#include "proc/proc.h"

void sleeplock_init(sleeplock_t *lk, char *name)
{
    spinlock_init(&lk->lock, "sleep lock");
    lk->name = name;
    lk->locked = 0;
    lk->pid = 0;
}

void sleeplock_acquire(sleeplock_t *lk)
{
    spinlock_acquire(&lk->lock);
    while (lk->locked) {
        sleep(lk, &lk->lock);
    }
    lk->locked = 1;
    struct proc *p = myproc();
    lk->pid = p ? p->pid : -1;
    spinlock_release(&lk->lock);
}

void sleeplock_release(sleeplock_t *lk)
{
    spinlock_acquire(&lk->lock);
    lk->locked = 0;
    lk->pid = 0;
    wakeup(lk);
    spinlock_release(&lk->lock);
}

int sleeplock_holding(sleeplock_t *lk)
{
    int holding;
    spinlock_acquire(&lk->lock);
    struct proc *p = myproc();
    int pid = p ? p->pid : -1;
    holding = lk->locked && lk->pid == pid;
    spinlock_release(&lk->lock);
    return holding;
}
