#ifndef __PROC_H__
#define __PROC_H__

#include "common.h"
#include "lib/lock.h"
#include "mem/vmem.h"
#include "fs/file.h"

#define NPROC 16   // 允许存在的最大进程数
#define NOFILE 16
#define EXEC_MAXARG 16   // exec 调用支持的最大参数个数

struct trapframe {
    uint64 kernel_satp;     // 内核页表（返回内核时使用）
    uint64 kernel_sp;       // 内核栈顶
    uint64 kernel_trap;     // uservec 保存后跳转的 C 入口
    uint64 epc;             // 用户态程序计数器
    uint64 kernel_hartid;   // 当前 hart id
    uint64 ra;
    uint64 sp;
    uint64 gp;
    uint64 tp;
    uint64 t0;
    uint64 t1;
    uint64 t2;
    uint64 s0;
    uint64 s1;
    uint64 a0;
    uint64 a1;
    uint64 a2;
    uint64 a3;
    uint64 a4;
    uint64 a5;
    uint64 a6;
    uint64 a7;
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
    uint64 t3;
    uint64 t4;
    uint64 t5;
    uint64 t6;
};

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
    PROC_USED,
    PROC_RUNNABLE,
    PROC_RUNNING,
    PROC_SLEEPING,
    PROC_ZOMBIE,
};

// 进程（实际上是内核线程）描述符
struct proc {
    spinlock_t lock;      // 保护进程内部字段
    enum proc_state state;
    int pid;
    struct proc *parent;
    int exit_code;
    int killed;
    void *chan;           // sleep/wakeup 使用
    char name[16];

    uint64 kstack;        // 内核栈底（低地址）
    struct context ctx;   // 被调度时需要保存的寄存器
    uint64 sz;            // 用户内存大小
    pagetable_t pagetable;    // 用户页表
    struct trapframe *trapframe; // 用户态寄存器快照
    struct file *ofile[16];

    void (*entry)(void);  // 运行的函数
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
void sleep(void *chan, spinlock_t *lk);
void wakeup(void *chan);
int kill_process(int pid);
pagetable_t proc_pagetable(struct proc *p);
void proc_freepagetable(pagetable_t pagetable, uint64 sz);
int growproc(int n);
int fork_process(void);
int exec_process(struct proc *p, const char *path, const char *const argv[]);
void userinit(void);
void scheduler(void) __attribute__((noreturn));

#endif
