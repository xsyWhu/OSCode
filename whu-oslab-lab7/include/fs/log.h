#ifndef __FS_LOG_H__
#define __FS_LOG_H__

#include "common.h"

struct buf;
struct superblock;

void log_init(int dev, const struct superblock *sb);
void begin_op(void);
void end_op(void);
void log_write(struct buf *b);

#endif
