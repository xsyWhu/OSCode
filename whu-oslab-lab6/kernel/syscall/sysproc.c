#include "syscall.h"
#include "proc/proc.h"
#include "mem/vmem.h"

int sys_fork(void)
{
    return fork_process();
}

int sys_exit(void)
{
    int status;
    if (argint(0, &status) < 0) {
        status = -1;
    }
    exit_process(status);
    return 0;
}

int sys_wait(void)
{
    uint64 uaddr;
    if (argaddr(0, &uaddr) < 0) {
        return -1;
    }

    int status = 0;
    int pid = wait_process(&status);
    if (pid < 0) {
        return -1;
    }

    if (uaddr != 0) {
        struct proc *p = myproc();
        if (!p->pagetable || copyout(p->pagetable, uaddr, &status, sizeof(status)) < 0) {
            return -1;
        }
    }
    return pid;
}

int sys_kill(void)
{
    int pid;
    if (argint(0, &pid) < 0) {
        return -1;
    }
    return kill_process(pid);
}

int sys_getpid(void)
{
    return myproc()->pid;
}

int sys_sbrk(void)
{
    int n;
    if (argint(0, &n) < 0) {
        return -1;
    }
    struct proc *p = myproc();
    uint64 addr = p->sz;
    if (growproc(n) < 0) {
        return -1;
    }
    return addr;
}
