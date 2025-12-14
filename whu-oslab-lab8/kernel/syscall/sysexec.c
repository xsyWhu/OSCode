#include "syscall.h"
#include "proc/proc.h"
#include "mem/vmem.h"
#include "mem/pmem.h"
#include "memlayout.h"
#include "lib/string.h"

#define EXEC_PATH_MAX 64

int sys_exec(void)
{
    char path[EXEC_PATH_MAX];
    uint64 uargv = 0;
    struct proc *p = myproc();
    if (!p) {
        return -1;
    }

    if (argstr(0, path, sizeof(path)) < 0) {
        return -1;
    }
    if (argaddr(1, &uargv) < 0) {
        return -1;
    }

    const char *kargv[EXEC_MAXARG + 1];
    char *argbufs[EXEC_MAXARG + 1];
    memset(kargv, 0, sizeof(kargv));
    memset(argbufs, 0, sizeof(argbufs));

    int argc = 0;
    int ret = -1;

    if (uargv) {
        for (;; ) {
            uint64 uarg = 0;
            if (copyin(p->pagetable, &uarg, uargv + sizeof(uint64) * argc, sizeof(uint64)) < 0) {
                goto cleanup;
            }
            if (uarg == 0) {
                break;
            }
            if (argc >= EXEC_MAXARG) {
                goto cleanup;
            }
            char *buf = (char*)pmem_alloc(true);
            if (!buf) {
                goto cleanup;
            }
            if (copyinstr(p->pagetable, buf, uarg, PGSIZE) < 0) {
                pmem_free((uint64)buf, true);
                goto cleanup;
            }
            argbufs[argc] = buf;
            kargv[argc] = buf;
            argc++;
        }
    }

    ret = exec_process(p, path, kargv);

cleanup:
    for (int i = 0; i < EXEC_MAXARG + 1; i++) {
        if (argbufs[i]) {
            pmem_free((uint64)argbufs[i], true);
        }
    }
    return ret;
}
