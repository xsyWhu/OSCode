#include "lib/string.h"
#include "lib/print.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "proc/proc.h"
#include "proc/trapframe.h"
#include "memlayout.h"
#include "trap/trap.h"
#include "riscv.h"

extern void swtch(struct context *old, struct context *new);
extern char trampoline[];
extern char uservec[];
extern char userret[];
extern char initcode_start[];
extern char initcode_end[];
extern void kernel_vector(void);
void syscall(void);

struct proc proc_table[NPROC];
struct cpu cpus[NCPU];

static int next_pid = 1;
static int proc_initialized = 0;

static struct proc* alloc_process(void (*entry)(void), const char *name);
static int alloc_trapframe(struct proc *p);
static pgtbl_t proc_pagetable(struct proc *p);
static void proc_freepagetable(struct proc *p);
static void free_process(struct proc *p);
static void proc_trampoline(void) __attribute__((noreturn));
static int handle_device_interrupt(uint64 scause);

int mycpuid(void)
{
    return (int)r_tp();
}

cpu_t* mycpu(void)
{
    return &cpus[mycpuid()];
}

struct proc* myproc(void)
{
    struct cpu *c = mycpu();
    return c->proc;
}

void proc_init(void)
{
    if (proc_initialized)
        return;

    memset(proc_table, 0, sizeof(proc_table));
    memset(cpus, 0, sizeof(cpus));

    for (int i = 0; i < NCPU; i++) {
        cpus[i].id = i;
    }

    next_pid = 1;
    proc_initialized = 1;
}

static struct proc* alloc_process(void (*entry)(void), const char *name)
{
    struct proc *p = 0;

    for (int i = 0; i < NPROC; i++) {
        if (proc_table[i].state == PROC_UNUSED) {
            p = &proc_table[i];
            break;
        }
    }

    if (!p)
        return 0;

    memset(p, 0, sizeof(*p));

    p->pid = next_pid++;
    p->state = PROC_RUNNABLE;
    p->entry = entry;
    p->parent = myproc();
    p->sz = 0;
    p->pagetable = NULL;
    p->tf = NULL;
    p->killed = 0;

    if (name && *name) {
        strncpy(p->name, name, sizeof(p->name) - 1);
        p->name[sizeof(p->name) - 1] = '\0';
    }

    void *stack_page = pmem_alloc(true);
    if (!stack_page) {
        memset(p, 0, sizeof(*p));
        return 0;
    }

    p->kstack = (uint64)stack_page;

    memset(&p->ctx, 0, sizeof(p->ctx));
    p->ctx.sp = p->kstack + PGSIZE;
    p->ctx.ra = (uint64)proc_trampoline;

    return p;
}

int create_process(void (*entry)(void), const char *name)
{
    if (!entry)
        return -1;

    if (!proc_initialized)
        proc_init();

    struct proc *p = alloc_process(entry, name);
    if (!p)
        return -1;

    return p->pid;
}

void exit_process(int status)
{
    struct proc *p = myproc();
    if (!p)
        panic("exit_process: no current process");

    p->exit_code = status;
    p->state = PROC_ZOMBIE;

    struct cpu *c = mycpu();
    swtch(&p->ctx, &c->ctx);

    panic("exit_process returned");
    while (1) {
        // should never reach here, keep function noreturn
    }
}

int wait_process(int *status)
{
    struct proc *cur = myproc();
    if (!cur)
        return -1;

    for (;;) {
        int have_child = 0;

        for (int i = 0; i < NPROC; i++) {
            struct proc *p = &proc_table[i];

            if (p->parent != cur)
                continue;

            have_child = 1;
            if (p->state == PROC_ZOMBIE) {
                int pid = p->pid;

                if (status)
                    *status = p->exit_code;

                free_process(p);
                return pid;
            }
        }

        if (!have_child)
            return -1;

        yield();
    }
}

void yield(void)
{
    struct cpu *c = mycpu();
    struct proc *p = c->proc;

    if (!p)
        return;

    p->state = PROC_RUNNABLE;
    swtch(&p->ctx, &c->ctx);
}

void scheduler(void)
{
    struct cpu *c = mycpu();
    c->proc = 0;

    for (;;) {
        intr_on();

        for (int i = 0; i < NPROC; i++) {
            struct proc *p = &proc_table[i];

            if (p->state != PROC_RUNNABLE)
                continue;

            c->proc = p;
            p->state = PROC_RUNNING;

            swtch(&c->ctx, &p->ctx);

            c->proc = 0;
        }
    }
}

static void proc_trampoline(void)
{
    struct proc *p = myproc();
    if (!p)
        panic("proc_trampoline: no proc");

    if (p->entry) {
        p->entry();
        exit_process(0);
    }

    usertrapret();
    __builtin_unreachable();
}

static int alloc_trapframe(struct proc *p)
{
    if (p->tf)
        return 0;
    void *page = pmem_alloc(true);
    if (!page)
        return -1;
    memset(page, 0, PGSIZE);
    p->tf = (struct trapframe*)page;
    return 0;
}

static pgtbl_t proc_pagetable(struct proc *p)
{
    pgtbl_t pagetable = uvm_create();
    if (!pagetable)
        return NULL;

    // Map trampoline (shared text).
    vm_mappages(pagetable, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

    // Map per-process trapframe page.
    vm_mappages(pagetable, TRAPFRAME, (uint64)p->tf, PGSIZE, PTE_R | PTE_W);

    return pagetable;
}

static void proc_freepagetable(struct proc *p)
{
    if (!p->pagetable)
        return;
    vm_unmappages(p->pagetable, TRAMPOLINE, PGSIZE, false);
    vm_unmappages(p->pagetable, TRAPFRAME, PGSIZE, false);
    uvm_free(p->pagetable, p->sz);
    p->pagetable = NULL;
}

static void free_process(struct proc *p)
{
    if (p->pagetable)
        proc_freepagetable(p);

    if (p->tf) {
        pmem_free((uint64)p->tf, true);
        p->tf = NULL;
    }

    if (p->kstack) {
        pmem_free(p->kstack, true);
        p->kstack = 0;
    }

    memset(p, 0, sizeof(*p));
    p->state = PROC_UNUSED;
}

void usertrap(void)
{
    struct proc *p = myproc();
    if (!p)
        panic("usertrap: no process");
    if (!p->tf || !p->pagetable)
        panic("usertrap: no trapframe/pagetable");

    uint64 sstatus = r_sstatus();
    if (sstatus & SSTATUS_SPP) {
        panic("usertrap: not from user mode");
    }

    // 切换到内核 trap 向量，避免嵌套时仍走 trampoline
    w_stvec((uint64)kernel_vector);

    uint64 scause = r_scause();
    p->tf->epc = r_sepc();

    if (scause == 8) {
        // 系统调用
        if (p->killed)
            exit_process(-1);

        p->tf->epc += 4;
        intr_on();
        syscall();
    } else if (handle_device_interrupt(scause)) {
        // 已处理（例如时钟/外设中断）
    } else {
        printf("usertrap: unexpected scause=%p stval=%p pid=%d\n",
               scause, r_stval(), p->pid);
        p->killed = 1;
    }

    if (p->killed)
        exit_process(-1);

    usertrapret();
}

void usertrapret(void)
{
    struct proc *p = myproc();
    if (!p)
        panic("usertrapret: no process");

    intr_off();

    // 设置 trap 向量指向 trampoline 的 uservec
    uint64 trampoline_uservec = TRAMPOLINE + (uint64)(uservec - trampoline);
    w_stvec(trampoline_uservec);

    // 准备 trapframe 中的内核上下文信息
    p->tf->kernel_satp = MAKE_SATP(kernel_pagetable_get());
    p->tf->kernel_sp = p->kstack + PGSIZE;
    p->tf->kernel_trap = (uint64)usertrap;
    p->tf->kernel_hartid = mycpuid();

    // 允许用户态中断
    uint64 sstatus = r_sstatus();
    sstatus &= ~SSTATUS_SPP;   // 下一次 sret 返回到用户态
    sstatus |= SSTATUS_SPIE;   // 使能用户态中断
    w_sstatus(sstatus);

    w_sepc(p->tf->epc);

    uint64 satp = MAKE_SATP(p->pagetable);
    uint64 fn = TRAMPOLINE + (uint64)(userret - trampoline);
    ((void (*)(uint64, uint64))fn)(TRAPFRAME, satp);
    __builtin_unreachable();
}

void userinit(void)
{
    struct proc *p = alloc_process(NULL, "init");
    if (!p)
        panic("userinit: alloc_process failed");

    if (alloc_trapframe(p) < 0)
        panic("userinit: alloc trapframe failed");

    p->pagetable = proc_pagetable(p);
    if (!p->pagetable)
        panic("userinit: pagetable failed");

    uint64 init_len = (uint64)(initcode_end - initcode_start);
    uint64 alloc_sz = PG_ROUND_UP(init_len);

    uint64 sz = uvm_alloc(p->pagetable, 0, alloc_sz);
    if (sz == 0)
        panic("userinit: uvm_alloc failed");
    p->sz = sz;

    if (uvm_load(p->pagetable, 0, (uint8*)initcode_start, init_len) < 0)
        panic("userinit: uvm_load failed");

    p->tf->epc = 0;
    p->tf->sp = alloc_sz;
    p->parent = NULL;
    p->state = PROC_RUNNABLE;
}

static int handle_device_interrupt(uint64 scause)
{
    if ((scause & (1UL << 63)) == 0)
        return 0;

    uint64 sip = r_sip();
    if (sip & SIE_SSIE) {
        w_sip(sip & ~SIE_SSIE);
        timer_interrupt_handler();
        return 1;
    }

    if (sip & SIE_SEIE) {
        external_interrupt_handler();
        return 1;
    }

    return 0;
}
