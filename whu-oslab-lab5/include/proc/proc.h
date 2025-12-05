#ifndef __PROC_H__
#define __PROC_H__

#include "common.h"

#define NPROC 16   // 允许存在的最大进程数

struct proc;

// 保存上下文所需的寄存器（与 swtch.S 中一致）
struct context {
    uint64 ra;
    uint64 sp;
    uint64 s0;
    uint64 s1;
    uint64 s2;
    uint64 s3;
    uint64 s4;
    uint64 s5;
    uint64 s6;
    uint64 s7;
    uint64 s8;
    uint64 s9;
    uint64 s10;
    uint64 s11;
};

// 简化的进程状态
enum proc_state {
    PROC_UNUSED = 0,
    PROC_RUNNABLE,
    PROC_RUNNING,
    PROC_SLEEPING,
    PROC_ZOMBIE,
};

// 进程（实际上是内核线程）描述符
struct proc {
    int pid;
    enum proc_state state;
    char name[16];

    uint64 kstack;        // 内核栈底（低地址）
    struct context ctx;   // 被调度时需要保存的寄存器

    void (*entry)(void);  // 运行的函数
    struct proc *parent;  // 创建它的进程
    int exit_code;

    void *chan;           // 预留给 sleep/wakeup
};

// 每个 CPU 的状态
struct cpu {
    struct proc *proc;    // 当前运行的进程
    struct context ctx;   // 调度器上下文
    int id;
    int started;
};

typedef struct cpu cpu_t;

extern struct proc proc_table[NPROC];
extern struct cpu cpus[NCPU];

struct cpu* mycpu(void);
int mycpuid(void);
struct proc* myproc(void);

void proc_init(void);
int create_process(void (*entry)(void), const char *name);
void exit_process(int status) __attribute__((noreturn));
int wait_process(int *status);
void yield(void);
void scheduler(void) __attribute__((noreturn));

#endif
