#ifndef __PMEM_H__
#define __PMEM_H__

#include "common.h"

// 来自kernel.ld
extern char KERNEL_DATA[];
extern char ALLOC_BEGIN[];
//extern char ALLOC_END[];

void  pmem_init(void);
void* pmem_alloc(bool in_kernel);
void  pmem_free(uint64 page, bool in_kernel);
int   pmem_incref(uint64 page);
int   pmem_decref(uint64 page);
int   pmem_refcount(uint64 page);
int pmem_alloc_pages(bool in_kernel, int n, uint64 pages[]);
uint32 pmem_free_pages_count(bool in_kernel);
// 分配多个物理页面，包装函数
int alloc_pages(int n, bool in_kernel, uint64 pages[]);
#endif
