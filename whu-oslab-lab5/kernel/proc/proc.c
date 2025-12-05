#include "lib/string.h"
#include "lib/print.h"
#include "mem/pmem.h"
#include "proc/proc.h"
#include "riscv.h"

extern void swtch(struct context *old, struct context *new);

struct proc proc_table[NPROC];
struct cpu cpus[NCPU];

static int next_pid = 1;
static int proc_initialized = 0;

static struct proc* alloc_process(void (*entry)(void), const char *name);
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

                if (p->kstack)
                    pmem_free(p->kstack, true);

                memset(p, 0, sizeof(*p));
                p->state = PROC_UNUSED;
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
    if (p && p->entry)
        p->entry();

    exit_process(0);
    __builtin_unreachable();
}
