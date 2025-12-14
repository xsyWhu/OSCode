#ifndef __FS_DIR_H__
#define __FS_DIR_H__

#include "fs/fs.h"

struct inode* dirlookup(struct inode *dp, char *name, uint32 *poff);
int dirlink(struct inode *dp, char *name, uint32 inum);
int namecmp(const char *s, const char *t);

struct inode* namei(char *path);
struct inode* nameiparent(char *path, char *name);

#endif
