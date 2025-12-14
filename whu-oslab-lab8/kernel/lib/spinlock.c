#include "lib/lock.h"
#include "lib/print.h"
#include "proc/proc.h"
#include "riscv.h"

// per-CPU interrupt nesting counts
static int ncli[NCPU];
static int intena[NCPU];

// 关闭中断（带嵌套计数）
void push_off(void)
{
    int cpu = mycpuid();
    unsigned long old = r_sstatus();
    intr_off();
    if (ncli[cpu] == 0) {
        intena[cpu] = (old & SSTATUS_SIE) != 0;
    }
    ncli[cpu] += 1;
}

// 恢复中断（带嵌套计数）
void pop_off(void)
{
    if (r_sstatus() & SSTATUS_SIE)
        panic("pop_off - interruptible");

    int cpu = mycpuid();
    if (--ncli[cpu] < 0)
        panic("pop_off - ncli < 0");

    if (ncli[cpu] == 0 && intena[cpu])
        intr_on();
}

// 判断是否持有自旋锁
bool spinlock_holding(spinlock_t *lk)
{
    return (lk->locked && lk->cpuid == r_tp());
}

// 初始化自旋锁
void spinlock_init(spinlock_t *lk, char *name)
{
    lk->locked = 0;
    lk->name = name;
    lk->cpuid = -1;
}

// 获取自旋锁
void spinlock_acquire(spinlock_t *lk)
{
    push_off();
    if (spinlock_holding(lk))
        panic(lk->name ? lk->name : "acquire");

    while (__sync_lock_test_and_set(&lk->locked, 1) != 0)
        ;

    __sync_synchronize();
    lk->cpuid = r_tp();
}

// 释放自旋锁
void spinlock_release(spinlock_t *lk)
{
    if (!spinlock_holding(lk))
        panic("release");

    lk->cpuid = -1;
    __sync_synchronize();
    __sync_lock_release(&lk->locked);

    pop_off();
}
