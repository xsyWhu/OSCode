#ifndef __VIRTIO_DISK_H__
#define __VIRTIO_DISK_H__

#include "common.h"

struct buf;

void virtio_disk_init(void);
void virtio_disk_rw(struct buf *b, int write);
void virtio_disk_intr(void);

#endif
