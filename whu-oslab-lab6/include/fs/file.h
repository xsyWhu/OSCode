#ifndef __FILE_H__
#define __FILE_H__

#include "common.h"
#include "lib/lock.h"

enum filetype {
    FD_NONE = 0,
    FD_CONSOLE,
};

#define O_RDONLY 0x000
#define O_WRONLY 0x001
#define O_RDWR   0x002
#define O_CREATE 0x200

struct file {
    enum filetype type;
    int ref;
    char readable;
    char writable;
};

void fileinit(void);
struct file* filealloc(void);
struct file* filedup(struct file *f);
void fileclose(struct file *f);
int fileread(struct file *f, char *dst, int n);
int filewrite(struct file *f, const char *src, int n);

#endif
