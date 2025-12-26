#ifndef __FS_PIPE_H__
#define __FS_PIPE_H__

#include "common.h"
#include "lib/lock.h"

struct file;

struct pipe;

int pipealloc(struct file **f0, struct file **f1);
void pipeclose(struct pipe *pi, int writable);
int pipewrite(struct pipe *pi, const char *addr, int n);
int piperead(struct pipe *pi, char *addr, int n);

#endif
