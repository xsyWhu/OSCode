#include "dev/uart.h"
#include "lib/print.h"
#include "mem/vmem.h"
#include "proc/proc.h"
#include "syscall.h"

extern int argint(int n, int *ip);
extern int argaddr(int n, uint64 *ip);

uint64 sys_exit(void)
{
    int status;
    if (argint(0, &status) < 0)
        status = -1;
    exit_process(status);
    return 0; // not reached
}

uint64 sys_getpid(void)
{
    return myproc()->pid;
}

uint64 sys_write(void)
{
    int fd;
    uint64 uva;
    int len;
    if (argint(0, &fd) < 0 ||
        argaddr(1, &uva) < 0 ||
        argint(2, &len) < 0) {
        return (uint64)-1;
    }

    if (len < 0)
        return (uint64)-1;

    if (fd != 1 && fd != 2) {
        return (uint64)-1;
    }

    struct proc *p = myproc();
    char buf[128];
    int written = 0;
    while (written < len) {
        int n = len - written;
        if (n > (int)sizeof(buf))
            n = sizeof(buf);
        if (copyin(p->pagetable, buf, uva + written, n) < 0)
            return (uint64)-1;
        for (int i = 0; i < n; i++) {
            uart_putc_sync(buf[i]);
        }
        written += n;
    }
    return written;
}
