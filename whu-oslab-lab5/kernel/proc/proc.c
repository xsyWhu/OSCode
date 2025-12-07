#include "lib/string.h"
#include "lib/print.h"
#include "lib/lock.h"
#include "mem/pmem.h"
#include "proc/proc.h"
#include "riscv.h"

extern void swtch(struct context *old, struct context *new);

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
    p->name[0] = '\0';

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

static void free_process(struct proc *p)
{
    if (!p)
        return;

    if (p->kstack) {
        pmem_free(p->kstack, true);
        p->kstack = 0;
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
    if (p && p->entry)
        p->entry();

    exit_process(0);
    __builtin_unreachable();
}
