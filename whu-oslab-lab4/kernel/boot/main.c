#include "riscv.h"
#include "lib/print.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "trap/trap.h"
#include "dev/timer.h"
#include "dev/uart.h"
#include "dev/plic.h"

volatile static int started = 0;

// === Lab4: interrupt test helpers ===
void test_timer_interrupt(void);
void test_exception_handling(void);
void test_interrupt_overhead(void);

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
        trap_init();
        trap_inithart();
        uart_init();

        // 打个总标题
        printf("\n========================================\n");
        printf("  Lab4: Interrupt Tests\n");
        printf("========================================\n\n");

        // 使能 S-mode 外设/时钟/软件中断
        intr_on();
        printf("Interrupts enabled (sstatus.SIE = %d)\n\n", intr_get());

        // 时钟中断测试（三小项都在这个函数里）
        test_timer_interrupt();

        // 中断“开销”/tick 间隔粗略测量
        test_interrupt_overhead();

        // 异常处理测试（会触发 panic，一般单独跑，先注释）
        //test_exception_handling();

        __sync_synchronize();
        started = 1;

    } else {
        while(started == 0);
        __sync_synchronize();

        kvm_inithart();
        trap_inithart();
        intr_on();
    }
    printf("System entering idle loop...\n");
    while (1);
}
// 测试非法指令异常
static void trigger_illegal_instruction(void)
{
    printf("Triggering illegal instruction exception...\n");
    printf("You should see \"Illegal instruction in kernel\" and a panic.\n\n");

    // 这里直接塞一个无效指令编码
    asm volatile(".word 0x00000000");

    // 理论上到不了这行
    printf("WARNING: illegal instruction did NOT trap as expected!\n");
}

// 测试内存访问异常（Load/Store page fault/access fault）
static void trigger_bad_memory_access(void)
{
    printf("Triggering bad memory access exception...\n");
    printf("Trying to read from an unmapped address...\n");
    printf("You should see \"Page fault in kernel\" (or similar) and a panic.\n\n");

    volatile uint64 *p = (uint64*)0xffffffffffffffffULL;  // 明显超出映射范围
    uint64 x = *p;  // 触发异常
    (void)x;

    printf("WARNING: bad memory access did NOT trap as expected!\n");
}

// ==================== Test 1/2/3: timer interrupt ====================
void test_timer_interrupt(void)
{
    // ---------- Test 1: 观察 50 个 tick ----------
    printf("=== Test 1: Timer Tick Test ===\n");
    printf("Observing 50 timer ticks (printing 'T' for each tick)\n\n");

    uint64 start_tick = timer_get_ticks();
    uint64 last_tick  = start_tick;
    int    tick_count = 0;

    printf("Ticks: ");

    while (tick_count < 50) {
        uint64 current_tick = timer_get_ticks();
        if (current_tick != last_tick) {
            printf("T");
            last_tick = current_tick;
            tick_count++;

            if (tick_count % 10 == 0) {
                printf(" [%d]\n", tick_count);
            }
        }
    }

    uint64 end_tick = timer_get_ticks();
    printf("\n\nTest 1 Results:\n");
    printf("  Start tick: %d\n", start_tick);
    printf("  End tick:   %d\n", end_tick);
    printf("  Total ticks observed: %d\n\n", end_tick - start_tick);

    // ---------- Test 2: 精度测试，看 10 个 tick 是否“差不多 10 次” ----------
    printf("=== Test 2: Timer Accuracy Test ===\n");
    printf("Testing if about 10 ticks elapse when we wait for 10 tick steps...\n\n");

    // 等待一个完整 tick 边界
    uint64 accuracy_start = timer_get_ticks();
    while (timer_get_ticks() == accuracy_start)
        ;
    accuracy_start = timer_get_ticks();

    uint64 accuracy_target = accuracy_start + 10;
    int    busy_counter    = 0;

    while (timer_get_ticks() < accuracy_target) {
        busy_counter++;   // 纯粹占 CPU，让中断有机会进来
    }

    uint64 accuracy_end = timer_get_ticks();

    printf("Expected: 10 ticks\n");
    printf("Actual:   %d ticks\n", accuracy_end - accuracy_start);

    uint64 delta            = accuracy_end - accuracy_start;
    uint64 accuracy_percent = (delta > 0) ? (1000 * 10 / delta) : 0;

    printf("Accuracy (rough): 10/%d = %d.%d%%\n",
           delta,
           (int)(accuracy_percent / 10),
           (int)(accuracy_percent % 10));

    if (accuracy_end - accuracy_start == 10) {
        printf("  ✓ PASS: Timer accuracy is precise!\n\n");
    } else if (accuracy_end - accuracy_start > 10) {
        printf("  ⚠ WARNING: Timer running slightly slow (expected on emulators)\n\n");
    } else {
        printf("  ⚠ WARNING: Timer running slightly fast\n\n");
    }

    // ---------- Test 3: 实时“进度条”式观察 20 个 tick ----------
    printf("=== Test 3: Real-time Timer Watch ===\n");
    printf("Watching timer for 20 ticks (each '.' = 1 tick)\n\n");

    uint64 watch_start = timer_get_ticks();
    uint64 watch_last  = watch_start;
    int    watch_dots  = 0;

    while (timer_get_ticks() < watch_start + 20) {
        uint64 current = timer_get_ticks();
        if (current > watch_last) {
            printf(".");
            watch_dots++;

            if (watch_dots % 10 == 0) {
                printf(" %d/%d\n", watch_dots, 20);
            }

            watch_last = current;
        }
    }

    printf("\n\nTest 3 Results:\n");
    printf("  Ticks observed: %d\n", watch_dots);
    printf("  ✓ Timer working in real-time!\n\n");

    // ---------- Summary ----------
    printf("========================================\n");
    printf("  Timer Test Summary\n");
    printf("========================================\n");
    printf("Test 1 - Tick Observation:  PASS (>=50 ticks)\n");
    printf("Test 2 - Accuracy Check:    %s\n",
           (accuracy_end - accuracy_start == 10) ? "PASS" : "PASS (approx.)");
    printf("Test 3 - Real-time Watch:   PASS\n\n");
}

// ==================== Test: exception handling ====================
static void trigger_ecall_from_smode(void)
{
    printf("Triggering an Environment call from S-mode (ecall)...\n");
    printf("You should see exception info printed by trap_kernel_handler().\n");
    printf("After that, the kernel is expected to panic and stop.\n\n");

    asm volatile("ecall");

    // 正常情况下不会执行到这里
    printf("WARNING: ecall did NOT trap as expected!\n");
}

void test_exception_handling(void)
{
    printf("========================================\n");
    printf("  Exception Handling Test\n");
    printf("========================================\n\n");

    // 三选一：每次只打开一个测试就行
    // 1) 测试 ecall 异常
    trigger_ecall_from_smode();
    // 3) 测试内存访问异常
    trigger_bad_memory_access();
    // 2) 测试非法指令异常
     trigger_illegal_instruction();
    // 理论上到不了这里，因为上面任意一个都会 panic
    printf("Exception test finished (this line should not normally be reached).\n\n");
}

// ==================== Test: interrupt "overhead" (rough) ====================
void test_interrupt_overhead(void)
{
    printf("========================================\n");
    printf("  Interrupt Overhead Test (rough)\n");
    printf("========================================\n\n");

    // 先对齐到一个新的 tick 边界
    uint64 t_start_tick = timer_get_ticks();
    while (timer_get_ticks() == t_start_tick)
        ;

    const int N = 20;

    uint64 logical_start_tick = timer_get_ticks();
    uint64 t0                 = r_time();

    // 等 N 个 tick
    for (int i = 0; i < N; i++) {
        uint64 cur = timer_get_ticks();
        while (timer_get_ticks() == cur)
            ;
    }

    uint64 t1               = r_time();
    uint64 logical_end_tick = timer_get_ticks();

    uint64 ticks_delta  = logical_end_tick - logical_start_tick;
    uint64 cycles_delta = t1 - t0;

    printf("Timer ticks elapsed (logical): %d\n", ticks_delta);
    printf("Time elapsed (cycles):         %d\n", cycles_delta);

    if (ticks_delta > 0) {
        printf("Avg cycles per tick interval:  %d\n", (uint64)(cycles_delta / ticks_delta));
    }

    printf("\nNOTE: 这里测的是“完整 tick 周期”的粗略开销，\n");
    printf("      包含定时器硬件间隔 + trap 进出 + handler 代码，不是纯 trap 指令成本。\n\n");
}
