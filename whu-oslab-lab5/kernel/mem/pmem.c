#include "mem/pmem.h"
#include "lib/lock.h"
#include "lib/print.h"
#include "riscv.h"
#include "memlayout.h"
#include "lib/string.h"

#define KERNEL_PAGES 1024   // 将原来的内核物理页数由16MB(4096页)改为4MB(1024页)
#define MAX_KERNEL_PAGES  KERNEL_PAGES
#define MAX_USER_PAGES   ((128 * 1024 * 1024 / PGSIZE) - KERNEL_PAGES)

static uint8 kern_state[MAX_KERNEL_PAGES];
static uint8 user_state[MAX_USER_PAGES];

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

    // 新增：总页数 + 每页状态
    uint32 total_pages;
    uint8* state;    // 指向一个长度为 total_pages 的数组
} alloc_region_t; 
// 内核和用户可分配的物理页分开

 static alloc_region_t kern_region, user_region;

 static inline uint32 page_index(alloc_region_t* region, uint64 page) {
    return (page - region->begin) / PGSIZE;
}

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
    kern_region.total_pages = KERNEL_PAGES;
    kern_region.state       = kern_state;
    memset(kern_region.state, 0, sizeof(kern_state)); // 初始都“未使用”

    // 初始化用户物理页区域
    user_region.begin = kern_region.end;
    user_region.end = alloc_end;
    user_region.allocable = 0;
    user_region.list_head.next = NULL;
    spinlock_init(&user_region.lk, "user_pmem_lock");
    user_region.total_pages = (user_region.end - user_region.begin) / PGSIZE;
    user_region.state       = user_state;
    memset(user_region.state, 0, sizeof(user_state));

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
    uint32 idx = page_index(region, page);

    spinlock_acquire(&region->lk);
    //duplicate free check
    if (region->state[idx] == 1) {
        spinlock_release(&region->lk);
        panic("pmem_free: double free detected (page already free)");
    }
    region->state[idx] = 1;  // 标记为空闲

    memset((void*)page, 1, PGSIZE);
    // 将页面地址转换为 page_node_t 指针
    page_node_t* node = (page_node_t*)page;
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

        uint64 page = (uint64)node;
        uint32 idx = page_index(region, page);
        if (region->state[idx] == 0) {
            // 理论上这里应该永远是 1（空闲），如果是 0 就说明 freelist 被破坏
            // 你可以 panic 或者至少打印 warning
        }
        region->state[idx] = 0;  // 标记为“已分配”
    }
    spinlock_release(&region->lk);

    if (node) {
        memset((void*)node, 5, PGSIZE);
    }

    // 如果成功分配，返回页面地址；否则返回 NULL
    return (void*)node;
}

// pmem_alloc_pages(: 分配多个物理页面)
// 返回实际分配到的页数（<= n），把每一页的物理地址写到 pages[] 里
int pmem_alloc_pages(bool in_kernel, int n, uint64 pages[]) {
    if (n <= 0) return 0;

    alloc_region_t* region = in_kernel ? &kern_region : &user_region;
    int allocated = 0;

    spinlock_acquire(&region->lk);
    while (allocated < n && region->list_head.next) {
        page_node_t* node = region->list_head.next;
        region->list_head.next = node->next;
        region->allocable--;
        pages[allocated++] = (uint64)node;

        uint64 page = (uint64)node;
        uint32 idx = page_index(region, page);
        if (region->state[idx] == 0) {
            // 理论上这里应该永远是 1（空闲），如果是 0 就说明 freelist 被破坏
            // 你可以 panic 或者至少打印 warning
        }
        region->state[idx] = 0;  // 标记为“已分配”
    }
    spinlock_release(&region->lk);

    // 填充调试模式
    for (int i = 0; i < allocated; i++) {
        memset((void*)pages[i], 5, PGSIZE);
    }
    return allocated;
}
// pmem_free_pages_count: 获取可分配页面数
uint32 pmem_free_pages_count(bool in_kernel) {
    alloc_region_t* region = in_kernel ? &kern_region : &user_region;
    spinlock_acquire(&region->lk);
    uint32 n = region->allocable;
    spinlock_release(&region->lk);
    return n;
}
// Helper function for external use
int alloc_pages(int n, bool in_kernel, uint64 pages[]) {
    return pmem_alloc_pages(in_kernel, n, pages);
}