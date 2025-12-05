#include "riscv.h"
#include "lib/print.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "trap/trap.h"
#include "dev/timer.h"
#include "dev/uart.h"
#include "dev/plic.h"
#include "proc/proc.h"
#include "lib/lock.h"

volatile static int started = 0;

// === Test entry points ===
static void run_all_tests(void);
static void test_process_creation(void);
static void test_scheduler(void);
static void test_synchronization(void);
static void debug_proc_table(void);

// === 测试用任务 ===
static void simple_task(void);
static void cpu_task(void);
static void producer_task(void);
static void consumer_task(void);

// === 共享测试状态 ===
static spinlock_t sync_lock;
static int sync_buffer = 0;
static volatile int produced_total = 0;
static volatile int consumed_total = 0;
static const int sync_capacity = 4;
static const int sync_target = 24;

// 简单的内核线程任务（演示用）
static void worker_body(const char *name, uint64 workload);
static void worker_fast(void);
static void worker_medium(void);
static void worker_slow(void);
static void start_worker_demo(void);

static inline void worker_do_work(uint64 cycles)
{
    for (uint64 i = 0; i < cycles; i++) {
        asm volatile("nop");
    }
}

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
        proc_init();

        printf("\n========================================\n");
        printf("  Lab5: Process Management & Scheduler\n");
        printf("========================================\n\n");

        int tester = create_process(run_all_tests, "proc-tests");
        if (tester < 0) {
            panic("Failed to create test runner process");
        }
        printf("Spawned process-test runner pid=%d\n", tester);

        intr_on();
        printf("Interrupts enabled (sstatus.SIE = %d)\n", intr_get());
        printf("Entering scheduler on hart %d...\n\n", cpuid);

        __sync_synchronize();
        started = 1;

        scheduler();
    } else {
        while(started == 0);
        __sync_synchronize();

        kvm_inithart();
        trap_inithart();
        intr_on();

        printf("Hart %d idle - waiting for work\n", cpuid);
        while (1) {
            asm volatile("wfi");
        }
    }
    return 0;
}

static void worker_body(const char *name, uint64 workload)
{
    uint64 iter = 0;
    uint64 last_report = 0;

    while (1) {
        if (iter == 0 || iter >= last_report + 100) {
            last_report = iter;
            printf("[%s] hart=%d iter=%lu ticks=%lu\n",
                   name, mycpuid(), iter, timer_get_ticks());
        }

        worker_do_work(workload);

        iter++;
        yield();
    }
}

static void worker_fast(void)
{
    worker_body("fast", 500000);
}

static void worker_medium(void)
{
    worker_body("medium", 2000000);
}

static void worker_slow(void)
{
    worker_body("slow", 6000000);
}

// ---------------------------
// 测试入口与实现
// ---------------------------

static void wait_for_children(int n)
{
    while (n > 0) {
        if (wait_process(NULL) > 0) {
            n--;
        } else {
            break;
        }
    }
}

static void run_all_tests(void)
{
    printf("\n[TEST] Starting kernel process tests...\n");
    test_process_creation();
    test_scheduler();
    test_synchronization();
    debug_proc_table();
    printf("[TEST] All tests completed, launching worker demo...\n");
    start_worker_demo();
    exit_process(0);
}

static void simple_task(void)
{
    for (int i = 0; i < 3; i++) {
        printf("[simple_task] pid=%d iteration=%d\n", myproc()->pid, i);
        yield();
    }
    exit_process(0);
}

static void cpu_task(void)
{
    for (int i = 0; i < 5; i++) {
        worker_do_work(3000000);
        yield();
    }
    printf("[cpu_task] pid=%d finished workload\n", myproc()->pid);
    exit_process(0);
}

static void producer_task(void)
{
    for (int i = 0; i < sync_target; i++) {
        while (1) {
            spinlock_acquire(&sync_lock);
            if (sync_buffer < sync_capacity) {
                sync_buffer++;
                produced_total++;
                spinlock_release(&sync_lock);
                printf("[producer] produced item #%d (buffer=%d)\n",
                       produced_total, sync_buffer);
                break;
            }
            spinlock_release(&sync_lock);
            yield();
        }
    }
    printf("[producer] finished production (%d items)\n", sync_target);
    exit_process(0);
}

static void consumer_task(void)
{
    for (int i = 0; i < sync_target; i++) {
        while (1) {
            spinlock_acquire(&sync_lock);
            if (sync_buffer > 0) {
                sync_buffer--;
                consumed_total++;
                spinlock_release(&sync_lock);
                printf("[consumer] consumed item #%d (buffer=%d)\n",
                       consumed_total, sync_buffer);
                break;
            }
            spinlock_release(&sync_lock);
            yield();
        }
    }
    printf("[consumer] finished consumption (%d items)\n", sync_target);
    exit_process(0);
}

static void test_process_creation(void)
{
    printf("\n[TEST] test_process_creation\n");
    int created = 0;

    for (int i = 0; i < NPROC / 2; i++) {
        int pid = create_process(simple_task, "test-simple");
        if (pid < 0)
            break;
        created++;
    }

    printf("[TEST] spawned %d simple tasks\n", created);
    wait_for_children(created);
    printf("[TEST] test_process_creation completed\n");
}

static void test_scheduler(void)
{
    printf("\n[TEST] test_scheduler\n");
    const int cpu_tasks = 3;
    for (int i = 0; i < cpu_tasks; i++) {
        int pid = create_process(cpu_task, "test-cpu");
        if (pid < 0)
            panic("Failed to create cpu_task");
        printf("[TEST] cpu_task pid=%d created\n", pid);
    }
    wait_for_children(cpu_tasks);
    printf("[TEST] test_scheduler completed\n");
}

static void test_synchronization(void)
{
    printf("\n[TEST] test_synchronization\n");
    spinlock_init(&sync_lock, "sync_lock");
    sync_buffer = 0;
    produced_total = 0;
    consumed_total = 0;

    int prod = create_process(producer_task, "producer");
    int cons = create_process(consumer_task, "consumer");
    if (prod < 0 || cons < 0)
        panic("Failed to create producer/consumer");

    wait_for_children(2);

    printf("[TEST] produced=%d consumed=%d buffer=%d\n",
           produced_total, consumed_total, sync_buffer);
    printf("[TEST] test_synchronization completed\n");
}

static const char *proc_state_name(enum proc_state state)
{
    switch (state) {
    case PROC_UNUSED:    return "UNUSED";
    case PROC_RUNNABLE:  return "RUNNABLE";
    case PROC_RUNNING:   return "RUNNING";
    case PROC_SLEEPING:  return "SLEEPING";
    case PROC_ZOMBIE:    return "ZOMBIE";
    default:             return "UNKNOWN";
    }
}

static void debug_proc_table(void)
{
    printf("\n[TEST] debug_proc_table\n");
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &proc_table[i];
        if (p->state != PROC_UNUSED) {
            printf("  slot=%d pid=%d state=%s name=%s\n",
                   i, p->pid, proc_state_name(p->state), p->name);
        }
    }
    printf("[TEST] debug_proc_table completed\n");
}

static void start_worker_demo(void)
{
    int fast = create_process(worker_fast, "worker-fast");
    int medium = create_process(worker_medium, "worker-medium");
    int slow = create_process(worker_slow, "worker-slow");

    printf("Spawned worker threads: fast=%d medium=%d slow=%d\n",
           fast, medium, slow);
}
