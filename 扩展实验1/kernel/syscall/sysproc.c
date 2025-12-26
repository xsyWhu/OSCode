#include "syscall.h"
#include "proc/proc.h"
#include "mem/vmem.h"
#include "lib/klog.h"

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

int sys_setpriority(void)
{
    int pid;
    if (argint(0, &pid) < 0) {
        return -1;
    }
    int priority;
    if (argint(1, &priority) < 0) {
        return -1;
    }
    return setpriority(pid, priority);
}

int sys_getpriority(void)
{
    int pid;
    if (argint(0, &pid) < 0) {
        return -1;
    }
    return getpriority(pid);
}

int sys_klog(void)
{
    uint64 uaddr;
    int n;
    if (argaddr(0, &uaddr) < 0 || argint(1, &n) < 0) {
        return -1;
    }
    if (n <= 0) {
        return 0;
    }
    char buf[LOG_BUF_SIZE];
    int r = klog_read(buf, n > LOG_BUF_SIZE ? LOG_BUF_SIZE : n);
    if (r <= 0) {
        return r;
    }
    struct proc *p = myproc();
    if (!p || !p->pagetable) {
        return -1;
    }
    if (copyout(p->pagetable, uaddr, buf, r) < 0) {
        return -1;
    }
    return r;
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
