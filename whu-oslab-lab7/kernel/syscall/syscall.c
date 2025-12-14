#include "syscall.h"
#include "lib/print.h"
#include "proc/proc.h"
#include "mem/vmem.h"

#define MAX_SYSCALL_ARGS 6

static int fetchaddr(uint64 addr, uint64 *ip) __attribute__((unused));
static int fetchstr(uint64 addr, char *buf, int max);
static uint64 argraw(struct proc *p, int n);

static int debug_syscalls = 0;

extern int sys_fork(void);
extern int sys_exit(void);
extern int sys_wait(void);
extern int sys_kill(void);
extern int sys_getpid(void);
extern int sys_pipe(void);
extern int sys_open(void);
extern int sys_close(void);
extern int sys_read(void);
extern int sys_write(void);
extern int sys_sbrk(void);
extern int sys_exec(void);
extern int sys_mkdir(void);
extern int sys_mknod(void);
extern int sys_chdir(void);
extern int sys_fstat(void);
extern int sys_unlink(void);
extern int sys_link(void);
extern int sys_dup(void);

static struct syscall_desc syscall_table[SYS_MAX] = {
    [SYS_fork]   = { sys_fork,   "fork",   0 },
    [SYS_exit]   = { sys_exit,   "exit",   1 },
    [SYS_wait]   = { sys_wait,   "wait",   1 },
    [SYS_kill]   = { sys_kill,   "kill",   1 },
    [SYS_getpid] = { sys_getpid, "getpid", 0 },
    [SYS_pipe]   = { sys_pipe,   "pipe",   1 },
    [SYS_open]   = { sys_open,   "open",   2 },
    [SYS_close]  = { sys_close,  "close",  1 },
    [SYS_read]   = { sys_read,   "read",   3 },
    [SYS_write]  = { sys_write,  "write",  3 },
    [SYS_sbrk]   = { sys_sbrk,   "sbrk",   1 },
    [SYS_exec]   = { sys_exec,   "exec",   2 },
    [SYS_mkdir]  = { sys_mkdir,  "mkdir",  1 },
    [SYS_mknod]  = { sys_mknod,  "mknod",  3 },
    [SYS_chdir]  = { sys_chdir,  "chdir",  1 },
    [SYS_fstat]  = { sys_fstat,  "fstat",  2 },
    [SYS_unlink] = { sys_unlink, "unlink", 1 },
    [SYS_link]   = { sys_link,   "link",   2 },
    [SYS_dup]    = { sys_dup,    "dup",    1 },
};

static uint64 argraw(struct proc *p, int n)
{
    if (n < 0 || n >= MAX_SYSCALL_ARGS) {
        panic("argraw: invalid arg index");
    }

    switch (n) {
    case 0: return p->trapframe->a0;
    case 1: return p->trapframe->a1;
    case 2: return p->trapframe->a2;
    case 3: return p->trapframe->a3;
    case 4: return p->trapframe->a4;
    case 5: return p->trapframe->a5;
    default:
        panic("argraw: unexpected arg index");
        return 0;
    }
}

int argint(int n, int *ip)
{
    struct proc *p = myproc();
    uint64 val = argraw(p, n);
    if (ip) {
        *ip = (int)val;
    }
    return 0;
}

int argaddr(int n, uint64 *ip)
{
    struct proc *p = myproc();
    uint64 addr = argraw(p, n);
    if (p->sz && addr >= p->sz) {
        return -1;
    }
    if (ip) {
        *ip = addr;
    }
    return 0;
}

int argstr(int n, char *buf, int max)
{
    uint64 addr;
    if (argaddr(n, &addr) < 0) {
        return -1;
    }
    return fetchstr(addr, buf, max);
}

static int __attribute__((unused)) fetchaddr(uint64 addr, uint64 *ip)
{
    struct proc *p = myproc();
    if (addr >= p->sz || addr + sizeof(uint64) > p->sz) {
        return -1;
    }
    if (copyin(p->pagetable, ip, addr, sizeof(*ip)) < 0) {
        return -1;
    }
    return 0;
}

static int fetchstr(uint64 addr, char *buf, int max)
{
    struct proc *p = myproc();
    if (max < 0) {
        return -1;
    }
    if (copyinstr(p->pagetable, buf, addr, max) < 0) {
        return -1;
    }
    return 0;
}

void syscall(void)
{
    struct proc *p = myproc();
    if (!p || !p->trapframe) {
        panic("syscall: no current process");
    }

    int num = (int)p->trapframe->a7;
    int retval = -1;

    if (num > 0 && num < SYS_MAX && syscall_table[num].func) {
        retval = syscall_table[num].func();
        if (debug_syscalls) {
            printf("pid %d: syscall %s -> %d\n", p->pid,
                   syscall_table[num].name ? syscall_table[num].name : "<unnamed>",
                   retval);
        }
    } else {
        printf("pid %d: unknown syscall %d\n", p->pid, num);
    }

    p->trapframe->a0 = retval;
}
