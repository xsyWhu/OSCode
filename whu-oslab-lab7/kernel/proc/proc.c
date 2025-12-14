#include "lib/string.h"
#include "lib/print.h"
#include "lib/lock.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "memlayout.h"
#include "fs/fs.h"
#include "proc/proc.h"
#include "trap/trap.h"
#include "riscv.h"

extern void swtch(struct context *old, struct context *new);
extern char trampoline[];

struct proc proc_table[NPROC];
struct cpu cpus[NCPU];

static int next_pid = 1;
static int proc_initialized = 0;
static spinlock_t proc_table_lock;
static spinlock_t pid_lock;

static struct proc* alloc_process(void (*entry)(void), const char *name);
static void free_process(struct proc *p);
static void proc_trampoline(void) __attribute__((noreturn));

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
    spinlock_init(&proc_table_lock, "proc_table");
    spinlock_init(&pid_lock, "pid_lock");

    for (int i = 0; i < NCPU; i++) {
        cpus[i].id = i;
    }
    for (int i = 0; i < NPROC; i++) {
        spinlock_init(&proc_table[i].lock, "proc");
        proc_table[i].state = PROC_UNUSED;
    }

    next_pid = 1;
    proc_initialized = 1;
}

static struct proc* alloc_process(void (*entry)(void), const char *name)
{
    struct proc *p = 0;

    spinlock_acquire(&proc_table_lock);
    for (int i = 0; i < NPROC; i++) {
        if (proc_table[i].state == PROC_UNUSED) {
            p = &proc_table[i];
            spinlock_acquire(&p->lock);
            p->state = PROC_USED;
            break;
        }
    }
    spinlock_release(&proc_table_lock);

    if (!p)
        return 0;

    spinlock_acquire(&pid_lock);
    p->pid = next_pid++;
    spinlock_release(&pid_lock);

    p->entry = entry;
    p->parent = myproc();
    p->exit_code = 0;
    p->killed = 0;
    p->chan = 0;
    p->sz = 0;
    p->pagetable = 0;
    p->trapframe = 0;
    p->name[0] = '\0';
    for (int i = 0; i < NOFILE; i++) {
        p->ofile[i] = 0;
    }
    p->cwd = 0;

    if (name && *name) {
        strncpy(p->name, name, sizeof(p->name) - 1);
        p->name[sizeof(p->name) - 1] = '\0';
    }

    void *stack_page = pmem_alloc(true);
    if (!stack_page) {
        p->pid = 0;
        p->parent = 0;
        p->entry = 0;
        p->state = PROC_UNUSED;
        spinlock_release(&p->lock);
        return 0;
    }

    p->kstack = (uint64)stack_page;

    struct trapframe *tf_page = (struct trapframe*)pmem_alloc(true);
    if (!tf_page) {
        pmem_free(p->kstack, true);
        p->kstack = 0;
        p->pid = 0;
        p->parent = 0;
        p->entry = 0;
        p->state = PROC_UNUSED;
        spinlock_release(&p->lock);
        return 0;
    }
    memset(tf_page, 0, PGSIZE);
    p->trapframe = tf_page;

    memset(&p->ctx, 0, sizeof(p->ctx));
    p->ctx.sp = p->kstack + PGSIZE;
    p->ctx.ra = (uint64)proc_trampoline;

    return p; // lock held
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

    p->state = PROC_RUNNABLE;
    int pid = p->pid;
    spinlock_release(&p->lock);
    return pid;
}

void exit_process(int status)
{
    struct proc *p = myproc();
    if (!p)
        panic("exit_process: no current process");

    spinlock_acquire(&p->lock);
    p->exit_code = status;
    p->state = PROC_ZOMBIE;
    spinlock_release(&p->lock);

    if (p->parent)
        wakeup(p->parent);

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

    spinlock_acquire(&proc_table_lock);
    for (;;) {
        int have_child = 0;

        for (int i = 0; i < NPROC; i++) {
            struct proc *p = &proc_table[i];

            if (p->parent != cur)
                continue;

            have_child = 1;
            spinlock_acquire(&p->lock);
            if (p->state == PROC_ZOMBIE) {
                int pid = p->pid;

                if (status)
                    *status = p->exit_code;

                free_process(p);
                spinlock_release(&p->lock);
                spinlock_release(&proc_table_lock);
                return pid;
            }
            spinlock_release(&p->lock);
        }

        if (!have_child) {
            spinlock_release(&proc_table_lock);
            return -1;
        }

        sleep(cur, &proc_table_lock);
    }
}

int kill_process(int pid)
{
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &proc_table[i];
        spinlock_acquire(&p->lock);
        if (p->state != PROC_UNUSED && p->pid == pid) {
            p->killed = 1;
            if (p->state == PROC_SLEEPING) {
                p->state = PROC_RUNNABLE;
            }
            spinlock_release(&p->lock);
            return 0;
        }
        spinlock_release(&p->lock);
    }
    return -1;
}

void userinit(void)
{
    struct proc *p = alloc_process(NULL, "init");
    if (!p) {
        panic("userinit: alloc_process failed");
    }

    const char *argv[] = { "init", NULL };
    if (exec_process(p, "/init", argv) < 0) {
        spinlock_release(&p->lock);
        panic("userinit: exec_process failed");
    }

    for (int fd = 0; fd < NOFILE && fd < 3; fd++) {
        struct file *f = filealloc();
        if (!f) {
            panic("userinit: filealloc");
        }
        f->type = FD_CONSOLE;
        f->readable = 1;
        f->writable = 1;
        p->ofile[fd] = f;
    }
    p->cwd = iget(fs_device(), ROOTINO);
    p->state = PROC_RUNNABLE;

    spinlock_release(&p->lock);
}

int fork_process(void)
{
    struct proc *p = myproc();
    if (!p || !p->pagetable) {
        return -1;
    }

    struct proc *np = alloc_process(NULL, p->name);
    if (!np) {
        return -1;
    }

    np->parent = p;
    np->sz = p->sz;

    np->pagetable = proc_pagetable(np);
    if (!np->pagetable) {
        free_process(np);
        spinlock_release(&np->lock);
        return -1;
    }

    if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0) {
        proc_freepagetable(np->pagetable, np->sz);
        np->pagetable = 0;
        free_process(np);
        spinlock_release(&np->lock);
        return -1;
    }

    memcpy(np->trapframe, p->trapframe, sizeof(*p->trapframe));
    np->trapframe->a0 = 0;

    for (int i = 0; i < NOFILE; i++) {
        if (p->ofile[i]) {
            np->ofile[i] = filedup(p->ofile[i]);
        } else {
            np->ofile[i] = 0;
        }
    }
    if (p->cwd) {
        np->cwd = idup(p->cwd);
    } else {
        np->cwd = 0;
    }

    np->state = PROC_RUNNABLE;
    int pid = np->pid;
    spinlock_release(&np->lock);
    return pid;
}

void yield(void)
{
    struct cpu *c = mycpu();
    struct proc *p = c->proc;

    if (!p)
        return;

    spinlock_acquire(&p->lock);
    p->state = PROC_RUNNABLE;
    spinlock_release(&p->lock);
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

            spinlock_acquire(&p->lock);
            if (p->state != PROC_RUNNABLE) {
                spinlock_release(&p->lock);
                continue;
            }

            c->proc = p;
            p->state = PROC_RUNNING;
            spinlock_release(&p->lock);

            swtch(&c->ctx, &p->ctx);

            c->proc = 0;
        }
    }
}

void sleep(void *chan, spinlock_t *lk)
{
    struct proc *p = myproc();
    if (!p || !lk)
        panic("sleep: invalid args");

    spinlock_acquire(&p->lock);
    spinlock_release(lk);
    p->chan = chan;
    p->state = PROC_SLEEPING;

    spinlock_release(&p->lock);
    swtch(&p->ctx, &mycpu()->ctx);

    spinlock_acquire(&p->lock);
    p->chan = 0;
    spinlock_release(&p->lock);
    spinlock_acquire(lk);
}

void wakeup(void *chan)
{
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &proc_table[i];

        spinlock_acquire(&p->lock);
        if (p->state == PROC_SLEEPING && p->chan == chan) {
            p->chan = 0;
            p->state = PROC_RUNNABLE;
        }
        spinlock_release(&p->lock);
    }
}

pagetable_t proc_pagetable(struct proc *p)
{
    pagetable_t pagetable = uvmcreate();
    if (pagetable == NULL) {
        return NULL;
    }

    // Map trampoline
    vm_mappages(pagetable, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
    // Map trapframe
    vm_mappages(pagetable, TRAPFRAME, (uint64)p->trapframe, PGSIZE, PTE_R | PTE_W);

    return pagetable;
}

void proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
    if (pagetable == NULL) {
        return;
    }
    vm_unmappages(pagetable, TRAMPOLINE, PGSIZE, false);
    vm_unmappages(pagetable, TRAPFRAME, PGSIZE, false);
    uvmfree(pagetable, sz);
}

int growproc(int n)
{
    struct proc *p = myproc();
    if (!p || !p->pagetable) {
        return -1;
    }

    uint64 sz = p->sz;
    if (n > 0) {
        uint64 newsz = sz + (uint64)n;
        uint64 res = uvmalloc(p->pagetable, sz, newsz);
        if (res == 0) {
            return -1;
        }
        p->sz = res;
    } else if (n < 0) {
        uint64 decr = (uint64)(-n);
        uint64 target = (decr > sz) ? 0 : sz - decr;
        uint64 res = uvmdealloc(p->pagetable, sz, target);
        p->sz = res;
    }
    return 0;
}

static void free_process(struct proc *p)
{
    if (!p)
        return;

    if (p->kstack) {
        pmem_free(p->kstack, true);
        p->kstack = 0;
    }

    if (p->trapframe) {
        pmem_free((uint64)p->trapframe, true);
        p->trapframe = 0;
    }

    if (p->pagetable) {
        proc_freepagetable(p->pagetable, p->sz);
        p->pagetable = 0;
    }

    for (int i = 0; i < NOFILE; i++) {
        if (p->ofile[i]) {
            fileclose(p->ofile[i]);
            p->ofile[i] = 0;
        }
    }
    if (p->cwd) {
        iput(p->cwd);
        p->cwd = 0;
    }

    p->pid = 0;
    p->parent = 0;
    p->exit_code = 0;
    p->killed = 0;
    p->chan = 0;
    p->entry = 0;
    p->name[0] = '\0';
    p->state = PROC_UNUSED;
}

static void proc_trampoline(void)
{
    struct proc *p = myproc();
    if (p && p->entry) {
        p->entry();
    } else {
        usertrapret();
    }

    exit_process(0);
    __builtin_unreachable();
}
