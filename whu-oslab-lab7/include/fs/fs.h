// Basic filesystem definitions (simplified xv6-style)
#ifndef __FS_H__
#define __FS_H__

#include "common.h"

#define BSIZE        4096        // block size
#define ROOTDEV      1           // single RAM disk device id
#define ROOTINO      1           // root inode number
#define RAMDISK_BLOCKS 8192      // blocks available in RAM disk

#define LOGSIZE      10          // fixed-size log for the RAM disk
#define NINODE       50          // max in-memory inodes

// On-disk file system format.
// Block 0 is unused.
// Block 1 is superblock.
// Next is log blocks, then inode blocks, then free bitmap, then data.

struct superblock {
    uint magic;      // must be FSMAGIC
    uint size;       // total blocks
    uint nblocks;    // data blocks
    uint ninodes;    // number of inodes
    uint nlog;       // number of log blocks
    uint logstart;   // log start block
    uint inodestart; // inode start block
    uint bmapstart;  // free map start block
};

#define FSMAGIC 0x10203040

#define NDIRECT   12
#define NINDIRECT (BSIZE/sizeof(uint))
#define MAXFILE   (NDIRECT + NINDIRECT)

// On-disk inode structure
struct dinode {
    short type;           // copy of enum inode_type
    short major;
    short minor;
    short nlink;
    uint size;
    uint addrs[NDIRECT+1]; // data block addresses
};

// Supported inode types
enum inode_type {
    T_UNUSED = 0,
    T_DIR    = 1,
    T_FILE   = 2,
    T_DEV    = 3,
};

#define DIRSIZ 14
struct dirent {
    ushort inum;
    char name[DIRSIZ];
};

struct buf;

// layout helpers
#define IPB           (BSIZE / sizeof(struct dinode))
#define BPB           (BSIZE * 8)
#define IBLOCK(i, sb)     ((i) / IPB + (sb).inodestart)
#define BBLOCK(b, sb)     ((b) / BPB + (sb).bmapstart)

// In-memory inode
struct inode {
    uint dev;
    uint inum;
    int ref;
    int valid;

    short type;
    short major;
    short minor;
    short nlink;
    uint size;
    uint addrs[NDIRECT+1];
};

void        fs_init(void);
void        iinit(int dev);
struct inode* iget(uint dev, uint inum);
struct inode* dirlookup(struct inode *dp, char *name, uint *poff);
struct inode* namei(char *path);
struct inode* nameiparent(char *path, char *name);
int         readi(struct inode *ip, int user, uint64 dst, uint off, uint n);
int         writei(struct inode *ip, int user, uint64 src, uint off, uint n);
void        ilock(struct inode *ip);
void        iunlock(struct inode *ip);
void        iunlockput(struct inode *ip);
void        iput(struct inode *ip);
struct inode* ialloc(uint dev, short type);
void        iupdate(struct inode *ip);
int         dirlink(struct inode *dp, char *name, uint inum);
int         namecmp(const char *s, const char *t);

// Logging
void        begin_op(void);
void        end_op(void);
void        log_write(struct buf *b);
void        recover_from_log(void);
void        initlog(int dev, struct superblock *sb);

// Buffer cache interface
struct buf {
    int valid;      // has data been read from disk?
    int disk;       // does disk "own" buffer?
    uint dev;
    uint blockno;
    int refcnt;
    struct buf *prev; // LRU cache list
    struct buf *next;
    struct buf *qnext; // disk queue
    uint8 data[BSIZE];
};

struct buf*  bread(uint dev, uint blockno);
void        bwrite(struct buf *b);
void        brelse(struct buf *b);
void        bzero(struct buf *b);
void        binit(void);
uint64      bcache_get_hits(void);
uint64      bcache_get_misses(void);

// Debug helpers
void        fs_get_superblock(struct superblock *out);
int         fs_count_free_blocks(void);
int         fs_count_free_inodes(void);
void        fs_debug_icache(void);

#endif
