// Per-open file state and helpers
#ifndef __FILE_H__
#define __FILE_H__

#include "common.h"
#include "fs.h"

#define NOFILE 16
#define NFILE  100

enum file_type {
    FD_NONE = 0,
    FD_PIPE,
    FD_INODE,
    FD_DEVICE,
};

struct file {
    enum file_type type;
    int ref;
    char readable;
    char writable;
    struct inode *ip;
    uint off;
};

struct file* filealloc(void);
void         fileclose(struct file *f);
struct file* filedup(struct file *f);
int          fileread(struct file *f, uint64 addr, int n);
int          filewrite(struct file *f, uint64 addr, int n);
void         fileinit(void);

#endif
