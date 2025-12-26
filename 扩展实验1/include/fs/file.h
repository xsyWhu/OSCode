#ifndef __FILE_H__
#define __FILE_H__

#include "common.h"
#include "lib/lock.h"
#include "stat.h"

enum filetype {
    FD_NONE = 0,
    FD_CONSOLE,
    FD_INODE,
    FD_PIPE,
};

#define O_RDONLY 0x000
#define O_WRONLY 0x001
#define O_RDWR   0x002
#define O_CREATE 0x200

struct inode;

struct pipe;

struct file {
    enum filetype type;
    int ref;
    char readable;
    char writable;
    uint64 off;
    struct inode *ip;
    struct pipe *pipe;
};

void fileinit(void);
struct file* filealloc(void);
struct file* filedup(struct file *f);
void fileclose(struct file *f);
int fileread(struct file *f, char *dst, int n);
int filewrite(struct file *f, const char *src, int n);
int filestat(struct file *f, struct stat *st);

#endif
