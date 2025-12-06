#include "dev/uart.h"
#include "lib/print.h"
#include "mem/vmem.h"
#include "proc/proc.h"
#include "fs/file.h"
#include "fs/fs.h"
#include "fs/fcntl.h"
#include "syscall/syscall.h"

extern int argint(int n, int *ip);
extern int argaddr(int n, uint64 *ip);
extern int argstr(int n, char *buf, int max);

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

uint64 sys_read(void)
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

    struct proc *p = myproc();
    if (fd < 0 || fd >= NOFILE || p->ofile[fd] == 0)
        return (uint64)-1;
    begin_op();
    int r = fileread(p->ofile[fd], uva, len);
    end_op();
    return r;
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

    struct proc *p = myproc();
    if (fd == 1 || fd == 2) {
        // console output
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

    if (fd < 0 || fd >= NOFILE || p->ofile[fd] == 0)
        return (uint64)-1;
    begin_op();
    int r = filewrite(p->ofile[fd], uva, len);
    end_op();
    return r;
}

static struct inode* create(char *path, short type, short major, short minor)
{
    char name[DIRSIZ];
    struct inode *dp = nameiparent(path, name);
    if (dp == NULL)
        return NULL;

    ilock(dp);

    struct inode *ip = dirlookup(dp, name, 0);
    if (ip != NULL) {
        iunlockput(dp);
        ilock(ip);
        if (type == T_FILE && ip->type == T_FILE)
            return ip;
        iunlockput(ip);
        return NULL;
    }

    if ((ip = ialloc(dp->dev, type)) == NULL)
        panic("create: ialloc");
    ilock(ip);
    ip->major = major;
    ip->minor = minor;
    ip->nlink = 1;
    iupdate(ip);

    if (type == T_DIR) {
        dp->nlink++;
        iupdate(dp);
        if (dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
            panic("create dots");
    }

    if (dirlink(dp, name, ip->inum) < 0)
        panic("create: dirlink");

    iunlockput(dp);
    return ip;
}

uint64 sys_open(void)
{
    char path[64];
    int omode;
    if (argstr(0, path, sizeof(path)) < 0 || argint(1, &omode) < 0)
        return (uint64)-1;

    struct inode *ip;
    begin_op();
    if (omode & O_CREATE) {
        ip = create(path, T_FILE, 0, 0);
        if (ip == NULL)
            goto bad;
    } else {
        ip = namei(path);
        if (ip == NULL)
            goto bad;
        ilock(ip);
        if (ip->type == T_DIR && omode != O_RDONLY) {
            iunlockput(ip);
            goto bad;
        }
    }

    struct file *f;
    if ((f = filealloc()) == 0) {
        iunlockput(ip);
        goto bad;
    }

    f->type = FD_INODE;
    f->ip = ip;
    f->off = 0;
    f->readable = !(omode & O_WRONLY);
    f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

    int fd = fdalloc(myproc(), f);
    if (fd < 0) {
        fileclose(f);
        iunlockput(ip);
        goto bad;
    }

    iunlock(ip);
    end_op();
    return fd;
bad:
    end_op();
    return (uint64)-1;
}

uint64 sys_close(void)
{
    int fd;
    if (argint(0, &fd) < 0)
        return (uint64)-1;
    struct proc *p = myproc();
    if (fd < 0 || fd >= NOFILE || p->ofile[fd] == 0)
        return (uint64)-1;
    struct file *f = p->ofile[fd];
    p->ofile[fd] = 0;
    fileclose(f);
    return 0;
}

uint64 sys_unlink(void)
{
    char path[64];
    if (argstr(0, path, sizeof(path)) < 0)
        return (uint64)-1;

    char name[DIRSIZ];
    begin_op();
    struct inode *dp = nameiparent(path, name);
    if (dp == NULL) {
        end_op();
        return (uint64)-1;
    }

    ilock(dp);
    if (namecmp(name, ".") == 0 || namecmp(name, "..") == 0) {
        iunlockput(dp);
        end_op();
        return (uint64)-1;
    }

    uint off;
    struct inode *ip = dirlookup(dp, name, &off);
    if (ip == NULL) {
        iunlockput(dp);
        end_op();
        return (uint64)-1;
    }
    ilock(ip);
    if (ip->nlink < 1)
        panic("unlink: nlink < 1");

    struct dirent de = {0};
    if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
        panic("unlink: write");

    if (ip->type == T_DIR) {
        dp->nlink--;
        iupdate(dp);
    }
    iunlockput(dp);

    ip->nlink--;
    iupdate(ip);
    iunlockput(ip);
    end_op();
    return 0;
}
