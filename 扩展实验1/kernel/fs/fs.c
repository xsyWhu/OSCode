#include "fs/fs.h"
#include "fs/bio.h"
#include "fs/dir.h"
#include "fs/log.h"
#include "lib/print.h"
#include "lib/string.h"

static struct superblock sb;
static int fs_dev = ROOTDEV;

static struct {
    spinlock_t lock;
    struct inode inode[NINODE];
} icache;

static void read_superblock(int dev, struct superblock *dst);
static void iupdate(struct inode *ip);
static uint32 balloc(uint32 dev);
static void bfree(uint32 dev, uint32 b);

void fs_init(int dev)
{
    fs_dev = dev;
    read_superblock(dev, &sb);
    if (sb.magic != FSMAGIC) {
        printf("Superblock magic mismatch: got 0x%x expect 0x%x\n",
               sb.magic, FSMAGIC);
        panic("fs_init: invalid filesystem image");
    }

    printf("[fs] init start\n");
    log_init(dev, &sb);
    printf("[fs] log initialized\n");
    iinit();

    struct inode *root_ip = iget(dev, ROOTINO);
    ilock(root_ip);
    printf("[fs] root inode type=%d valid=%d\n", root_ip->type, root_ip->valid);
    if (root_ip->type == ITYPE_EMPTY) {
        printf("[fs] root missing, creating directory\n");
        begin_op();
        root_ip->type = ITYPE_DIR;
        root_ip->nlink = 2;
        root_ip->size = 2 * sizeof(struct dirent);
        uint32 data_block = balloc(dev);
        root_ip->addrs[0] = data_block;
        struct buf *bp = bread(dev, data_block);
        struct dirent *de = (struct dirent*)bp->data;
        de[0].inum = ROOTINO;
        safestrcpy(de[0].name, ".", DIRSIZ);
        de[1].inum = ROOTINO;
        safestrcpy(de[1].name, "..", DIRSIZ);
        log_write(bp);
        brelse(bp);
        iupdate(root_ip);
        end_op();
    }
    iunlockput(root_ip);

    printf("[fs] size=%u blocks=%u ninodes=%u nlog=%u\n",
           sb.size, sb.nblocks, sb.ninodes, sb.nlog);
}

const struct superblock* fs_superblock(void)
{
    return &sb;
}

void fs_print_superblock(void)
{
    printf("\n=== Superblock ===\n");
    printf(" magic     = 0x%x\n", sb.magic);
    printf(" size      = %u blocks\n", sb.size);
    printf(" nblocks   = %u data blocks\n", sb.nblocks);
    printf(" ninodes   = %u\n", sb.ninodes);
    printf(" nlog      = %u\n", sb.nlog);
    printf(" logstart  = %u\n", sb.logstart);
    printf(" inodestart= %u\n", sb.inodestart);
    printf(" bmapstart = %u\n", sb.bmapstart);
    printf("===================\n");
}

uint32 fs_device(void)
{
    return fs_dev;
}

void iinit(void)
{
    spinlock_init(&icache.lock, "icache");
    for (int i = 0; i < NINODE; i++) {
        icache.inode[i].ref = 0;
        icache.inode[i].valid = 0;
        sleeplock_init(&icache.inode[i].lock, "inode");
    }
}

static void read_superblock(int dev, struct superblock *dst)
{
    printf("[fs] read_superblock: start (dev=%d)\n", dev);
    struct buf *b = bread(dev, 1);
    if (!b) {
        panic("read_superblock: bread failed");
    }
    printf("[fs] read_superblock: bread done\n");
    memmove(dst, b->data, sizeof(*dst));
    brelse(b);
}

static struct inode* iget_locked(uint32 dev, uint32 inum)
{
    struct inode *empty = 0;

    spinlock_acquire(&icache.lock);
    for (int i = 0; i < NINODE; i++) {
        struct inode *ip = &icache.inode[i];
        if (ip->ref > 0 && ip->dev == dev && ip->inum == inum) {
            ip->ref++;
            spinlock_release(&icache.lock);
            return ip;
        }
        if (empty == 0 && ip->ref == 0) {
            empty = ip;
        }
    }

    if (empty == 0) {
        spinlock_release(&icache.lock);
        panic("iget: no inodes");
    }

    empty->dev = dev;
    empty->inum = inum;
    empty->ref = 1;
    empty->valid = 0;

    spinlock_release(&icache.lock);
    return empty;
}

struct inode* iget(uint32 dev, uint32 inum)
{
    return iget_locked(dev, inum);
}

struct inode* idup(struct inode *ip)
{
    spinlock_acquire(&icache.lock);
    ip->ref++;
    spinlock_release(&icache.lock);
    return ip;
}

void ilock(struct inode *ip)
{
    if (ip == 0 || ip->ref < 1) {
        panic("ilock");
    }

    sleeplock_acquire(&ip->lock);
    if (!ip->valid) {
        struct buf *bp = bread(ip->dev, IBLOCK(ip->inum, sb));
        struct dinode *dip = (struct dinode*)bp->data;
        dip += ip->inum % IPB;
        ip->type = dip->type;
        ip->major = dip->major;
        ip->minor = dip->minor;
        ip->nlink = dip->nlink;
        ip->size = dip->size;
        memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
        brelse(bp);
        ip->valid = 1;
        if (ip->type == ITYPE_EMPTY) {
            printf("[fs] ilock panic: inode=%d dip_type=0 block=%d\n",
                   ip->inum, IBLOCK(ip->inum, sb));
            panic("ilock: no type");
        }
    }
}

void iunlock(struct inode *ip)
{
    if (ip == 0 || !sleeplock_holding(&ip->lock) || ip->ref < 1) {
        panic("iunlock");
    }
    sleeplock_release(&ip->lock);
}

void iput(struct inode *ip)
{
    if (ip == 0)
        return;

    spinlock_acquire(&icache.lock);
    if (ip->ref == 1 && ip->valid && ip->nlink == 0) {
        spinlock_release(&icache.lock);
        ilock(ip);
        itrunc(ip);
        ip->type = ITYPE_EMPTY;
        iupdate(ip);
        ip->valid = 0;
        iunlock(ip);

        spinlock_acquire(&icache.lock);
    }

    ip->ref--;
    spinlock_release(&icache.lock);
}

void iunlockput(struct inode *ip)
{
    iunlock(ip);
    iput(ip);
}

static void iupdate(struct inode *ip)
{
    struct buf *bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    struct dinode *dip = (struct dinode*)bp->data;
    dip += ip->inum % IPB;
    dip->type = ip->type;
    dip->major = ip->major;
    dip->minor = ip->minor;
    dip->nlink = ip->nlink;
    dip->size = ip->size;
    memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
    log_write(bp);
    brelse(bp);
}

static uint8*
bmap_data(struct buf *bp, uint32 bi)
{
    return &bp->data[bi / 8];
}

static uint32 balloc(uint32 dev)
{
    for (uint32 b = 0; b < sb.size; b += BPB) {
        struct buf *bp = bread(dev, BBLOCK(b, sb));
        for (uint32 bi = 0; bi < BPB && (b + bi) < sb.size; bi++) {
            uint32 mask = 1 << (bi & 7);
            uint8 *byte = bmap_data(bp, bi);
            if ((*byte & mask) == 0) {
                *byte |= mask;
                log_write(bp);
                brelse(bp);

                uint32 blockno = b + bi;
                struct buf *clr = bread(dev, blockno);
                memset(clr->data, 0, BSIZE);
                log_write(clr);
                brelse(clr);
                return blockno;
            }
        }
        brelse(bp);
    }
    panic("balloc: out of blocks");
    return 0;
}

static void bfree(uint32 dev, uint32 b)
{
    struct buf *bp = bread(dev, BBLOCK(b, sb));
    uint32 bi = b % BPB;
    uint32 mask = 1 << (bi & 7);
    uint8 *byte = bmap_data(bp, bi);
    if ((*byte & mask) == 0) {
        panic("bfree");
    }
    *byte &= ~mask;
    log_write(bp);
    brelse(bp);
}

uint32 inode_bmap(struct inode *ip, uint32 bn)
{
    if (bn < NDIRECT) {
        if (ip->addrs[bn] == 0) {
            ip->addrs[bn] = balloc(ip->dev);
        }
        return ip->addrs[bn];
    }
    bn -= NDIRECT;

    if (bn >= NINDIRECT) {
        panic("bmap: out of range");
    }

    if (ip->addrs[NDIRECT] == 0) {
        ip->addrs[NDIRECT] = balloc(ip->dev);
    }

    struct buf *bp = bread(ip->dev, ip->addrs[NDIRECT]);
    uint32 *a = (uint32*)bp->data;
        if (a[bn] == 0) {
            a[bn] = balloc(ip->dev);
            log_write(bp);
        }
        uint32 addr = a[bn];
        brelse(bp);
    return addr;
}

void itrunc(struct inode *ip)
{
    for (int i = 0; i < NDIRECT; i++) {
        if (ip->addrs[i]) {
            bfree(ip->dev, ip->addrs[i]);
            ip->addrs[i] = 0;
        }
    }

    if (ip->addrs[NDIRECT]) {
        struct buf *bp = bread(ip->dev, ip->addrs[NDIRECT]);
        uint32 *a = (uint32*)bp->data;
        for (int i = 0; i < NINDIRECT; i++) {
            if (a[i]) {
                bfree(ip->dev, a[i]);
            }
        }
        brelse(bp);
        bfree(ip->dev, ip->addrs[NDIRECT]);
        ip->addrs[NDIRECT] = 0;
    }
    ip->size = 0;
    iupdate(ip);
}

struct inode* inode_create(char *path, short type, short major, short minor)
{
    char name[DIRSIZ];
    struct inode *dp = nameiparent(path, name);
    if (dp == 0)
        return 0;

    ilock(dp);
    struct inode *ip = dirlookup(dp, name, 0);
    if (ip != 0) {
        iunlockput(dp);
        ilock(ip);
        if (type == ITYPE_FILE && ip->type == ITYPE_FILE) {
            return ip;
        }
        iunlockput(ip);
        return 0;
    }

    ip = ialloc(dp->dev, type);
    ilock(ip);
    ip->major = major;
    ip->minor = minor;
    ip->nlink = 1;
    iupdate(ip);

    if (type == ITYPE_DIR) {
        dp->nlink++;
        iupdate(dp);
        if (dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0) {
            panic("inode_create dots");
        }
    }

    if (dirlink(dp, name, ip->inum) < 0) {
        panic("inode_create dirlink");
    }

    iunlockput(dp);
    return ip;
}

void stati(struct inode *ip, struct stat *st)
{
    st->type = ip->type;
    st->major = ip->major;
    st->minor = ip->minor;
    st->nlink = ip->nlink;
    st->dev = ip->dev;
    st->inum = ip->inum;
    st->size = ip->size;
}

void inode_update(struct inode *ip)
{
    iupdate(ip);
}

static int either_copyout(int user_dst, uint64 dst, void *src, uint32 len)
{
    if (user_dst) {
        panic("copyout not supported");
    } else {
        memmove((void*)dst, src, len);
    }
    return 0;
}

static int either_copyin(void *dst, int user_src, uint64 src, uint32 len)
{
    if (user_src) {
        panic("copyin not supported");
    } else {
        memmove(dst, (void*)src, len);
    }
    return 0;
}

int readi(struct inode *ip, int user_dst, uint64 dst, uint32 off, uint32 n)
{
    if (off > ip->size || off + n < off)
        return 0;
    if (off + n > ip->size)
        n = ip->size - off;

    uint32 tot = 0;
    while (tot < n) {
        uint32 bn = (off + tot) / BSIZE;
        uint32 addr = inode_bmap(ip, bn);
        struct buf *bp = bread(ip->dev, addr);
        uint32 start = (off + tot) % BSIZE;
        uint32 m = n - tot;
        if (m > BSIZE - start)
            m = BSIZE - start;
        if (either_copyout(user_dst, dst + tot, bp->data + start, m) < 0) {
            brelse(bp);
            break;
        }
        brelse(bp);
        tot += m;
    }
    return tot;
}

int writei(struct inode *ip, int user_src, uint64 src, uint32 off, uint32 n)
{
    if (off > ip->size || off + n < off)
        return -1;

    uint32 tot = 0;
    while (tot < n) {
        uint32 bn = (off + tot) / BSIZE;
        uint32 addr = inode_bmap(ip, bn);
        struct buf *bp = bread(ip->dev, addr);
        uint32 start = (off + tot) % BSIZE;
        uint32 m = n - tot;
        if (m > BSIZE - start)
            m = BSIZE - start;
        if (either_copyin(bp->data + start, user_src, src + tot, m) < 0) {
            brelse(bp);
            break;
        }
        log_write(bp);
        brelse(bp);
        tot += m;
    }

    if (off + tot > ip->size) {
        ip->size = off + tot;
    }
    iupdate(ip);
    return tot;
}
struct inode* ialloc(uint32 dev, short type)
{
    for (uint32 inum = 1; inum < sb.ninodes; inum++) {
        struct buf *bp = bread(dev, IBLOCK(inum, sb));
        struct dinode *dip = (struct dinode*)bp->data;
        dip += inum % IPB;
        if (dip->type == ITYPE_EMPTY) {
            memset(dip, 0, sizeof(*dip));
            dip->type = type;
            log_write(bp);
            brelse(bp);
            struct inode *ip = iget(dev, inum);
            return ip;
        }
        brelse(bp);
    }
    panic("ialloc: no inodes");
    return 0;
}
