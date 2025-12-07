#include "syscall.h"
#include "proc/proc.h"
#include "fs/file.h"
#include "mem/vmem.h"
#include "lib/string.h"

#define LAB6_MAXPATH 128

static int fdalloc(struct proc *p, struct file *f)
{
    for (int fd = 0; fd < NOFILE; fd++) {
        if (p->ofile[fd] == 0) {
            p->ofile[fd] = f;
            return fd;
        }
    }
    return -1;
}

static int open_console(struct proc *p, int omode)
{
    struct file *f = filealloc();
    if (!f)
        return -1;
    f->type = FD_CONSOLE;
    f->readable = (omode == O_RDONLY) || (omode == O_RDWR);
    f->writable = (omode == O_WRONLY) || (omode == O_RDWR) || (omode & O_CREATE);
    int fd = fdalloc(p, f);
    if (fd < 0) {
        fileclose(f);
    }
    return fd;
}

static int is_console_path(const char *path)
{
    const char *target = "/dev/console";
    for (int i = 0;; i++) {
        if (target[i] == '\0' && path[i] == '\0')
            return 1;
        if (target[i] != path[i])
            return 0;
        if (target[i] == '\0')
            return 0;
    }
}

int sys_open(void)
{
    char path[LAB6_MAXPATH];
    int omode;
    if (argstr(0, path, sizeof(path)) < 0 || argint(1, &omode) < 0) {
        return -1;
    }
    struct proc *p = myproc();
    if (!p)
        return -1;

    if (is_console_path(path)) {
        return open_console(p, omode);
    }
    return -1;
}

int sys_close(void)
{
    int fd;
    if (argint(0, &fd) < 0)
        return -1;
    struct proc *p = myproc();
    if (!p)
        return -1;
    if (fd < 0 || fd >= NOFILE || p->ofile[fd] == 0)
        return -1;
    struct file *f = p->ofile[fd];
    p->ofile[fd] = 0;
    fileclose(f);
    return 0;
}

int sys_write(void)
{
    int fd, n;
    uint64 addr;
    if (argint(0, &fd) < 0 || argaddr(1, &addr) < 0 || argint(2, &n) < 0)
        return -1;
    struct proc *p = myproc();
    if (!p)
        return -1;
    if (fd < 0 || fd >= NOFILE)
        return -1;
    struct file *f = p->ofile[fd];
    if (!f || !f->writable)
        return -1;

    int total = 0;
    char buf[128];
    while (total < n) {
        int m = n - total;
        if (m > sizeof(buf))
            m = sizeof(buf);
        if (copyin(p->pagetable, buf, addr + total, m) < 0)
            return -1;
        int r = filewrite(f, buf, m);
        if (r < 0)
            return -1;
        total += r;
        if (r < m)
            break;
    }
    return total;
}

int sys_read(void)
{
    int fd, n;
    uint64 addr;
    if (argint(0, &fd) < 0 || argaddr(1, &addr) < 0 || argint(2, &n) < 0)
        return -1;
    struct proc *p = myproc();
    if (!p)
        return -1;
    if (fd < 0 || fd >= NOFILE)
        return -1;
    struct file *f = p->ofile[fd];
    if (!f || !f->readable)
        return -1;

    int total = 0;
    char buf[128];
    while (total < n) {
        int m = n - total;
        if (m > sizeof(buf))
            m = sizeof(buf);
        int r = fileread(f, buf, m);
        if (r < 0)
            return -1;
        if (r == 0)
            break;
        if (copyout(p->pagetable, addr + total, buf, r) < 0)
            return -1;
        total += r;
        if (r < m)
            break;
    }
    return total;
}
