#ifndef __FS_LAYOUT_H__
#define __FS_LAYOUT_H__

#include "common.h"
#include "lib/lock.h"
#include "stat.h"

#define BSIZE   1024
#define BSHIFT  10
#define BPB     (BSIZE * 8)

#define ROOTDEV 1
#define ROOTINO 1

#define FSMAGIC 0x4c415637  // "LAV7"

#define NDIRECT      11
#define NINDIRECT    (BSIZE / sizeof(uint32))
#define MAXFILE      (NDIRECT + NINDIRECT)
#define LOGSIZE      30
#define MAXOPBLOCKS  10
#define NINODE       200

struct superblock {
    uint32 magic;
    uint32 size;
    uint32 nblocks;
    uint32 ninodes;
    uint32 nlog;
    uint32 logstart;
    uint32 inodestart;
    uint32 bmapstart;
};

struct dinode {
    uint16 type;
    uint16 major;
    uint16 minor;
    uint16 nlink;
    uint32 size;
    uint32 addrs[NDIRECT + 1];
    uint32 pad;
};

#define ITYPE_EMPTY 0
#define ITYPE_DIR   1
#define ITYPE_FILE  2
#define ITYPE_DEV   3

struct inode {
    uint32 dev;
    uint32 inum;
    int ref;
    sleeplock_t lock;
    int valid;

    short type;
    short major;
    short minor;
    short nlink;
    uint32 size;
    uint32 addrs[NDIRECT + 1];
};

#define IPB        (BSIZE / sizeof(struct dinode))
#define IBLOCK(i, sb) ((i) / IPB + (sb).inodestart)
#define BBLOCK(b, sb) ((b) / BPB + (sb).bmapstart)

#define DIRSIZ 14

struct dirent {
    uint16 inum;
    char name[DIRSIZ];
};

void fs_init(int dev);
const struct superblock* fs_superblock(void);
void fs_print_superblock(void);
uint32 fs_device(void);
void iinit(void);
struct inode* iget(uint32 dev, uint32 inum);
struct inode* idup(struct inode *ip);
void ilock(struct inode *ip);
void iunlock(struct inode *ip);
void iput(struct inode *ip);
void iunlockput(struct inode *ip);
struct inode* ialloc(uint32 dev, short type);
void itrunc(struct inode *ip);
uint32 inode_bmap(struct inode *ip, uint32 bn);
int readi(struct inode *ip, int user_dst, uint64 dst, uint32 off, uint32 n);
int writei(struct inode *ip, int user_src, uint64 src, uint32 off, uint32 n);
struct inode* inode_create(char *path, short type, short major, short minor);
void stati(struct inode *ip, struct stat *st);
void inode_update(struct inode *ip);

#endif
