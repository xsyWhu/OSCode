/*
内核页表的创建 kvmmake
页表遍历 walk
建立映射 mappages
*/

#include "riscv.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "lib/string.h"
#include "lib/lock.h"
#include "memlayout.h"
#include "lib/print.h"

extern char trampoline[];

static pgtbl_t kernel_pgtbl;

extern char etext[]; 
extern char trampoline[];

pte_t* vm_getpte(pgtbl_t pgtbl, uint64 va, bool alloc)
{
    if(va >= VA_MAX) 
    {
        panic("vm_getpte: virtual address out of bound");
    }

    for (int level = 2; level > 0; level--) {
        pte_t* pte = &pgtbl[VA_TO_VPN(va, level)];

        if (*pte & PTE_V) {
           pgtbl = (pgtbl_t)PTE_TO_PA(*pte);
        } 
        else 
        {
            if (alloc) {
                pgtbl = (pgtbl_t)pmem_alloc(true); // 页表属于内核
                if (pgtbl == NULL) {
                    return NULL; // 物理内存不足
                }
                memset(pgtbl, 0, PGSIZE);
                // 在当前PTE中填入新页表的物理地址, 并设置有效位
                // 注意: 指向下一级页表的PTE, 其R/W/X权限位必须为0
                *pte = PA_TO_PTE(pgtbl) | PTE_V;
            } else {
                // 如果 alloc 为 false, 则直接返回 NULL
                return NULL;
            }
        }
    }
    return &pgtbl[VA_TO_VPN(va, 0)];
}
// vm_mappages: 在页表中创建一段虚拟地址到物理地址的映射
// - pgtbl: 目标页表
// - va:    虚拟地址起始
// - pa:    物理地址起始
// - len:   映射长度 (必须是 PGSIZE 的整数倍)
// - perm:  权限位 (PTE_R, PTE_W, PTE_X)
void vm_mappages(pgtbl_t pgtbl, uint64 va, uint64 pa, uint64 len, int perm) {
  if((va % PGSIZE) != 0){
    panic("mappages: va not aligned");}

  if((len % PGSIZE) != 0){
    panic("mappages: size not aligned");}

  if(len == 0){
    panic("mappages: size");}
  
    uint64 current_va = va;
    uint64 end_va = va + len;
    uint64 current_pa = pa;
    pte_t* pte;

    while (current_va < end_va) {
        // 1. 获取当前虚拟地址对应的最低级PTE的地址
        pte = vm_getpte(pgtbl, current_va, true);

        if (pte == NULL) {
            panic("vm_mappages: pmem_alloc failed");
        }
        if (*pte & PTE_V) {
            // 如果该PTE已存在映射, 这是不允许的
            panic("vm_mappages: remap");
        }

        // 2. 设置PTE, 包含物理页号, 权限位和有效位
        *pte = PA_TO_PTE(current_pa) | perm | PTE_V;

        // 3. 移动到下一个页面
        current_va += PGSIZE;
        current_pa += PGSIZE;
    }
}

// vm_unmappages: 在页表中解除一段地址映射
// - pgtbl:  目标页表
// - va:     虚拟地址起始
// - len:    映射长度
// - freeit: 是否释放映射对应的物理页面
void vm_unmappages(pgtbl_t pgtbl, uint64 va, uint64 len, bool freeit) {
    if ((va % PGSIZE) != 0) {
        panic("vm_unmappages: va not aligned");
    }
    if ((len % PGSIZE) != 0) {
        panic("vm_unmappages: length not aligned");
    }
    uint64 current_va = va;
    uint64 end_va = va + len;
    pte_t* pte;

    while (current_va < end_va) {
        // 1. 获取当前虚拟地址对应的最低级PTE的地址 (不分配新页表)
        pte = vm_getpte(pgtbl, current_va, false);

        if (pte != NULL && (*pte & PTE_V)) {
            // 2. 如果映射存在
            if (freeit) {
                // 3. 如果需要, 释放其对应的物理页
                uint64 pa = PTE_TO_PA(*pte);
                // 根据物理地址 pa 判断它属于哪个区域
                if (pa >= PG_ROUND_UP((uint64)ALLOC_BEGIN) && pa < PG_ROUND_UP((uint64)ALLOC_BEGIN) + 1024 * PGSIZE) {
                    // 物理地址在内核区
                    pmem_free(pa, true);
                } else if (pa >= PG_ROUND_UP((uint64)ALLOC_BEGIN) + 1024 * PGSIZE && pa < PHYSTOP) {
                    // 物理地址在用户区
                    pmem_free(pa, false);
                } else {
                    // 物理地址不在任何一个可管理区域内, 这是一个严重错误   
                    panic("vm_unmappages: pa out of any known region");
                }
            }
            // 4. 将PTE清零, 使映射失效
            *pte = 0;
        }

        // 5. 移动到下一个页面
        current_va += PGSIZE;
    }
}


void kvm_init() {
    // 1. 为顶级页表分配一个物理页
    kernel_pgtbl = (pgtbl_t)pmem_alloc(true);
    if (kernel_pgtbl == NULL) {
        panic("kvm_init: failed to allocate root page table");
    }
    memset(kernel_pgtbl, 0, PGSIZE);

    // 2. 映射硬件设备: UART
    // 将 UART 寄存器的物理地址映射到等值的虚拟地址
    vm_mappages(kernel_pgtbl, UART_BASE, UART_BASE, PGSIZE, PTE_R | PTE_W);
    

    // 3. 映射硬件设备: PLIC
    // 将 PLIC 寄存器的物理地址区域映射到等值的虚拟地址
    vm_mappages(kernel_pgtbl, PLIC_BASE, PLIC_BASE, 0x400000, PTE_R | PTE_W);
    
    // 4. 映射内核代码段 (.text)
    // 权限为 可读 | 可执行 (R-X)
    vm_mappages(kernel_pgtbl, KERNEL_BASE, KERNEL_BASE, (uint64)etext - KERNEL_BASE, PTE_R | PTE_X);

    // 5. 映射内核数据段和剩余的所有物理内存
    // 权限为 可读 | 可写 (RW-)
    uint64 pa_for_data = (uint64)etext;
    vm_mappages(kernel_pgtbl, pa_for_data, pa_for_data, PHYSTOP - pa_for_data, PTE_R | PTE_W);

    // 6. trampoline 映射到固定地址，供用户态返回使用
    vm_mappages(kernel_pgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}

// kvm_inithart: 在每个CPU核上启用分页
void kvm_inithart() {

    sfence_vma();
    // 1. 将内核页表的物理地址写入 satp 寄存器, 正式启用分页
    // MAKE_SATP 宏会将页表地址转换为 satp 需要的格式
    w_satp(MAKE_SATP(kernel_pgtbl));

    // 2. 刷新 TLB (Translation Lookaside Buffer)
    // 确保旧的/无效的地址翻译被清除
    sfence_vma();
}

// 计算虚拟地址范围
static uint64 calc_va_start(int level2_idx, int level1_idx, int level0_idx) {
    return ((uint64)level2_idx << 30) | ((uint64)level1_idx << 21) | ((uint64)level0_idx << 12);
}

// 打印权限的辅助函数
static void print_permissions(pte_t pte) {
    if (pte & PTE_R) printf("r"); else printf("-");
    if (pte & PTE_W) printf("w"); else printf("-");
    if (pte & PTE_X) printf("x"); else printf("-");
    if (pte & PTE_U) printf("u"); else printf("-");
}

// 判断地址范围的辅助函数
static const char* get_region_name(uint64 va, uint64 pa) {
    // 根据虚拟地址判断区域
    if (va == UART_BASE) return "UART";
    if (va >= PLIC_BASE && va < PLIC_BASE + 0x400000) return "PLIC";
    if (va >= KERNEL_BASE && va < (uint64)etext) return "KERNEL_TEXT";
    if (va >= (uint64)etext && va < PHYSTOP) return "KERNEL_DATA";
    //if (va == TRAMPOLINE) return "TRAMPOLINE";
    return "UNKNOWN";
}

static void print_hex_padded(uint64 value) {
    printf("0x");
    // 从高位开始打印，确保16位
    int printed = 0;
    for (int i = 60; i >= 0; i -= 4) {
        uint64 digit = (value >> i) & 0xF;
        if (digit != 0 || printed || i == 0) {
            printf("%llx", digit);
            printed = 1;
        } else if (printed == 0) {
            printf("0");  // 前导零
        }
    }
}

static void vm_print_recursive_new(pgtbl_t pgtbl, int level, int indices[3]) {
    for (int i = 0; i < 512; i++) {
        pte_t pte = pgtbl[i];
        
        if (!(pte & PTE_V)) continue;
        
        indices[level] = i;
        
        if (PTE_CHECK(pte) && level > 0) {
            // 非叶子节点，继续递归
            uint64 child_pa = PTE_TO_PA(pte);
            vm_print_recursive_new((pgtbl_t)child_pa, level - 1, indices);
        } else {
            // 叶子节点，打印映射信息
            uint64 va = calc_va_start(indices[2], indices[1], indices[0]);
            uint64 pa = PTE_TO_PA(pte);
            
            printf("  VA: ");
            print_hex_padded(va);
            printf(" -> PA: ");
            print_hex_padded(pa);
            printf(" | ");
            print_permissions(pte);
            printf(" | %s\n", get_region_name(va, pa));
        }
    }
}

void vm_print(pgtbl_t pgtbl) {
    printf("\n=== KERNEL PAGE TABLE MAPPINGS ===\n");
    printf("Root Page Table: %p\n\n", pgtbl);
    printf("  Virtual Address    ->  Physical Address   | Perm | Region\n");
    printf("  ----------------------------------------------------------\n");
    
    int indices[3] = {0, 0, 0};
    vm_print_recursive_new(pgtbl, 2, indices);
    
    printf("  ----------------------------------------------------------\n");
    printf("  Legend: r=read, w=write, x=execute, u=user\n");
    printf("=== END PAGE TABLE ===\n\n");
}

// 递归释放页表树
static void vm_freewalk(pgtbl_t pgtbl, int level, bool free_leaf) {
    for (int i = 0; i < 512; i++) {
        pte_t pte = pgtbl[i];
        if (!(pte & PTE_V)) {
            continue;
        }

        if (PTE_CHECK(pte) && level > 0) {
            // 中间页表：递归释放子页表
            uint64 child_pa = PTE_TO_PA(pte);
            vm_freewalk((pgtbl_t)child_pa, level - 1, free_leaf);

            // 子页表本身是通过 pmem_alloc(true) 分配的
            pmem_free(child_pa, true);

        } else {
            // 叶子 PTE（level==0 或 R/W/X 非 0）
            if (free_leaf) {
                uint64 pa = PTE_TO_PA(pte);

                // 和 vm_unmappages 同一套逻辑：内核区 / 用户区
                uint64 kbegin = PG_ROUND_UP((uint64)ALLOC_BEGIN);
                uint64 ksplit = kbegin + 1024 * PGSIZE; // 或硬编码 1024*PGSIZE

                if (pa >= kbegin && pa < ksplit) {
                    pmem_free(pa, true);
                } else if (pa >= ksplit && pa < PHYSTOP) {
                    pmem_free(pa, false);
                } else {
                    panic("vm_freewalk: leaf pa out of known region");
                }
            }
        }

        // 无论如何，最后都把 PTE 清零
        pgtbl[i] = 0;
    }
}
void vm_destroy_pagetable(pgtbl_t root, bool free_leaf) {
    if (root == NULL) {
        return;
    }
    vm_freewalk(root, 2, free_leaf);
    // 最后释放根页表本身
    pmem_free((uint64)root, true);
    printf("vm_destroy_pagetable: page table destroyed\n");
}

static uint64 walkaddr(pgtbl_t pagetable, uint64 va) {
    if (va >= MAXVA)
        return 0;
    pte_t *pte = vm_getpte(pagetable, va, false);
    if (pte == NULL)
        return 0;
    if ((*pte & PTE_V) == 0)
        return 0;
    if ((*pte & PTE_U) == 0)
        return 0;
    uint64 pa = PTE_TO_PA(*pte);
    return pa + (va & (PGSIZE - 1));
}

pgtbl_t uvm_create(void) {
    pgtbl_t pagetable = (pgtbl_t)pmem_alloc(true);
    if (!pagetable)
        return NULL;
    memset(pagetable, 0, PGSIZE);
    return pagetable;
}

uint64 uvm_alloc(pgtbl_t pagetable, uint64 oldsz, uint64 newsz) {
    if (newsz < oldsz)
        return oldsz;

    oldsz = PG_ROUND_UP(oldsz);
    for (uint64 a = oldsz; a < newsz; a += PGSIZE) {
        void *mem = pmem_alloc(false);
        if (!mem) {
            uvm_free(pagetable, a);
            return 0;
        }
        memset(mem, 0, PGSIZE);
        vm_mappages(pagetable, a, (uint64)mem, PGSIZE, PTE_R | PTE_W | PTE_X | PTE_U);
    }
    return newsz;
}

void uvm_free(pgtbl_t pagetable, uint64 sz) {
    if (pagetable == NULL)
        return;
    if (sz > 0) {
        uint64 a = PG_ROUND_UP(sz);
        for (; a > 0; ) {
            a -= PGSIZE;
            pte_t *pte = vm_getpte(pagetable, a, false);
            if (pte && (*pte & PTE_V)) {
                uint64 pa = PTE_TO_PA(*pte);
                pmem_free(pa, false);
                *pte = 0;
            }
        }
    }
    vm_destroy_pagetable(pagetable, false);
}

int uvm_load(pgtbl_t pagetable, uint64 va, const uint8 *src, uint64 len) {
    uint64 offset = 0;
    while (offset < len) {
        uint64 pa = walkaddr(pagetable, va + offset);
        if (pa == 0) {
            printf("uvm_load: missing va=%p offset=%lu len=%lu\n", va + offset, offset, len);
            return -1;
        }
        uint64 n = PGSIZE - ((va + offset) & (PGSIZE - 1));
        if (n > len - offset)
            n = len - offset;
        memmove((void*)(pa), src + offset, n);
        offset += n;
    }
    return 0;
}

int copyout(pgtbl_t pagetable, uint64 dstva, const void *src, uint64 len) {
    uint64 offset = 0;
    const uint8 *s = (const uint8*)src;
    while (offset < len) {
        uint64 va = dstva + offset;
        uint64 pa = walkaddr(pagetable, va);
        if (pa == 0)
            return -1;
        uint64 n = PGSIZE - (va & (PGSIZE - 1));
        if (n > len - offset)
            n = len - offset;
        memmove((void*)(pa + (va & (PGSIZE - 1))), s + offset, n);
        offset += n;
    }
    return 0;
}

int copyin(pgtbl_t pagetable, void *dst, uint64 srcva, uint64 len) {
    uint64 offset = 0;
    uint8 *d = (uint8*)dst;
    while (offset < len) {
        uint64 va = srcva + offset;
        uint64 pa = walkaddr(pagetable, va);
        if (pa == 0)
            return -1;
        uint64 n = PGSIZE - (va & (PGSIZE - 1));
        if (n > len - offset)
            n = len - offset;
        memmove(d + offset, (void*)(pa + (va & (PGSIZE - 1))), n);
        offset += n;
    }
    return 0;
}

int copyinstr(pgtbl_t pagetable, char *dst, uint64 srcva, uint64 max) {
    uint64 offset = 0;
    while (offset < max) {
        uint64 va = srcva + offset;
        uint64 pa = walkaddr(pagetable, va);
        if (pa == 0)
            return -1;
        uint64 n = PGSIZE - (va & (PGSIZE - 1));
        for (uint64 i = 0; i < n && offset < max; i++, offset++) {
            char c = *(char*)(pa + ((va & (PGSIZE - 1)) + i));
            dst[offset] = c;
            if (c == '\0')
                return 0;
        }
    }
    if (max > 0)
        dst[max - 1] = '\0';
    return -1;
}

pgtbl_t kernel_pagetable_get(void)
{
    return kernel_pgtbl;
}
