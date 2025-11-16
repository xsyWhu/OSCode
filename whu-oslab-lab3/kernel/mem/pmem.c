#include "mem/pmem.h"
#include "lib/lock.h"
#include "lib/print.h"
#include "riscv.h"
#include "memlayout.h"
#include "lib/string.h"

#define KERNEL_PAGES 1024   // 将原来的内核物理页数由16MB(4096页)改为4MB(1024页)


typedef struct page_node { 
    struct page_node* next;
 } page_node_t;


// 许多物理页构成一个可分配的区域 
typedef struct alloc_region { 
    uint64 begin; // 起始物理地址

    uint64 end; // 终止物理地址

    spinlock_t lk; // 自旋锁(保护下面两个变量)
    
    uint32 allocable; // 可分配页面数

    page_node_t list_head; // 可分配链的链头节点 
} alloc_region_t; 
// 内核和用户可分配的物理页分开

 static alloc_region_t kern_region, user_region;


 // pmem_init: 初始化物理内存管理器
void pmem_init(void) {
    // ALLOC_BEGIN 来自链接脚本 kernel.ld 中的 'end' 符号
    // 它标志着内核代码和静态数据区的结束位置
    uint64 alloc_begin = PG_ROUND_UP((uint64)ALLOC_BEGIN);
    uint64 alloc_end = KERNEL_BASE + 128 * 1024 * 1024; // 128MB 物理内存

    // 初始化内核物理页区域
    kern_region.begin = alloc_begin;
    kern_region.end = kern_region.begin + KERNEL_PAGES * PGSIZE;
    kern_region.allocable = 0;
    kern_region.list_head.next = NULL;
    spinlock_init(&kern_region.lk, "kernel_pmem_lock");

    // 初始化用户物理页区域
    user_region.begin = kern_region.end;
    user_region.end = alloc_end;
    user_region.allocable = 0;
    user_region.list_head.next = NULL;
    spinlock_init(&user_region.lk, "user_pmem_lock");

    // 将所有可用物理页加入到对应的空闲链表中
    // 这里通过调用 pmem_free 来完成初始化
    // for (uint64 p = kern_region.begin; p < kern_region.end; p += PGSIZE) {
    //     pmem_free(p, true);
    // }

    // for (uint64 p = user_region.begin; p < user_region.end; p += PGSIZE) {
    //     pmem_free(p, false);
    // }

    // 逆序初始化，让低地址页面先被分配
    for (uint64 p = kern_region.end - PGSIZE; p >= kern_region.begin; p -= PGSIZE) {
        pmem_free(p, true);
    }
    for (uint64 p = user_region.end - PGSIZE; p >= user_region.begin; p -= PGSIZE) {
        pmem_free(p, false);
    }
}

// pmem_free: 释放一个物理页面
void pmem_free(uint64 page, bool in_kernel) {
    alloc_region_t* region = in_kernel ? &kern_region : &user_region;

    if ((page % PGSIZE) != 0) {
        panic("pmem_free: page address not aligned");
    }
    if (page < region->begin || page >= region->end) {
        panic("pmem_free: page address out of  region ");
    }
    memset((void*)page, 1, PGSIZE);

    // 将页面地址转换为 page_node_t 指针
    page_node_t* node = (page_node_t*)page;

    spinlock_acquire(&region->lk);
    // 头插法将页面插入空闲链表
    node->next = region->list_head.next;
    region->list_head.next = node;
    region->allocable++;
    spinlock_release(&region->lk);
}

// pmem_alloc: 分配一个物理页面
void* pmem_alloc(bool in_kernel) {
    alloc_region_t* region = in_kernel ? &kern_region : &user_region;
    page_node_t* node = NULL;

    spinlock_acquire(&region->lk);
    if (region->list_head.next) {
        // 从链表头取出一个节点
        node = region->list_head.next;
        region->list_head.next = node->next;
        region->allocable--;
    }
    spinlock_release(&region->lk);

    if (node) {
        memset((void*)node, 5, PGSIZE);
    }

    // 如果成功分配，返回页面地址；否则返回 NULL
    return (void*)node;
}
