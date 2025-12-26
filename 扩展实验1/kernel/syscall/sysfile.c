#include "syscall.h"
#include "proc/proc.h"
#include "fs/file.h"
#include "fs/fs.h"
#include "fs/dir.h"
#include "fs/log.h"
#include "fs/pipe.h"
#include "mem/vmem.h"
#include "lib/string.h"
#include "lib/print.h"
#include "stat.h"

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

static int isdirempty(struct inode *dp)
{
    struct dirent de;
    for (uint32 off = 2 * sizeof(de); off < dp->size; off += sizeof(de)) {
        if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
            panic("isdirempty");
        if (de.inum != 0)
            return 0;
    }
    return 1;
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

    begin_op();

    struct inode *ip = 0;
    if (omode & O_CREATE) {
        ip = inode_create(path, ITYPE_FILE, 0, 0);
        if (!ip) {
            end_op();
            return -1;
        }
    } else {
        ip = namei(path);
        if (!ip) {
            end_op();
            return -1;
        }
        ilock(ip);
    }

    if (ip->type == ITYPE_DIR && omode != O_RDONLY) {
        iunlockput(ip);
        end_op();
        return -1;
    }

    struct file *f = filealloc();
    if (!f) {
        iunlockput(ip);
        end_op();
        return -1;
    }
    f->type = FD_INODE;
    f->ip = ip;
    f->off = 0;
    f->readable = (omode & O_WRONLY) ? 0 : 1;
    f->writable = (omode & (O_WRONLY | O_RDWR)) ? 1 : 0;

    int fd = fdalloc(p, f);
    if (fd < 0) {
        fileclose(f);
        iunlockput(ip);
        end_op();
        return -1;
    }
    iunlock(ip);
    end_op();
    return fd;
}

int sys_mkdir(void)
{
    char path[LAB6_MAXPATH];
    if (argstr(0, path, sizeof(path)) < 0)
        return -1;

    begin_op();
    struct inode *ip = inode_create(path, ITYPE_DIR, 0, 0);
    if (!ip) {
        end_op();
        return -1;
    }
    iunlockput(ip);
    end_op();
    return 0;
}

int sys_mknod(void)
{
    char path[LAB6_MAXPATH];
    int major, minor;
    if (argstr(0, path, sizeof(path)) < 0 ||
        argint(1, &major) < 0 ||
        argint(2, &minor) < 0) {
        return -1;
    }
    begin_op();
    struct inode *ip = inode_create(path, ITYPE_DEV, (short)major, (short)minor);
    if (!ip) {
        end_op();
        return -1;
    }
    iunlockput(ip);
    end_op();
    return 0;
}

int sys_chdir(void)
{
    char path[LAB6_MAXPATH];
    if (argstr(0, path, sizeof(path)) < 0)
        return -1;
    begin_op();
    struct inode *ip = namei(path);
    if (!ip) {
        end_op();
        return -1;
    }
    ilock(ip);
    if (ip->type != ITYPE_DIR) {
        iunlockput(ip);
        end_op();
        return -1;
    }
    iunlock(ip);
    struct proc *p = myproc();
    if (!p) {
        iput(ip);
        end_op();
        return -1;
    }
    struct inode *old = p->cwd;
    p->cwd = ip;
    if (old)
        iput(old);
    end_op();
    return 0;
}

int sys_fstat(void)
{
    int fd;
    uint64 addr;
    if (argint(0, &fd) < 0 || argaddr(1, &addr) < 0)
        return -1;
    struct proc *p = myproc();
    if (!p || fd < 0 || fd >= NOFILE)
        return -1;
    struct file *f = p->ofile[fd];
    if (!f)
        return -1;
    struct stat st;
    if (filestat(f, &st) < 0)
        return -1;
    if (copyout(p->pagetable, addr, (char*)&st, sizeof(st)) < 0)
        return -1;
    return 0;
}

int sys_dup(void)
{
    int fd;
    if (argint(0, &fd) < 0)
        return -1;
    struct proc *p = myproc();
    if (!p || fd < 0 || fd >= NOFILE)
        return -1;
    struct file *f = p->ofile[fd];
    if (!f)
        return -1;
    int newfd = fdalloc(p, f);
    if (newfd < 0)
        return -1;
    filedup(f);
    return newfd;
}

int sys_link(void)
{
    char old[LAB6_MAXPATH], new[LAB6_MAXPATH];
    if (argstr(0, old, sizeof(old)) < 0 || argstr(1, new, sizeof(new)) < 0)
        return -1;

    begin_op();
    struct inode *ip = namei(old);
    if (!ip) {
        end_op();
        return -1;
    }
    ilock(ip);
    if (ip->type == ITYPE_DIR) {
        iunlockput(ip);
        end_op();
        return -1;
    }
    ip->nlink++;
    inode_update(ip);
    iunlock(ip);

    char name[DIRSIZ];
    struct inode *dp = nameiparent(new, name);
    if (!dp) {
        ilock(ip);
        ip->nlink--;
        inode_update(ip);
        iput(ip);
        end_op();
        return -1;
    }

    ilock(dp);
    if (dirlink(dp, name, ip->inum) < 0) {
        iunlockput(dp);
        ilock(ip);
        ip->nlink--;
        inode_update(ip);
        iput(ip);
        end_op();
        return -1;
    }
    iunlockput(dp);
    iput(ip);
    end_op();
    return 0;
}

int sys_unlink(void)
{
    char path[LAB6_MAXPATH], name[DIRSIZ];
    if (argstr(0, path, sizeof(path)) < 0)
        return -1;
    begin_op();
    struct inode *dp = nameiparent(path, name);
    if (!dp) {
        end_op();
        return -1;
    }

    ilock(dp);
    if (namecmp(name, ".") == 0 || namecmp(name, "..") == 0) {
        iunlockput(dp);
        end_op();
        return -1;
    }

    uint32 off;
    struct inode *ip = dirlookup(dp, name, &off);
    if (!ip) {
        iunlockput(dp);
        end_op();
        return -1;
    }

    ilock(ip);
    if (ip->nlink < 1) {
        panic("unlink: nlink < 1");
    }
    if (ip->type == ITYPE_DIR && !isdirempty(ip)) {
        iunlockput(ip);
        iunlockput(dp);
        end_op();
        return -1;
    }

    struct dirent de = {0};
    if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de)) {
        panic("unlink: write");
    }
    if (ip->type == ITYPE_DIR) {
        dp->nlink--;
        inode_update(dp);
    }
    iunlock(dp);
    iput(dp);

    ip->nlink--;
    inode_update(ip);
    iunlockput(ip);
    end_op();
    return 0;
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

int sys_pipe(void)
{
    uint64 addr;
    if (argaddr(0, &addr) < 0)
        return -1;

    struct file *rf, *wf;
    if (pipealloc(&rf, &wf) < 0)
        return -1;

    struct proc *p = myproc();
    int fd0 = fdalloc(p, rf);
    int fd1 = fdalloc(p, wf);
    if (fd0 < 0 || fd1 < 0) {
        if (fd0 >= 0)
            p->ofile[fd0] = 0;
        fileclose(rf);
        fileclose(wf);
        return -1;
    }

    if (copyout(p->pagetable, addr, (char*)&fd0, sizeof(fd0)) < 0 ||
        copyout(p->pagetable, addr + sizeof(fd0), (char*)&fd1, sizeof(fd1)) < 0) {
        p->ofile[fd0] = 0;
        p->ofile[fd1] = 0;
        fileclose(rf);
        fileclose(wf);
        return -1;
    }

    return 0;
}
