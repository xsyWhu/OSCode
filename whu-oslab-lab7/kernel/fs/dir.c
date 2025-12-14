#include "fs/dir.h"
#include "fs/fs.h"
#include "lib/string.h"
#include "lib/print.h"
#include "proc/proc.h"

static char* skipelem(char *path, char *name);
static struct inode* namex(char *path, int nameiparent, char *name);

int namecmp(const char *s, const char *t)
{
    return strncmp(s, t, DIRSIZ);
}

struct inode* dirlookup(struct inode *dp, char *name, uint32 *poff)
{
    if (dp->type != ITYPE_DIR) {
        panic("dirlookup not DIR");
    }

    struct dirent de;
    for (uint32 off = 0; off < dp->size; off += sizeof(de)) {
        if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de)) {
            panic("dirlookup read");
        }
        if (de.inum == 0)
            continue;
        if (namecmp(name, de.name) == 0) {
            if (poff)
                *poff = off;
            struct inode *ip = iget(dp->dev, de.inum);
            return ip;
        }
    }
    return 0;
}

int dirlink(struct inode *dp, char *name, uint32 inum)
{
    if (dp->type != ITYPE_DIR) {
        panic("dirlink not DIR");
    }

    struct inode *ip = dirlookup(dp, name, 0);
    if (ip != 0) {
        iput(ip);
        return -1;
    }

    struct dirent de;
    uint32 off;
    for (off = 0; off < dp->size; off += sizeof(de)) {
        if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de)) {
            panic("dirlink read");
        }
        if (de.inum == 0)
            break;
    }

    memset(&de, 0, sizeof(de));
    de.inum = inum;
    safestrcpy(de.name, name, DIRSIZ);

    if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de)) {
        panic("dirlink write");
    }
    return 0;
}

static char*
skipelem(char *path, char *name)
{
    while (*path == '/')
        path++;
    if (*path == 0)
        return 0;

    char *s = path;
    while (*path != '/' && *path != 0)
        path++;

    int len = path - s;
    if (len >= DIRSIZ) {
        memmove(name, s, DIRSIZ);
    } else {
        memmove(name, s, len);
        name[len] = 0;
    }
    while (*path == '/')
        path++;
    return path;
}

static struct inode*
namex(char *path, int nameiparent, char *name)
{
    struct inode *ip;
    if (path[0] == '/') {
        ip = iget(fs_device(), ROOTINO);
    } else {
        struct proc *p = myproc();
        if (p && p->cwd) {
            ip = idup(p->cwd);
        } else {
            ip = iget(fs_device(), ROOTINO);
        }
    }
    if (ip == 0)
        return 0;

    char elem[DIRSIZ];
    if (name == 0)
        name = elem;

    while ((path = skipelem(path, name)) != 0) {
        ilock(ip);
        if (ip->type != ITYPE_DIR) {
            iunlockput(ip);
            return 0;
        }
        if (nameiparent && *path == 0) {
            iunlock(ip);
            return ip;
        }
        struct inode *next = dirlookup(ip, name, 0);
        if (next == 0) {
            iunlockput(ip);
            return 0;
        }
        iunlockput(ip);
        ip = next;
    }

    if (nameiparent) {
        iput(ip);
        return 0;
    }
    return ip;
}

struct inode* namei(char *path)
{
    return namex(path, 0, 0);
}

struct inode* nameiparent(char *path, char *name)
{
    return namex(path, 1, name);
}
