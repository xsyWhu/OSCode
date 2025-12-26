#ifndef __USER_H__
#define __USER_H__

#include "common.h"

int fork(void);
void exit(int) __attribute__((noreturn));
int wait(int *status);
int pipe(int *);
int read(int, void *, int);
int write(int, const void *, int);
int close(int);
int kill(int);
int setpriority(int, int);
int getpriority(int);
int klog(char *, int);
int exec(const char *, char **);
int open(const char *, int);
int mknod(const char *, short, short);
int unlink(const char *);
int fstat(int fd, void *);
int link(const char *, const char *);
int mkdir(const char *);
int chdir(const char *);
int dup(int);
int getpid(void);
char* sbrk(int);
int sleep(int);
int uptime(void);
int msgget(int key);
int msgsend(int qid, const void *buf, int len);
int msgrecv(int qid, void *buf, int maxlen);
int strlen(const char *s);
void puts(const char *s);

#endif
