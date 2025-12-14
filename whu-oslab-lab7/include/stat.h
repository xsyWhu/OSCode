#ifndef __LAB7_STAT_H__
#define __LAB7_STAT_H__

#include "common.h"

struct stat {
    short type;
    short major;
    short minor;
    short nlink;
    uint32 dev;
    uint32 inum;
    uint64 size;
};

#endif
