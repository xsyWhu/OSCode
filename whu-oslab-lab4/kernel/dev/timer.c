#include "lib/lock.h"
#include "lib/print.h"
#include "dev/timer.h"
#include "memlayout.h"
#include "riscv.h"

/*-------------------- 工作在M-mode --------------------*/

// in trap.S M-mode时钟中断处理流程()
extern void timer_vector();

// 每个CPU在时钟中断中需要的临时空间(考虑为什么可以这么写)
static uint64 mscratch[NCPU][5];

// 时钟初始化
// called in start.c
void timer_init()
{
    // 1. 获取当前CPU ID
    int id = r_mhartid();
    
    // 2. 设置第一次中断时间
    *(uint64*)CLINT_MTIMECMP(id) = *(uint64*)CLINT_MTIME + INTERVAL;
    
    // 3. 为当前CPU准备mscratch数组
    mscratch[id][3] = CLINT_MTIMECMP(id);
    mscratch[id][4] = INTERVAL;
    
    // 4. 设置mscratch寄存器
    w_mscratch((uint64)&mscratch[id]);
    
    // 5. 设置M-mode trap向量
    w_mtvec((uint64)timer_vector);
    
    // 6. 使能M-mode中断
    w_mstatus(r_mstatus() | MSTATUS_MIE);
    w_mie(r_mie() | MIE_MTIE);
}


/*--------------------- 工作在S-mode --------------------*/

// 系统时钟
static timer_t sys_timer;

// 时钟创建(初始化系统时钟)
void timer_create()
{
    // 初始化自旋锁
    spinlock_init(&sys_timer.lk, "timer");
    
    // 初始化ticks为0
    sys_timer.ticks = 0;
}

// 时钟更新(ticks++ with lock)
void timer_update()
{
    // 获取锁
    spinlock_acquire(&sys_timer.lk);
    
    // 更新ticks
    sys_timer.ticks++;
    
    // 可选：唤醒等待的进程
    // wakeup(&sys_timer.ticks);
    
    // 释放锁
    spinlock_release(&sys_timer.lk);
}

// 返回系统时钟ticks
uint64 timer_get_ticks()
{
    uint64 t;
    
    // 读取ticks需要持锁（保证原子性）
    spinlock_acquire(&sys_timer.lk);
    t = sys_timer.ticks;
    spinlock_release(&sys_timer.lk);
    
    return t;
}