#ifndef __KLOG_H__
#define __KLOG_H__

#include "common.h"
#include "lib/lock.h"

#define LOG_BUF_SIZE 4096

enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
};

struct klog_buffer {
    spinlock_t lock;
    char buf[LOG_BUF_SIZE];
    int read_pos;
    int write_pos;
    int level;
    uint64 dropped;
};

extern struct klog_buffer klog_buf;

void klog_init(void);
void klog(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int klog_read(char *dst, int n);
void klog_set_level(int level);

#endif
