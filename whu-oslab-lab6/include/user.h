#ifndef __USER_H__
#define __USER_H__

#include "common.h"

int fork(void);
void exit(int) __attribute__((noreturn));
int wait(int *status);
int read(int, void *, int);
int write(int, const void *, int);
int close(int);
int kill(int);
int getpid(void);
int exec(const char *, char *const *);
int open(const char *, int);
int sbrk(int);

#endif
