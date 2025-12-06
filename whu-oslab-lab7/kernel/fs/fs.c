#include "fs/fs.h"
#include "fs/file.h"
#include "lib/lock.h"
#include "lib/print.h"
#include "lib/string.h"
#include "proc/proc.h"
#include "mem/vmem.h"

// Core filesystem implementation (adapted from xv6, simplified for RAM disk).

// Convenience for bitmap helpers
#define MIN(a, b) ((a) < (b) ? (a) : (b))

void iinit(int dev);
extern void virtio_disk_init(void);

// Global superblock
static struct superblock sb;
static spinlock_t icache_lock;
static struct {
    struct inode inode[NINODE];
} icache;

void fs_get_superblock(struct superblock *out)
{
    if (out)
        memmove(out, &sb, sizeof(sb));
}

// Read the super block.
static void readsb(int dev, struct superblock *sb_out)
{
    struct buf *bp = bread(dev, 1);
    memmove(sb_out, bp->data, sizeof(*sb_out));
    brelse(bp);
}

// Write superblock back to disk.
static void writesb(int dev, struct superblock *sb_in)
{
    struct buf *bp = bread(dev, 1);
    memmove(bp->data, sb_in, sizeof(*sb_in));
    bwrite(bp);
    brelse(bp);
}

static void bfree(int dev, uint b);
static uint balloc(uint dev);
static struct inode* dirlookup_int(struct inode *dp, char *name, uint *poff);
static void mark_reserved(void);

// Initialize FS structures and format RAM disk if needed.
void fs_init(void)
{
    binit();
    fileinit();
    virtio_disk_init();

    readsb(ROOTDEV, &sb);
    if (sb.magic != FSMAGIC) {
        // Format fresh filesystem
        memset(&sb, 0, sizeof(sb));
        sb.magic = FSMAGIC;
        sb.size = RAMDISK_BLOCKS;
        sb.nlog = LOGSIZE;
        sb.ninodes = 200;
        sb.logstart = 2;
        sb.inodestart = sb.logstart + sb.nlog;
        sb.bmapstart = sb.inodestart + (sb.ninodes / IPB) + 1;
        sb.nblocks = sb.size;
        writesb(ROOTDEV, &sb);
        initlog(ROOTDEV, &sb); // init log before using log_write
        mark_reserved();

        // Allocate root inode
        iinit(ROOTDEV);
        struct inode *root = ialloc(ROOTDEV, T_DIR);
        if (!root || root->inum != ROOTINO) {
            panic("fs_init: failed to create root");
        }
        ilock(root);
        dirlink(root, ".", root->inum);
        dirlink(root, "..", root->inum);
        iunlockput(root);
    } else {
        iinit(ROOTDEV);
        recover_from_log();
        initlog(ROOTDEV, &sb);
    }
}

static void mark_reserved(void)
{
    uint data_start = sb.bmapstart + 1;
    for (uint b = 0; b < data_start; b++) {
        struct buf *bp = bread(ROOTDEV, BBLOCK(b, sb));
        int bi = b % BPB;
        int m = 1 << (bi % 8);
        bp->data[bi/8] |= m;
        log_write(bp);
        brelse(bp);
    }
}

int fs_count_free_blocks(void)
{
    int free = 0;
    for (uint b = 0; b < sb.nblocks; b += BPB) {
        struct buf *bp = bread(ROOTDEV, BBLOCK(b, sb));
        for (int bi = 0; bi < BPB && b + bi < sb.nblocks; bi++) {
            int m = 1 << (bi % 8);
            if ((bp->data[bi/8] & m) == 0)
                free++;
        }
        brelse(bp);
    }
    return free;
}

int fs_count_free_inodes(void)
{
    int free = 0;
    for (int inum = 1; inum < sb.ninodes; inum++) {
        struct buf *bp = bread(ROOTDEV, IBLOCK(inum, sb));
        struct dinode *dip = (struct dinode*)bp->data + (inum % IPB);
        if (dip->type == T_UNUSED)
            free++;
        brelse(bp);
    }
    return free;
}

void fs_debug_icache(void)
{
    spinlock_acquire(&icache_lock);
    printf("=== Icache ===\n");
    for (int i = 0; i < NINODE; i++) {
        struct inode *ip = &icache.inode[i];
        if (ip->ref > 0) {
            printf("  slot=%d dev=%d inum=%d ref=%d type=%d size=%d nlink=%d\n",
                   i, ip->dev, ip->inum, ip->ref, ip->type, ip->size, ip->nlink);
        }
    }
    printf("==============\n");
    spinlock_release(&icache_lock);
}

// Inode cache init.
void iinit(int dev)
{
    spinlock_init(&icache_lock, "icache");
    for (int i = 0; i < NINODE; i++) {
        icache.inode[i].ref = 0;
        icache.inode[i].valid = 0;
        icache.inode[i].dev = dev;
    }
}

// Allocate a new disk block.
static uint balloc(uint dev)
{
    struct buf *bp;
    uint data_start = sb.bmapstart + 1;
    for (uint b = data_start; b < sb.size; b += BPB) {
        bp = bread(dev, BBLOCK(b, sb));
        for (int bi = 0; bi < BPB && b + bi < sb.nblocks; bi++) {
            int m = 1 << (bi % 8);
            if ((bp->data[bi/8] & m) == 0) {
                bp->data[bi/8] |= m;
                log_write(bp);
                brelse(bp);
                bzero(bp = bread(dev, b + bi));
                log_write(bp);
                brelse(bp);
                return b + bi;
            }
        }
        brelse(bp);
    }
    panic("balloc: out of blocks");
    return 0;
}

// Free a disk block.
static void bfree(int dev, uint b)
{
    uint data_start = sb.bmapstart + 1;
    if (b < data_start) {
        printf("bfree warning: freeing reserved block %u\n", b);
        return;
    }
    struct buf *bp = bread(dev, BBLOCK(b, sb));
    int bi = b % BPB;
    int m = 1 << (bi % 8);
    if ((bp->data[bi/8] & m) == 0) {
        printf("bfree warning: block %u already free\n", b);
        brelse(bp);
        return;
    }
    bp->data[bi/8] &= ~m;
    log_write(bp);
    brelse(bp);
}

// Return an inode from the cache, allocating if necessary.
struct inode* iget(uint dev, uint inum)
{
    spinlock_acquire(&icache_lock);
    struct inode *empty = 0;

    for (int i = 0; i < NINODE; i++) {
        struct inode *ip = &icache.inode[i];
        if (ip->ref > 0 && ip->dev == dev && ip->inum == inum) {
            ip->ref++;
            spinlock_release(&icache_lock);
            return ip;
        }
        if (empty == 0 && ip->ref == 0) {
            empty = ip;
        }
    }

    if (empty == 0) {
        spinlock_release(&icache_lock);
        panic("iget: no inodes");
    }

    empty->dev = dev;
    empty->inum = inum;
    empty->ref = 1;
    empty->valid = 0;
    spinlock_release(&icache_lock);
    return empty;
}

// Lock the given inode.
void ilock(struct inode *ip)
{
    if (ip == 0 || ip->ref < 1)
        panic("ilock");
    if (ip->valid == 0) {
        struct buf *bp = bread(ip->dev, IBLOCK(ip->inum, sb));
        struct dinode *dip = (struct dinode*)bp->data + (ip->inum % IPB);
        ip->type = dip->type;
        ip->major = dip->major;
        ip->minor = dip->minor;
        ip->nlink = dip->nlink;
        ip->size = dip->size;
        memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
        brelse(bp);
        ip->valid = 1;
        if (ip->type == T_UNUSED) {
            panic("ilock: no type");
        }
    }
}

void iunlock(struct inode *ip)
{
    if (ip == 0 || ip->ref < 1)
        panic("iunlock");
    // nothing else; we don't use sleep locks
}

void iupdate(struct inode *ip)
{
    struct buf *bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    struct dinode *dip = (struct dinode*)bp->data + (ip->inum % IPB);
    dip->type = ip->type;
    dip->major = ip->major;
    dip->minor = ip->minor;
    dip->nlink = ip->nlink;
    dip->size = ip->size;
    memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
    log_write(bp);
    brelse(bp);
}

void iput(struct inode *ip)
{
    spinlock_acquire(&icache_lock);
    if (ip->ref == 1 && ip->valid && ip->nlink == 0) {
        // Truncate and free inode
        spinlock_release(&icache_lock);

        // Free blocks
        for (int i = 0; i < NDIRECT; i++) {
            if (ip->addrs[i]) {
                bfree(ip->dev, ip->addrs[i]);
                ip->addrs[i] = 0;
            }
        }
        if (ip->addrs[NDIRECT]) {
            struct buf *bp = bread(ip->dev, ip->addrs[NDIRECT]);
            uint *a = (uint*)bp->data;
            for (int j = 0; j < NINDIRECT; j++) {
                if (a[j])
                    bfree(ip->dev, a[j]);
            }
            brelse(bp);
            bfree(ip->dev, ip->addrs[NDIRECT]);
            ip->addrs[NDIRECT] = 0;
        }
        ip->size = 0;
        ip->type = T_UNUSED;
        iupdate(ip);

        spinlock_acquire(&icache_lock);
        ip->valid = 0;
    }
    ip->ref--;
    spinlock_release(&icache_lock);
}

void iunlockput(struct inode *ip)
{
    iunlock(ip);
    iput(ip);
}

// Allocate an inode.
struct inode* ialloc(uint dev, short type)
{
    for (int inum = 1; inum < sb.ninodes; inum++) {
        struct buf *bp = bread(dev, IBLOCK(inum, sb));
        struct dinode *dip = (struct dinode*)bp->data + (inum % IPB);
        if (dip->type == T_UNUSED) {
            memset(dip, 0, sizeof(*dip));
            dip->type = type;
            log_write(bp);
            brelse(bp);
            struct inode *ip = iget(dev, inum);
            ip->type = type;
            ip->nlink = 1;
            ip->size = 0;
            ip->major = 0;
            ip->minor = 0;
            memset(ip->addrs, 0, sizeof(ip->addrs));
            iupdate(ip);
            return ip;
        }
        brelse(bp);
    }
    panic("ialloc: no inodes");
    return NULL;
}

// Return the disk block address of the nth block in inode ip.
static uint bmap(struct inode *ip, uint bn)
{
    if (bn < NDIRECT) {
        if (ip->addrs[bn] == 0)
            ip->addrs[bn] = balloc(ip->dev);
        return ip->addrs[bn];
    }
    bn -= NDIRECT;

    if (bn < NINDIRECT) {
        // Load indirect block, allocating if necessary.
        if (ip->addrs[NDIRECT] == 0)
            ip->addrs[NDIRECT] = balloc(ip->dev);
        struct buf *bp = bread(ip->dev, ip->addrs[NDIRECT]);
        uint *a = (uint*)bp->data;
        uint addr = a[bn];
        if (addr == 0) {
            addr = balloc(ip->dev);
            a[bn] = addr;
            log_write(bp);
        }
        brelse(bp);
        return addr;
    }
    panic("bmap: out of range");
    return 0;
}

// Read data from inode into destination.
int readi(struct inode *ip, int user, uint64 dst, uint off, uint n)
{
    if (off > ip->size || off + n < off)
        return 0;
    if (off + n > ip->size)
        n = ip->size - off;

    uint tot = 0;
    while (tot < n) {
        uint bn = bmap(ip, off / BSIZE);
        struct buf *bp = bread(ip->dev, bn);
        uint m = MIN(n - tot, BSIZE - (off % BSIZE));
        if (user) {
            if (copyout(myproc()->pagetable, dst + tot, bp->data + (off % BSIZE), m) < 0) {
                brelse(bp);
                break;
            }
        } else {
            memmove((void *)(dst + tot), bp->data + (off % BSIZE), m);
        }
        brelse(bp);
        tot += m;
        off += m;
    }
    return tot;
}

// Write data to inode.
int writei(struct inode *ip, int user, uint64 src, uint off, uint n)
{
    if (off > ip->size || off + n < off)
        return -1;
    if (off + n > MAXFILE * BSIZE)
        return -1;

    uint tot = 0;
    while (tot < n) {
        uint bn = bmap(ip, off / BSIZE);
        struct buf *bp = bread(ip->dev, bn);
        uint m = MIN(n - tot, BSIZE - (off % BSIZE));
        if (user) {
            if (copyin(myproc()->pagetable, bp->data + (off % BSIZE), src + tot, m) < 0) {
                brelse(bp);
                return -1;
            }
        } else {
            memmove(bp->data + (off % BSIZE), (void *)(src + tot), m);
        }
        log_write(bp);
        brelse(bp);
        tot += m;
        off += m;
    }

    if (n > 0 && off > ip->size)
        ip->size = off;
    iupdate(ip);
    return n;
}

// Directories
int namecmp(const char *s, const char *t)
{
    return strncmp(s, t, DIRSIZ);
}

struct inode* dirlookup(struct inode *dp, char *name, uint *poff)
{
    if (dp->type != T_DIR)
        panic("dirlookup not DIR");

    return dirlookup_int(dp, name, poff);
}

static struct inode* dirlookup_int(struct inode *dp, char *name, uint *poff)
{
    struct dirent de;
    for (uint off = 0; off < dp->size; off += sizeof(de)) {
        if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
            panic("dirlookup read");
        if (de.inum == 0)
            continue;
        if (namecmp(name, de.name) == 0) {
            if (poff)
                *poff = off;
            return iget(dp->dev, de.inum);
        }
    }
    return NULL;
}

int dirlink(struct inode *dp, char *name, uint inum)
{
    struct dirent de;
    uint off;

    if (dirlookup(dp, name, 0) != 0)
        return -1;

    // Look for empty dirent.
    for (off = 0; off < dp->size; off += sizeof(de)) {
        if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
            panic("dirlink read");
        if (de.inum == 0)
            break;
    }

    strncpy(de.name, name, DIRSIZ);
    de.name[DIRSIZ-1] = '\0';
    de.inum = inum;
    if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
        panic("dirlink write");

    return 0;
}

// Path parsing helpers
static char* skipelem(char *path, char *name)
{
    while (*path == '/')
        path++;
    if (*path == 0)
        return 0;
    char *s = path;
    while (*path != '/' && *path != 0)
        path++;
    int len = path - s;
    if (len >= DIRSIZ)
        memmove(name, s, DIRSIZ);
    else {
        memmove(name, s, len);
        name[len] = 0;
    }
    while (*path == '/')
        path++;
    return path;
}

static struct inode* namex(char *path, int nameiparent, char *name)
{
    struct inode *ip, *next;

    if (*path == '/')
        ip = iget(ROOTDEV, ROOTINO);
    else
        ip = iget(ROOTDEV, ROOTINO); // no cwd support; always root

    while ((path = skipelem(path, name)) != 0) {
        ilock(ip);
        if (ip->type != T_DIR) {
            iunlockput(ip);
            return NULL;
        }
        if (nameiparent && *path == '\0') {
            iunlock(ip);
            return ip;
        }
        if ((next = dirlookup(ip, name, 0)) == 0) {
            iunlockput(ip);
            return NULL;
        }
        iunlockput(ip);
        ip = next;
    }
    if (nameiparent) {
        iput(ip);
        return NULL;
    }
    return ip;
}

struct inode* namei(char *path)
{
    char name[DIRSIZ];
    return namex(path, 0, name);
}

struct inode* nameiparent(char *path, char *name)
{
    return namex(path, 1, name);
}
