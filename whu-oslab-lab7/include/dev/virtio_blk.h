#ifndef __VIRTIO_BLK_H__
#define __VIRTIO_BLK_H__

#include "common.h"
#include "memlayout.h"
#include "fs/fs.h"

void virtio_disk_init(void);
void virtio_disk_rw(struct buf *b, int write);

#endif
