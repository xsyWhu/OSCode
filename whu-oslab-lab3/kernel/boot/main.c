/* kernel/boot/main.c */
#include "riscv.h"
#include "lib/print.h"
#include "proc/proc.h"
#include "lib/lock.h"
#include "mem/pmem.h"
#include "lib/string.h" 
#include "mem/vmem.h"
#include "proc/cpu.h"

volatile static int started = 0;

int main()
{
    int cpuid = r_tp();
    if(cpuid == 0) {

        print_init();
        pmem_init();
        kvm_init();
        kvm_inithart();

        printf("cpu %d is booting!\n", cpuid);
        __sync_synchronize();
        // started = 1;

        pgtbl_t test_pgtbl = pmem_alloc(true);
        memset(test_pgtbl, 0, PGSIZE); 
        uint64 mem[5];
        for(int i = 0; i < 5; i++)
            mem[i] = (uint64)pmem_alloc(false);
        //Test1
        printf("\ntest-1\n\n");    
        vm_mappages(test_pgtbl, 0, mem[0], PGSIZE, PTE_R);
        vm_mappages(test_pgtbl, PGSIZE * 10, mem[1], PGSIZE, PTE_R | PTE_W);
        vm_mappages(test_pgtbl, PGSIZE * 512, mem[2], PGSIZE, PTE_R | PTE_X);
        vm_mappages(test_pgtbl, PGSIZE * 512 * 512, mem[2], PGSIZE, PTE_R | PTE_X);
        vm_mappages(test_pgtbl, VA_MAX - PGSIZE, mem[4], PGSIZE, PTE_W);
        vm_print(test_pgtbl);
        //Test2
        printf("\ntest-2\n\n");    
        //vm_mappages(test_pgtbl, 0, mem[0], PGSIZE, PTE_W);
        vm_unmappages(test_pgtbl, PGSIZE * 10, PGSIZE, false); //not free,just unmap
        vm_unmappages(test_pgtbl, PGSIZE * 512, PGSIZE, false);
        vm_print(test_pgtbl);
        //OtherTest
        // --- Test 3: Batch Allocation and Free ---
        printf("\n\n--- test-3: Batch Allocation/Free ---\n");
        int pages_to_allocate = 7;
        uint64 batch_mem[pages_to_allocate];
    
        uint32 user_free_before = pmem_free_pages_count(false);
        printf("User free pages before batch alloc: %u\n", user_free_before);

        // 1. 批量分配 7 个用户页
        int allocated = pmem_alloc_pages(false, pages_to_allocate, batch_mem);
        printf("Allocated %d pages out of %d requested.\n", allocated, pages_to_allocate);

        uint32 user_free_after = pmem_free_pages_count(false);
        printf("User free pages after batch alloc: %u (Expected: %u)\n", 
           user_free_after, user_free_before - allocated);
           
        // 2. 批量释放分配的页
        printf("Releasing %d allocated pages...\n", allocated);
        for (int i = 0; i < allocated; i++) {
            // 使用单页释放函数测试其兼容性
            pmem_free(batch_mem[i], false); 
        }

        uint32 user_free_final = pmem_free_pages_count(false);
        printf("User free pages after batch free: %u (Expected: %u)\n", 
           user_free_final, user_free_before);

        if (user_free_final == user_free_before) {
            printf("Test 3 PASSED: Free page count restored correctly.\n");
        } else {
            printf("Test 3 FAILED: Free page count mismatch.\n");
        }
        // --- Test 4: Alignment Check (Should Panic) ---
        printf("\n\n--- test-4: Alignment Check ---\n");
    
        // 尝试映射非对齐的 VA/PA
        printf("Attempting vm_mappages with unaligned VA (Should PANIC if implemented correctly)...\n");
        // 假设 PGSIZE 为 0x1000 (4096)
        // 注意: 如果您的 panic 函数不会返回，下面的代码将不会执行。
        // 如果您想继续测试，您需要注释掉或移除这行代码。
        vm_mappages(test_pgtbl, 0x1001, mem[0], PGSIZE, PTE_R); 

        // 尝试取消映射非对齐的 VA
        printf("Attempting vm_unmappages with unaligned VA (Should PANIC if implemented correctly)...\n");
        vm_unmappages(test_pgtbl, PGSIZE * 512 + 1, PGSIZE, true);
    
        // 假设注释掉了上面的 panic 代码以继续执行：
        printf("Alignment checks were skipped or passed (if panic was commented out).\n");

        // --- Test 5: Page Table Destruction ---
        printf("\n\n--- test-5: Page Table Destruction ---\n");
        
        // 1. 记录销毁前的状态
        uint32 kernel_free_before_destroy = pmem_free_pages_count(true);
        uint32 user_free_before_destroy = pmem_free_pages_count(false);
        printf("Kernel free pages before destroy: %u\n", kernel_free_before_destroy);
        printf("User free pages before destroy: %u\n", user_free_before_destroy);

        // 2. 销毁 test_pgtbl，并释放所有映射的物理页
        // 在 test-1 中，映射了 mem[0], mem[2] (两次), mem[4]
        // mem[1] 已在 test-2 中释放
        // 因此这里应该释放 mem[0], mem[2], mem[4] 三个用户页
        vm_destroy_pagetable(test_pgtbl, true); 

        // 3. 记录销毁后的状态
        uint32 kernel_free_after_destroy = pmem_free_pages_count(true);
        uint32 user_free_after_destroy = pmem_free_pages_count(false);

        // 4. 验证计数
        // 用户页应该增加 3 页 (mem[0], mem[2], mem[4])
        uint32 expected_user_increase = 3; 
        uint32 actual_user_increase = user_free_after_destroy - user_free_before_destroy;

        // 内核页应该增加 N_pt_pages 页 (所有中间页表和根页表)
        // N_pt_pages 在 test-1 和 test-2 中创建和使用
        uint32 actual_kernel_increase = kernel_free_after_destroy - kernel_free_before_destroy;
    
        printf("\nUser page change: +%u (Expected: +%u)\n", actual_user_increase, expected_user_increase);
        printf("Kernel page change (page tables freed): +%u\n", actual_kernel_increase);
        
        if (actual_user_increase == expected_user_increase && actual_kernel_increase > 0) {
            printf("Test 5 PASSED: Page table and associated memory freed correctly.\n");
        } else {
            printf("Test 5 FAILED: Free page count mismatch after destroy.\n");
        }
    } else {
        while(started == 0);
        __sync_synchronize();
        printf("cpu %d is booting!\n", cpuid);
    }
    while (1);    
}
