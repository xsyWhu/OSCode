#include "lib/print.h"
#include "mem/vmem.h"
#include "proc/proc.h"
#include "syscall.h"

extern uint64 sys_exit(void);
extern uint64 sys_getpid(void);
extern uint64 sys_write(void);

static uint64 syscalls[] = {
    [SYS_exit]   = (uint64)sys_exit,
    [SYS_getpid] = (uint64)sys_getpid,
    [SYS_write]  = (uint64)sys_write,
};

static uint64 argraw(int n);

int argint(int n, int *ip)
{
    *ip = (int)argraw(n);
    return 0;
}

int argaddr(int n, uint64 *ip)
{
    *ip = argraw(n);
    return 0;
}

int argstr(int n, char *buf, int max)
{
    uint64 addr;
    if (argaddr(n, &addr) < 0)
        return -1;
    if (copyinstr(myproc()->pagetable, buf, addr, max) < 0)
        return -1;
    return 0;
}

void syscall(void)
{
    struct proc *p = myproc();
    if (!p || !p->tf) {
        panic("syscall: no current process");
    }

    int num = p->tf->a7;
    uint64 (*handler)(void) = NULL;

    if (num > 0 && num < (int)(sizeof(syscalls) / sizeof(syscalls[0]))) {
        handler = (uint64 (*)(void))syscalls[num];
    }

    if (handler) {
        p->tf->a0 = handler();
    } else {
        printf("pid %d: unknown syscall %d\n", p->pid, num);
        p->tf->a0 = (uint64)-1;
    }
}

static uint64 argraw(int n)
{
    struct proc *p = myproc();
    if (!p || !p->tf)
        return 0;

    switch (n) {
    case 0: return p->tf->a0;
    case 1: return p->tf->a1;
    case 2: return p->tf->a2;
    case 3: return p->tf->a3;
    case 4: return p->tf->a4;
    case 5: return p->tf->a5;
    default:
        panic("argraw: invalid arg index");
        return 0;
    }
}
