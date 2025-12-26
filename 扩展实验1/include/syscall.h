#ifndef __SYSCALL_H__
#define __SYSCALL_H__

#include "common.h"

struct syscall_desc {
    int (*func)(void);
    const char *name;
    int arg_count;
};

enum syscall_number {
    SYS_fork = 1,
    SYS_exit,
    SYS_wait,
    SYS_kill,
    SYS_getpid,
    SYS_pipe,
    SYS_open,
    SYS_close,
    SYS_read,
    SYS_write,
    SYS_sbrk,
    SYS_exec,
    SYS_mkdir,
    SYS_mknod,
    SYS_chdir,
    SYS_fstat,
    SYS_unlink,
    SYS_link,
    SYS_dup,
    SYS_setpriority,
    SYS_getpriority,
    SYS_klog,
    SYS_msgget,
    SYS_msgsend,
    SYS_msgrecv,
    SYS_MAX,
};

void syscall(void);
int argint(int n, int *ip);
int argaddr(int n, uint64 *ip);
int argstr(int n, char *buf, int max);

#endif
