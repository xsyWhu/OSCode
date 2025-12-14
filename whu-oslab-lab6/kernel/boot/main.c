#include "riscv.h"
#include "lib/print.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "trap/trap.h"
#include "dev/timer.h"
#include "dev/uart.h"
#include "dev/plic.h"
#include "proc/proc.h"
#include "fs/file.h"
#include "lib/lock.h"
#include "lib/string.h"
#include "syscall.h"

#define LAB6_ENABLE_SYSCALL_TESTS 1

volatile static int started = 0;

// === Test entry points ===
static void run_all_tests(void);
static void test_process_creation(void);
static void test_scheduler(void);
static void test_synchronization(void);
static void test_exit_wait(void);
static void debug_proc_table(void);
static void run_lab6_syscall_tests(void);

// === 测试用任务 ===
static void simple_task(void);
static void cpu_task(void);
static void producer_task(void);
static void consumer_task(void);
static void exit_status_task(void);

// === 共享测试状态 ===
static spinlock_t sync_lock;
static int sync_buffer = 0;
static volatile int produced_total = 0;
static volatile int consumed_total = 0;
static const int sync_capacity = 4;
static const int sync_target = 24;
static const int exit_code_factor = 7;

#if LAB6_ENABLE_SYSCALL_TESTS
#define LAB6_O_RDONLY 0x0
#define LAB6_O_WRONLY 0x1
#define LAB6_O_RDWR   0x2
#define LAB6_O_CREATE 0x200

static struct proc* lab6_find_proc_by_name(const char *name);
static struct proc* lab6_find_proc_by_pid(int pid);
static uint64 lab6_invoke_syscall(struct proc *target, int num,
                                  uint64 a0, uint64 a1, uint64 a2,
                                  uint64 a3, uint64 a4, uint64 a5);
static uint64 lab6_user_sbrk(struct proc *p, uint64 bytes);
static void lab6_force_exit(struct proc *child, int status);

static struct proc* lab6_find_proc_by_name(const char *name)
{
    if (!name)
        return NULL;
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &proc_table[i];
        spinlock_acquire(&p->lock);
        int match = (p->state != PROC_UNUSED &&
                     strncmp(p->name, name, sizeof(p->name)) == 0);
        spinlock_release(&p->lock);
        if (match)
            return p;
    }
    return NULL;
}

static struct proc* lab6_find_proc_by_pid(int pid)
{
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &proc_table[i];
        spinlock_acquire(&p->lock);
        int match = (p->state != PROC_UNUSED && p->pid == pid);
        spinlock_release(&p->lock);
        if (match)
            return p;
    }
    return NULL;
}

static uint64 lab6_invoke_syscall(struct proc *target, int num,
                                  uint64 a0, uint64 a1, uint64 a2,
                                  uint64 a3, uint64 a4, uint64 a5)
{
    if (!target || !target->trapframe || !target->pagetable)
        return (uint64)-1;

    struct cpu *c = mycpu();
    struct proc *saved_proc = c->proc;
    struct trapframe saved_tf;
    int intena = intr_get();

    intr_off();
    memcpy(&saved_tf, target->trapframe, sizeof(saved_tf));

    c->proc = target;
    target->trapframe->a0 = a0;
    target->trapframe->a1 = a1;
    target->trapframe->a2 = a2;
    target->trapframe->a3 = a3;
    target->trapframe->a4 = a4;
    target->trapframe->a5 = a5;
    target->trapframe->a7 = num;

    syscall();
    uint64 ret = target->trapframe->a0;

    memcpy(target->trapframe, &saved_tf, sizeof(saved_tf));
    c->proc = saved_proc;
    if (intena)
        intr_on();

    return ret;
}

static uint64 lab6_user_sbrk(struct proc *p, uint64 bytes)
{
    if (!p || bytes > 0x7fffffffULL)
        return (uint64)-1;
    return lab6_invoke_syscall(p, SYS_sbrk, bytes, 0, 0, 0, 0, 0);
}

static void lab6_force_exit(struct proc *child, int status)
{
    if (!child)
        return;
    spinlock_acquire(&child->lock);
    child->exit_code = status;
    child->state = PROC_ZOMBIE;
    spinlock_release(&child->lock);
    if (child->parent)
        wakeup(child->parent);
}

static void lab6_test_basic_syscalls(void)
{
    printf("\n[LAB6] test_basic_syscalls\n");
    struct proc *initp = lab6_find_proc_by_name("init");
    if (!initp) {
        printf("[LAB6] init process not ready; skipping syscall tests\n");
        return;
    }

    uint64 pid = lab6_invoke_syscall(initp, SYS_getpid, 0, 0, 0, 0, 0, 0);
    printf("[LAB6] Current PID: %lu\n", pid);

    uint64 child_pid = lab6_invoke_syscall(initp, SYS_fork, 0, 0, 0, 0, 0, 0);
    if ((int)child_pid < 0) {
        printf("[LAB6] fork() failed/unavailable\n");
        return;
    }

    struct proc *child = lab6_find_proc_by_pid((int)child_pid);
    if (!child) {
        printf("[LAB6] fork returned pid=%lu but child not found\n", child_pid);
        return;
    }

    // Mark child as exited with status 42 (avoid running noreturn sys_exit)
    lab6_force_exit(child, 42);

    // Ask parent to wait; store status in parent's stack top
    uint64 status_va = initp->sz - sizeof(int);
    uint64 waited_pid = lab6_invoke_syscall(initp, SYS_wait, status_va, 0, 0, 0, 0, 0);
    int status = 0;
    copyin(initp->pagetable, &status, status_va, sizeof(status));

    printf("[LAB6] wait() returned pid=%lu status=%d\n", waited_pid, status);
}

static void lab6_test_parameter_passing(void)
{
    printf("\n[LAB6] test_parameter_passing\n");
    struct proc *initp = lab6_find_proc_by_name("init");
    if (!initp) {
        printf("[LAB6] init process not ready; skipping\n");
        return;
    }

    const char *path = "/dev/console";
    uint64 path_uaddr = lab6_user_sbrk(initp, strlen(path) + 16);
    if (path_uaddr == (uint64)-1 || copyout(initp->pagetable, path_uaddr, path, strlen(path) + 1) < 0) {
        printf("[LAB6] unable to stage path in user space\n");
        return;
    }

    char buffer[] = "Hello, World!";
    uint64 buf_uaddr = lab6_user_sbrk(initp, sizeof(buffer));
    if (buf_uaddr == (uint64)-1 || copyout(initp->pagetable, buf_uaddr, buffer, sizeof(buffer)) < 0) {
        printf("[LAB6] unable to stage buffer in user space\n");
        return;
    }

    int fd = (int)lab6_invoke_syscall(initp, SYS_open, path_uaddr, LAB6_O_RDWR, 0, 0, 0, 0);
    if (fd >= 0) {
        int bytes = (int)lab6_invoke_syscall(initp, SYS_write, fd, buf_uaddr, sizeof(buffer) - 1, 0, 0, 0);
        printf("[LAB6] Wrote %d bytes to console\n", bytes);
        lab6_invoke_syscall(initp, SYS_close, fd, 0, 0, 0, 0, 0);
    } else {
        printf("[LAB6] open /dev/console failed (expected without FS)\n");
    }

    int fd_answer = lab6_invoke_syscall(initp, SYS_write, (uint64)-1, buf_uaddr, 10, 0, 0, 0);
    printf("[LAB6] write with invalid fd result: %d\n", fd_answer);
    int pointTest = lab6_invoke_syscall(initp, SYS_write, fd, 0, 10, 0, 0, 0);
    printf("[LAB6] write with null buffer result: %d\n", pointTest);
    int test_answer = lab6_invoke_syscall(initp, SYS_write, fd, buf_uaddr, (uint64)-1, 0, 0, 0);
    printf("[LAB6] write with oversized length result: %d\n", test_answer);
}

static void lab6_test_security(void)
{
    printf("\n[LAB6] test_security\n");
    struct proc *initp = lab6_find_proc_by_name("init");
    if (!initp) {
        printf("[LAB6] init process not ready; skipping\n");
        return;
    }

    char *invalid_ptr = (char*)0x1000000;
    int result = (int)lab6_invoke_syscall(initp, SYS_write, 1, (uint64)invalid_ptr, 10, 0, 0, 0);
    printf("[LAB6] Invalid pointer write result: %d\n", result);

    // Use invalid fd to avoid blocking on console input
    char dummy[4] = {0};
    uint64 dummy_uaddr = lab6_user_sbrk(initp, sizeof(dummy));
    if (dummy_uaddr != (uint64)-1) {
        copyout(initp->pagetable, dummy_uaddr, dummy, sizeof(dummy));
    }
    result = (int)lab6_invoke_syscall(initp, SYS_read, (uint64)-1, dummy_uaddr, 1000, 0, 0, 0);
    printf("[LAB6] Oversized read result: %d\n", result);
}

static void lab6_test_syscall_performance(void)
{
    printf("\n[LAB6] test_syscall_performance\n");
    struct proc *initp = lab6_find_proc_by_name("init");
    if (!initp) {
        printf("[LAB6] init process not ready; skipping\n");
        return;
    }

    uint64 start = timer_get_ticks();
    for (int i = 0; i < 10000; i++) {
        (void)lab6_invoke_syscall(initp, SYS_getpid, 0, 0, 0, 0, 0, 0);
    }
    uint64 end = timer_get_ticks();
    printf("[LAB6] 10000 getpid() calls took %lu ticks\n", end - start);
}

static void run_lab6_syscall_tests(void)
{
    lab6_test_basic_syscalls();
    lab6_test_parameter_passing();
    lab6_test_security();
    lab6_test_syscall_performance();
}
#else
static inline void run_lab6_syscall_tests(void) {}
#endif
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
        fileinit();
        userinit();

        printf("\n========================================\n");
        printf("  Lab5&lab6 Tests\n");
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
    } 
    else {
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
    printf("\n[TEST] Starting tests...\n");
    test_process_creation();
    test_scheduler();
    test_synchronization();
    test_exit_wait();
    debug_proc_table();
    run_lab6_syscall_tests();
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
        spinlock_acquire(&sync_lock);
        while (sync_buffer >= sync_capacity) {
            sleep(&sync_buffer, &sync_lock);
        }
        sync_buffer++;
        produced_total++;
        printf("[producer] produced item #%d (buffer=%d)\n",
               produced_total, sync_buffer);
        wakeup(&sync_buffer);
        spinlock_release(&sync_lock);
    }
    printf("[producer] finished production (%d items)\n", sync_target);
    exit_process(0);
}

static void consumer_task(void)
{
    for (int i = 0; i < sync_target; i++) {
        spinlock_acquire(&sync_lock);
        while (sync_buffer <= 0) {
            sleep(&sync_buffer, &sync_lock);
        }
        sync_buffer--;
        consumed_total++;
        printf("[consumer] consumed item #%d (buffer=%d)\n",
               consumed_total, sync_buffer);
        wakeup(&sync_buffer);
        spinlock_release(&sync_lock);
    }
    printf("[consumer] finished consumption (%d items)\n", sync_target);
    exit_process(0);
}

static void exit_status_task(void)
{
    int pid = myproc()->pid;
    int status = pid * exit_code_factor;
    exit_process(status);
}

static void test_process_creation(void)
{
    printf("\n[LAB5] test_process_creation\n");
    int created = 0;

    for (int i = 0; i < NPROC / 2; i++) {
        int pid = create_process(simple_task, "test-simple");
        if (pid < 0)
            break;
        created++;
    }

    printf("[LAB5] spawned %d simple tasks\n", created);
    wait_for_children(created);
    printf("[LAB5] test_process_creation completed\n");
}

static void test_scheduler(void)
{
    printf("\n[LAB5] test_scheduler\n");
    const int cpu_tasks = 3;
    for (int i = 0; i < cpu_tasks; i++) {
        int pid = create_process(cpu_task, "test-cpu");
        if (pid < 0)
            panic("Failed to create cpu_task");
        printf("[LAB5] cpu_task pid=%d created\n", pid);
    }
    wait_for_children(cpu_tasks);
    printf("[LAB5] test_scheduler completed\n");
}

static void test_synchronization(void)
{
    printf("\n[LAB5] test_synchronization\n");
    spinlock_init(&sync_lock, "sync_lock");
    sync_buffer = 0;
    produced_total = 0;
    consumed_total = 0;

    int prod = create_process(producer_task, "producer");
    int cons = create_process(consumer_task, "consumer");
    if (prod < 0 || cons < 0)
        panic("Failed to create producer/consumer");

    wait_for_children(2);

    printf("[LAB5] produced=%d consumed=%d buffer=%d\n",
           produced_total, consumed_total, sync_buffer);
    printf("[LAB5] test_synchronization completed\n");
}

static void test_exit_wait(void)
{
    printf("\n[LAB5] test_exit_wait\n");
    const int workers = 4;
    for (int i = 0; i < workers; i++) {
        int pid = create_process(exit_status_task, "exit-status");
        if (pid < 0)
            panic("Failed to create exit_status_task");
        printf("[LAB5] exit worker pid=%d scheduled\n", pid);
    }

    for (int completed = 0; completed < workers; completed++) {
        int status = 0;
        int pid = wait_process(&status);
        if (pid < 0)
            panic("wait_process returned -1 unexpectedly");
        int expected = pid * exit_code_factor;
        if (status != expected) {
            printf("[LAB5] expected status %d for pid %d, got %d\n",
                   expected, pid, status);
            panic("exit status mismatch");
        }
    }

    if (wait_process(NULL) != -1) {
        panic("wait_process should return -1 when no children");
    }
    printf("[LAB5] test_exit_wait completed\n");
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
    printf("\n[LAB5] debug_proc_table\n");
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &proc_table[i];
        if (p->state != PROC_UNUSED) {
            printf("  slot=%d pid=%d state=%s name=%s\n",
                   i, p->pid, proc_state_name(p->state), p->name);
        }
    }
    printf("[LAB5] debug_proc_table completed\n");
}

static void start_worker_demo(void)
{
    int fast = create_process(worker_fast, "worker-fast");
    int medium = create_process(worker_medium, "worker-medium");
    int slow = create_process(worker_slow, "worker-slow");

    printf("Spawned worker threads: fast=%d medium=%d slow=%d\n",
           fast, medium, slow);
}
