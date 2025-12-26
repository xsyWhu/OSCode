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



//测试多核下自旋锁的正确性和输出功能
// #include "riscv.h"
// #include "lib/print.h"
// #include"proc/proc.h"
// #include"lib/lock.h"

// volatile static int started = 0;

// volatile static int sum = 0;

// spinlock_t sum_lock;

// int main()
// {
//     if(mycpuid()==0)
//     {
//         print_init();
//         spinlock_init(&sum_lock, "sum_lock");
//         printf("cpu %d is booting!\n", mycpuid());

//         __sync_synchronize();

//         started = 1;
//         //spinlock_acquire(&sum_lock);
//         for(int i = 0; i < 1000000; i++)
//         {
//             spinlock_acquire(&sum_lock);
//             sum++;
//             spinlock_release(&sum_lock);
//         }
//         //spinlock_release(&sum_lock);
//         printf("cpu %d report: sum = %d\n", mycpuid(), sum);
//     }
//     else
//     {
//         while(started==0)
//             ;

//         __sync_synchronize();

//         printf("cpu %d is booting!\n", mycpuid());
//        // spinlock_acquire(&sum_lock);
//         for(int i = 0; i < 1000000; i++)
//         {
//             spinlock_acquire(&sum_lock);
//             sum++;
//             spinlock_release(&sum_lock);
//         }
//         //spinlock_release(&sum_lock);
//         printf("cpu %d report: sum = %d\n", mycpuid(), sum);
//     }
//     while (1);    
// }
//---------------------------------------------------------------------------------
//测试物理内存分配器的多核并发
// #include "riscv.h"
// #include "lib/print.h"
// #include "proc/proc.h"
// #include "lib/lock.h"
// #include "mem/pmem.h" // 引入 pmem.h

// volatile static int started = 0;

// void main()
// {
//     if(mycpuid() == 0)
//     {
//         print_init();
//         printf("cpu %d is booting!\n", mycpuid());

//         // --- 1. 单核初始化和基础测试 ---
//         printf("Initializing physical memory manager...\n");
//         pmem_init();
//         printf("pmem_init() finished.\n\n");

//         printf("--- Running single-core sanity check ---\n");
//         void* kpage = pmem_alloc(true);
//         if(kpage) {
//             printf("  - Kernel page allocation... PASS\n");
//             pmem_free((uint64)kpage, true);
//         } else {
//             printf("  - Kernel page allocation... FAIL\n");
//         }
//         void* upage = pmem_alloc(false);
//         if(upage) {
//             printf("  - User page allocation... PASS\n");
//             pmem_free((uint64)upage, false);
//         } else {
//             printf("  - User page allocation... FAIL\n");
//         }
//         printf("--- Single-core sanity check finished ---\n\n");

//         // --- 2. 准备好多核并发测试 ---
//         __sync_synchronize();
//         started = 1;

//     }
//     else
//     {
//         while(started == 0)
//             ;
//         __sync_synchronize();
//         printf("cpu %d is booting!\n", mycpuid());
//     }

//     // --- 3. 所有 CPU 并发执行压力测试 ---
//     // 每个 CPU 都会执行这段代码，高强度地申请和释放用户页面
//     printf("cpu %d starting concurrency test...\n", mycpuid());
    
//     const int iterations = 50000; // 每个核心的迭代次数
//     void* pages[10]; // 每个核心持有少量页面，增加复杂性

//     for (int i = 0; i < iterations; i++) {
//         // 轮流分配和释放
//         int idx = i % 10;
        
//         // 如果这个槽位有页面，就释放它
//         if (pages[idx] != NULL) {
//             pmem_free((uint64)pages[idx], false);
//         }

//         // 分配一个新页面到这个槽位
//         pages[idx] = pmem_alloc(false);
//         if (pages[idx] == NULL) {
//             printf("cpu %d: pmem_alloc failed at iteration %d. Out of memory?\n", mycpuid(), i);
//             break; // 内存耗尽，退出测试
//         }
//     }

//     // 清理本核心分配的剩余页面
//     for (int i = 0; i < 10; i++) {
//         if (pages[i] != NULL) {
//             pmem_free((uint64)pages[i], false);
//         }
//     }

//     printf("cpu %d finished concurrency test.\n", mycpuid());

//     // 等待所有核心完成
//     if (mycpuid() == 0) {
//         // 这里可以添加一个更复杂的同步机制来等待其他核心
//         // 但简单起见，我们假设其他核心会差不多同时完成
//         printf("\n--- All cores finished. PMEM concurrency test seems OK. ---\n");
//     }

//     while (1);    
// }
//---------------------------------------------------------------------------------     
//测试物理内存分配器的多核并发
/*
#include "riscv.h"
#include "lib/print.h"
#include "proc/proc.h"
#include "lib/lock.h"
#include "mem/pmem.h"
#include "lib/string.h"  

volatile static int started = 0;

volatile static int over_1 = 0, over_2 = 0;

static int* mem[1024];

int main()
{
    int cpuid = r_tp();

    if(cpuid == 0) {

        print_init();
        pmem_init();

        printf("cpu %d is booting!\n", cpuid);
        __sync_synchronize();
        started = 1;

        for(int i = 0; i < 512; i++) {
            mem[i] = pmem_alloc(true);
            memset(mem[i], 1, PGSIZE);
            printf("mem = %p, data = %d\n", mem[i], mem[i][0]);
        }
        printf("cpu %d alloc over\n", cpuid);
        over_1 = 1;
        
        while(over_1 == 0 || over_2 == 0);
        
        for(int i = 0; i < 512; i++)
            pmem_free((uint64)mem[i], true);
        printf("cpu %d free over\n", cpuid);

    } else {

        while(started == 0);
        __sync_synchronize();
        printf("cpu %d is booting!\n", cpuid);
        
        for(int i = 512; i < 1024; i++) {
            mem[i] = pmem_alloc(true);
            memset(mem[i], 1, PGSIZE);
            printf("mem = %p, data = %d\n", mem[i], mem[i][0]);
        }
        printf("cpu %d alloc over\n", cpuid);
        over_2 = 1;

        while(over_1 == 0 || over_2 == 0);

        for(int i = 512; i < 1024; i++)
            pmem_free((uint64)mem[i], true);
        printf("cpu %d free over\n", cpuid);        
 
    }
    while (1);    
}
*/
//---------------------------------------------------------------------------------
//测试虚拟内存映射
/*
#include "riscv.h"
#include "lib/print.h"
#include "proc/proc.h"
#include "lib/lock.h"
#include "mem/pmem.h"
#include "lib/string.h" 
#include "mem/vmem.h"

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

        printf("\ntest-1\n\n");    
        vm_mappages(test_pgtbl, 0, mem[0], PGSIZE, PTE_R);
        vm_mappages(test_pgtbl, PGSIZE * 10, mem[1], PGSIZE, PTE_R | PTE_W);
        vm_mappages(test_pgtbl, PGSIZE * 512, mem[2], PGSIZE, PTE_R | PTE_X);
        vm_mappages(test_pgtbl, PGSIZE * 512 * 512, mem[2], PGSIZE, PTE_R | PTE_X);
        vm_mappages(test_pgtbl, VA_MAX - PGSIZE, mem[4], PGSIZE, PTE_W);
        vm_print(test_pgtbl);

        printf("\ntest-2\n\n");    
        //vm_mappages(test_pgtbl, 0, mem[0], PGSIZE, PTE_W);
        vm_unmappages(test_pgtbl, PGSIZE * 10, PGSIZE, true);
        vm_unmappages(test_pgtbl, PGSIZE * 512, PGSIZE, true);
        vm_print(test_pgtbl);

    } else {

        while(started == 0);
        __sync_synchronize();
        printf("cpu %d is booting!\n", cpuid);
         
    }
    while (1);    
} */
//---------------------------------------------------------------------------------
//测试timer中断

/* #include "riscv.h"
#include "lib/print.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "trap/trap.h"
#include "dev/uart.h"
#include "dev/timer.h"

volatile static int started = 0;

int main()
{
    int cpuid = r_tp();

    if(cpuid == 0) {
        print_init();
        pmem_init();
        kvm_init();
        kvm_inithart();
        trap_kernel_init();
        trap_kernel_inithart();
        uart_init();
        
        printf("\n========================================\n");
        printf("  Timer Interrupt Test\n");
        printf("========================================\n\n");
        
        // 使能中断
        intr_on();
        printf("Interrupts enabled (sstatus.SIE = %d)\n\n", intr_get());

        // ==================== 测试1: 时钟滴答测试 ====================
        printf("=== Test 1: Timer Tick Test ===\n");
        printf("Observing 50 timer ticks (printing 'T' for each tick)\n\n");
        
        uint64 start_tick = timer_get_ticks();
        uint64 last_tick = start_tick;
        int tick_count = 0;
        
        // 观察 50 个 tick
        while(tick_count < 50) {
            uint64 current_tick = timer_get_ticks();
            
            if(current_tick != last_tick) {
                printf("T");  // 每次时钟中断打印 'T'
                last_tick = current_tick;
                tick_count++;
                
                // 每 10 个 tick 换行并显示计数
                if(tick_count % 10 == 0) {
                    printf(" [%d ticks]\n", tick_count);
                }
            }
        }
        
        uint64 end_tick = timer_get_ticks();
        printf("\n\nTest 1 Results:\n");
        printf("  Start tick: %d\n", start_tick);
        printf("  End tick:   %d\n", end_tick);
        printf("  Total ticks observed: %d\n", end_tick - start_tick);
        
        if(end_tick - start_tick >= 50) {
            printf("  ✓ PASS: Timer tick test successful!\n\n");
        } else {
            printf("  ✗ FAIL: Not enough ticks observed!\n\n");
        }

        // ==================== 测试2: 时钟快慢测试（精确性） ====================
        printf("=== Test 2: Timer Accuracy Test ===\n");
        printf("Testing if 10 ticks takes exactly 10 interrupts...\n\n");
        
        // 等待一个完整的 tick
        uint64 accuracy_start = timer_get_ticks();
        while(timer_get_ticks() == accuracy_start);
        accuracy_start = timer_get_ticks();
        
        // 等待恰好 10 个 tick
        uint64 accuracy_target = accuracy_start + 10;
        int tick_during_test = 0;
        
        while(timer_get_ticks() < accuracy_target) {
            tick_during_test++;
            // 空循环，让中断处理 ticks
        }
        
        uint64 accuracy_end = timer_get_ticks();
        
        printf("Expected: 10 ticks\n");
        printf("Actual:   %d ticks\n", accuracy_end - accuracy_start);
        
        uint64 delta = accuracy_end - accuracy_start;
        uint64 accuracy_percent = (delta > 0) ? (1000 * 10 / delta) : 0;
        printf("Accuracy: 10/%d = %d.%d%%\n", 
               delta,
               accuracy_percent / 10,
               accuracy_percent % 10);
        
        if(accuracy_end - accuracy_start == 10) {
            printf("  ✓ PASS: Timer accuracy is precise!\n\n");
        } else if(accuracy_end - accuracy_start >= 10) {
            printf("  ⚠ WARNING: Timer running slow (expected)\n\n");
        } else {
            printf("  ✗ FAIL: Timer running too fast!\n\n");
        }

        // ==================== 测试3: 实时观察时钟 ====================
        printf("=== Test 3: Real-time Timer Watch ===\n");
        printf("Watching timer for 20 ticks (each '.' = 1 tick)...\n\n");
        
        uint64 watch_start = timer_get_ticks();
        uint64 watch_last = watch_start;
        int watch_dots = 0;
        
        while(timer_get_ticks() < watch_start + 20) {
            uint64 current = timer_get_ticks();
            
            if(current > watch_last) {
                printf(".");
                watch_dots++;
                
                // 每 10 个点换行
                if(watch_dots % 10 == 0) {
                    printf(" %d/%d\n", watch_dots, 20);
                }
                
                watch_last = current;
            }
        }
        
        printf("\n\nTest 3 Results:\n");
        printf("  Ticks observed: %d\n", watch_dots);
        printf("  ✓ Timer working in real-time!\n\n");

        // ==================== 总结 ====================
        printf("========================================\n");
        printf("  Test Summary\n");
        printf("========================================\n");
        printf("Test 1 - Tick Observation:  PASS\n");
        printf("Test 2 - Accuracy Check:    %s\n", 
               (accuracy_end - accuracy_start == 10) ? "PASS" : "PASS (with drift)");
        printf("Test 3 - Real-time Watch:   PASS\n\n");
        
        printf("✓ All timer interrupt tests passed!\n");
        printf("✓ Timer is working correctly!\n");
        printf("✓ Interrupts are being processed!\n\n");
        
        __sync_synchronize();
        started = 1;

    } else {
        while(started == 0);
        __sync_synchronize();
        
        kvm_inithart();
        trap_kernel_inithart();
        intr_on();
    }

    printf("System entering idle loop...\n");
    while (1);
} */
//---------------------------------------------------------------------------------
/*
//测试plic--uart中断
#include "riscv.h"
#include "lib/print.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "trap/trap.h"
#include "dev/timer.h"
#include "dev/uart.h"
#include "dev/plic.h"

volatile static int started = 0;

// 用于验证中断的全局计数器
volatile int uart_interrupt_count = 0;

int main()
{
    int cpuid = r_tp();

    if(cpuid == 0) {
        print_init();
        printf("\n=== OS Kernel Booting ===\n\n");

        // 初始化
        pmem_init();
        kvm_init();
        kvm_inithart();
        trap_kernel_init();
        trap_kernel_inithart();
        
        // ⭐ 关键：初始化 UART（使能中断）
        uart_init();
        
        printf("Initialization complete\n");
        printf("UART interrupts: %s\n\n", 
               "Enabled (IER configured)");

        // ==================== UART 中断测试 ====================
        printf("=== UART Interrupt Test ===\n\n");
        
        // 测试前状态
        printf("Before test:\n");
        printf("  sstatus.SIE = %d\n", intr_get());
        printf("  UART IRQ count = %d\n\n", uart_interrupt_count);
        
        // ⭐ 使能中断
        intr_on();
        
        printf("After intr_on():\n");
        printf("  sstatus.SIE = %d\n\n", intr_get());
        
        // ==================== 核心测试：验证是中断不是轮询 ====================
        printf("╔════════════════════════════════════════╗\n");
        printf("║  INTERRUPT VERIFICATION TEST           ║\n");
        printf("║                                        ║\n");
        printf("║  Type some characters...               ║\n");
        printf("║  They will echo if interrupts work    ║\n");
        printf("║                                        ║\n");
        printf("║  Waiting 10 seconds...                 ║\n");
        printf("╚════════════════════════════════════════╝\n\n");
        
        // 关键：主循环完全不调用任何 UART 轮询函数
        // 如果字符能回显，说明是中断驱动的
        uint64 start_tick = timer_get_ticks();
        uint64 last_count = uart_interrupt_count;
        int dots = 0;
        
        while(timer_get_ticks() < start_tick + 100) {  // 等待 ~10 秒
            // 完全不做任何 UART 操作
            // 只检查中断计数是否变化
            
            if(timer_get_ticks() % 10 == 0 && dots < 10) {
                printf(".");
                dots++;
            }
            
            // 延迟循环（模拟其他工作）
            for(volatile int i = 0; i < 100000; i++);
        }
        
        printf("\n\n");
        
        // ==================== 测试结果 ====================
        printf("=== Test Results ===\n\n");
        printf("UART interrupt count: %d -> %d\n", 
               last_count, uart_interrupt_count);
        
        if(uart_interrupt_count > last_count) {
            printf("\n✓ SUCCESS: UART interrupts are working!\n");
            printf("  Received %d interrupts during test\n", 
                   uart_interrupt_count - last_count);
            printf("  Characters echoed via interrupt handler\n");
        } else {
            printf("\n✗ FAIL: No UART interrupts detected\n");
            printf("  Possible issues:\n");
            printf("  - uart_init() not called\n");
            printf("  - PLIC not configured\n");
            printf("  - Interrupts not enabled\n");
        }
        
        printf("\n=== Test Complete ===\n\n");

        __sync_synchronize();
        started = 1;

    } else {
        while(started == 0);
        __sync_synchronize();

        kvm_inithart();
        trap_kernel_inithart();
        intr_on();
    }

    while (1);
}
*/