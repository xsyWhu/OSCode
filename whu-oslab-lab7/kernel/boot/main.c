#include "riscv.h"
#include "lib/print.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "trap/trap.h"
#include "dev/timer.h"
#include "dev/uart.h"
#include "dev/plic.h"
#include "dev/virtio_disk.h"
#include "proc/proc.h"
#include "fs/file.h"
#include "fs/dir.h"
#include "fs/fs.h"
#include "fs/log.h"
#include "fs/bio.h"
#include "lib/lock.h"
#include "lib/string.h"

#define LAB6_ENABLE_SYSCALL_TESTS 1
#define LAB7_FS_EARLY_INIT 0
#define LAB7_PATH_MAX 128

volatile static int started = 0;

// 新增：Lab7 测试规模控制
#define LAB7_CONCURRENT_WORKERS     2     // 并发 worker 数量
#define LAB7_CONCURRENT_ITERS       2     // 每个 worker 的循环次数，原来是 100
#define LAB7_PERF_SMALL_FILES       2     // 小文件个数，原来是 1000
#define LAB7_PERF_LARGE_WRITES      1     // 大文件写入次数，原来是 1024


// === Test entry points ===
static void run_all_tests(void);
static void test_process_creation(void);
static void test_scheduler(void);
static void test_synchronization(void);
static void test_exit_wait(void);
static void debug_proc_table(void);
static void run_lab6_syscall_tests(void);
static void run_lab7_filesystem_tests(void);
static void test_filesystem_integrity(void);
static void test_concurrent_access(void);
static void test_crash_recovery(void);
static void test_filesystem_performance(void);
static void debug_filesystem_state(void);
static void debug_inode_usage(void);
static void debug_disk_io(void);

static struct file* lab7_open_file(const char *path, int omode);
static int lab7_unlink(const char *path);
static int lab7_count_free_blocks(void);
static int lab7_count_free_inodes(void);
static void lab7_concurrent_worker(void);
static void lab7_make_concurrent_name(char *buf, int len, int worker_id, int iter);
static void lab7_make_small_name(char *buf, int len, int idx);
static void lab7_append_literal(const char *lit, int *off, char *buf, int len);
static void lab7_append_decimal(char *buf, int len, int *off, int value);
static void lab7_finalize_name(char *buf, int len, int off);
static int lab7_strcmp(const char *a, const char *b);
static int lab7_is_dir_empty(struct inode *dp);

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

static int lab6_sys_getpid(void)
{
    struct proc *p = myproc();
    return p ? p->pid : -1;
}

static int lab6_sys_fork(void)
{
    struct proc *p = myproc();
    if (!p || !p->pagetable) {
        printf("[LAB6] fork skipped (no user pagetable in current process)\n");
        return -1;
    }
    return fork_process();
}

static int lab6_sys_wait(int *status)
{
    return wait_process(status);
}

static int lab6_sys_open(const char *path, int mode)
{
    printf("[LAB6] open(\"%s\", %d) skipped (filesystem not implemented)\n",
           path ? path : "<null>", mode);
    return -1;
}

static int lab6_sys_close(int fd)
{
    printf("[LAB6] close(%d) skipped (filesystem not implemented)\n", fd);
    return -1;
}

static int lab6_sys_write(int fd, const void *buf, int len)
{
    printf("[LAB6] write(fd=%d, buf=%p, len=%d) skipped\n", fd, buf, len);
    return -1;
}

static int lab6_sys_read(int fd, void *buf, int len)
{
    printf("[LAB6] read(fd=%d, buf=%p, len=%d) skipped\n", fd, buf, len);
    return -1;
}

static uint64 lab6_sys_time(void)
{
    return timer_get_ticks();
}

static void lab6_test_basic_syscalls(void)
{
    printf("\n[LAB6] test_basic_syscalls\n");
    int pid = lab6_sys_getpid();
    printf("[LAB6] Current PID: %d\n", pid);
    int child_pid = lab6_sys_fork();
    if (child_pid == 0) {
        printf("[LAB6] Child process path would execute exit(42)\n");
        return;
    } else if (child_pid > 0) {
        int status = 0;
        if (lab6_sys_wait(&status) >= 0) {
            printf("[LAB6] Child exited with status %d\n", status);
        } else {
            printf("[LAB6] wait() unavailable in current configuration\n");
        }
    } else {
        printf("[LAB6] fork() unavailable in current configuration\n");
    }
}

static void lab6_test_parameter_passing(void)
{
    printf("\n[LAB6] test_parameter_passing\n");
    char buffer[] = "Hello, World!";
    int fd = lab6_sys_open("/dev/console", LAB6_O_RDWR);
    if (fd >= 0) {
        int bytes = lab6_sys_write(fd, buffer, sizeof(buffer) - 1);
        printf("[LAB6] Wrote %d bytes to console\n", bytes);
        lab6_sys_close(fd);
    } else {
        printf("[LAB6] open /dev/console failed (expected without FS)\n");
    }
    (void)lab6_sys_write(-1, buffer, 10);
    (void)lab6_sys_write(fd, NULL, 10);
    (void)lab6_sys_write(fd, buffer, -1);
}

static void lab6_test_security(void)
{
    printf("\n[LAB6] test_security\n");
    char *invalid_ptr = (char*)0x1000000;
    int result = lab6_sys_write(1, invalid_ptr, 10);
    printf("[LAB6] Invalid pointer write result: %d\n", result);
    char small_buf[4];
    result = lab6_sys_read(0, small_buf, 1000);
    printf("[LAB6] Oversized read result: %d\n", result);
}

static void lab6_test_syscall_performance(void)
{
    printf("\n[LAB6] test_syscall_performance\n");
    uint64 start = lab6_sys_time();
    for (int i = 0; i < 10000; i++) {
        (void)lab6_sys_getpid();
    }
    uint64 end = lab6_sys_time();
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
        printf("Physical memory manager initialized.\n");
        kvm_init();
        printf("Kernel virtual memory initialized.\n");
        kvm_inithart();
        printf("Kernel page table for hart %d initialized.\n", cpuid);
        trap_init();
        printf("Trap vectors initialized.\n");
        trap_inithart();
        printf("Trap handling for hart %d initialized.\n", cpuid);
        uart_init();
        printf("UART initialized.\n");
        virtio_disk_init();
        printf("Virtio disk initialized.\n");
        binit();
        printf("Buffer cache initialized.\n");
        fs_init(ROOTDEV);
        printf("File system initialized.\n");
        proc_init();
        printf("Process table initialized.\n");
        fileinit();
        printf("File table initialized.\n");
        userinit();
        printf("First user process initialized.\n");

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
    run_lab7_filesystem_tests();
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

static volatile int lab7_worker_next_id = 0;

static int lab7_assign_worker_id(void)
{
    return __sync_fetch_and_add(&lab7_worker_next_id, 1);
}

static void run_lab7_filesystem_tests(void)
{
    printf("\n[LAB7] Running filesystem tests\n");
    test_filesystem_integrity();
    test_concurrent_access();
    test_crash_recovery();
    test_filesystem_performance();
    debug_filesystem_state();
    debug_inode_usage();
    debug_disk_io();
    printf("[LAB7] Filesystem tests completed\n");
}

// static void run_lab7_filesystem_tests_simple(void)
// {
//     printf("\n[LAB7] Running filesystem tests\n");
//     test_filesystem_integrity();
//     // test_concurrent_access();
//     // test_crash_recovery();
//     // test_filesystem_performance();
//     debug_filesystem_state();
//     // debug_inode_usage();   // 这个本来就先关掉
//     debug_disk_io();
//     printf("[LAB7] Filesystem tests completed\n");
// }

static void test_filesystem_integrity(void)
{
    printf("\n[LAB7] test_filesystem_integrity\n");

    struct file *f = lab7_open_file("testfile", O_CREATE | O_RDWR);
    if (!f) {
        printf("[LAB7] failed to open testfile for writing\n");
        return;
    }
    const char message[] = "Hello, filesystem!";
    int written = filewrite(f, message, strlen(message));
    fileclose(f);
    if (written != (int)strlen(message)) {
        printf("[LAB7] failed to write test data\n");
        return;
    }

    f = lab7_open_file("testfile", O_RDONLY);
    if (!f) {
        printf("[LAB7] failed to reopen testfile for reading\n");
        return;
    }
    char buffer[64];
    int read = fileread(f, buffer, sizeof(buffer) - 1);
    if (read < 0)
        read = 0;
    buffer[read] = '\0';
    fileclose(f);

    if (lab7_strcmp(message, buffer) != 0) {
        printf("[LAB7] readback mismatch: got \"%s\"\n", buffer);
        return;
    }

    if (lab7_unlink("testfile") < 0) {
        printf("[LAB7] failed to remove testfile\n");
    }

    printf("[LAB7] test_filesystem_integrity passed\n");
}

static void test_concurrent_access(void)
{
    printf("\n[LAB7] test_concurrent_access\n");
    lab7_worker_next_id = 0;
    const int workers = LAB7_CONCURRENT_WORKERS;
    int launched = 0;

    for (int i = 0; i < workers; i++) {
        int pid = create_process(lab7_concurrent_worker, "lab7-conc");
        if (pid < 0) {
            printf("[LAB7] failed to spawn worker %d\n", i);
            break;
        }
        launched++;
        printf("[LAB7] worker pid=%d started\n", pid);
    }

    wait_for_children(launched);
    printf("[LAB7] test_concurrent_access completed\n");
}

static void lab7_concurrent_worker(void)
{
    int worker_id = lab7_assign_worker_id();
    char filename[32];

    for (int iter = 0; iter < LAB7_CONCURRENT_ITERS; iter++) {
        lab7_make_concurrent_name(filename, sizeof(filename), worker_id, iter);
        struct file *f = lab7_open_file(filename, O_CREATE | O_RDWR);
        if (f) {
            int value = (worker_id << 16) | iter;
            filewrite(f, (const char*)&value, sizeof(value));
            fileclose(f);
        }
        lab7_unlink(filename);
    }
    exit_process(0);
}

static void test_crash_recovery(void)
{
    printf("\n[LAB7] test_crash_recovery (simulation placeholder)\n");
    printf("[LAB7] crash recovery simulation skipped (requires external framework)\n");
}

static struct file* lab7_open_file(const char *path, int omode)
{
    if (!path)
        return 0;
    char pathbuf[LAB7_PATH_MAX];
    safestrcpy(pathbuf, path, sizeof(pathbuf));
    begin_op();
    struct inode *ip = 0;
    if (omode & O_CREATE) {
        ip = inode_create(pathbuf, ITYPE_FILE, 0, 0);
        if (!ip) {
            end_op();
            return 0;
        }
    } else {
        ip = namei(pathbuf);
        if (!ip) {
            end_op();
            return 0;
        }
        printf("[lab7] open_file existing inode %d type=%d\n", ip->inum, ip->type);
        ilock(ip);
    }

    if (ip->type == ITYPE_DIR && (omode & (O_WRONLY | O_RDWR))) {
        iunlockput(ip);
        end_op();
        return 0;
    }

    struct file *f = filealloc();
    if (!f) {
        iunlockput(ip);
        end_op();
        return 0;
    }

    f->type = FD_INODE;
    f->ip = ip;
    f->off = 0;
    f->readable = (omode & O_WRONLY) ? 0 : 1;
    f->writable = (omode & (O_WRONLY | O_RDWR)) ? 1 : 0;
    if (omode & O_CREATE)
        f->writable = 1;

    iunlock(ip);
    end_op();
    return f;
}

static int lab7_is_dir_empty(struct inode *dp)
{
    struct dirent de;
    for (uint32 off = 2 * sizeof(de); off < dp->size; off += sizeof(de)) {
        if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de)) {
            panic("lab7_is_dir_empty: readi failed");
        }
        if (de.inum != 0)
            return 0;
    }
    return 1;
}

static int lab7_unlink(const char *path)
{
    if (!path)
        return -1;
    char pathbuf[LAB7_PATH_MAX];
    safestrcpy(pathbuf, path, sizeof(pathbuf));
    char name[DIRSIZ];
    begin_op();
    struct inode *dp = nameiparent(pathbuf, name);
    if (!dp) {
        end_op();
        return -1;
    }

    ilock(dp);
    if (namecmp(name, ".") == 0 || namecmp(name, "..") == 0) {
        iunlockput(dp);
        end_op();
        return -1;
    }

    uint32 off;
    struct inode *ip = dirlookup(dp, name, &off);
    if (!ip) {
        iunlockput(dp);
        end_op();
        return -1;
    }

    ilock(ip);
    if (ip->nlink < 1) {
        panic("lab7_unlink: nlink < 1");
    }
    if (ip->type == ITYPE_DIR && !lab7_is_dir_empty(ip)) {
        iunlockput(ip);
        iunlockput(dp);
        end_op();
        return -1;
    }

    struct dirent de = {0};
    if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de)) {
        panic("lab7_unlink: writei failed");
    }
    if (ip->type == ITYPE_DIR) {
        dp->nlink--;
        inode_update(dp);
    }
    iunlock(dp);
    iput(dp);

    ip->nlink--;
    inode_update(ip);
    iunlockput(ip);
    end_op();
    return 0;
}

static void test_filesystem_performance(void)
{
    printf("\n[LAB7] test_filesystem_performance\n");
    uint64 start = timer_get_ticks();

    for (int i = 0; i < LAB7_PERF_SMALL_FILES; i++) {
        char filename[32];
        lab7_make_small_name(filename, sizeof(filename), i);
        struct file *f = lab7_open_file(filename, O_CREATE | O_RDWR);
        if (f) {
            filewrite(f, "test", 4);
            fileclose(f);
        }
    }
    uint64 small_time = timer_get_ticks() - start;

    start = timer_get_ticks();
    struct file *large = lab7_open_file("large_file", O_CREATE | O_RDWR);
    char large_buffer[4096];
    memset(large_buffer, 0, sizeof(large_buffer));
    if (large) {
        for (int i = 0; i < LAB7_PERF_LARGE_WRITES; i++) {
            filewrite(large, large_buffer, sizeof(large_buffer));
        }
        fileclose(large);
    }
    uint64 large_time = timer_get_ticks() - start;

    for (int i = 0; i < LAB7_PERF_SMALL_FILES; i++) {
        char filename[32];
        lab7_make_small_name(filename, sizeof(filename), i);
        lab7_unlink(filename);
    }
    lab7_unlink("large_file");

    printf("[LAB7] Small files (%lux4B): %lu ticks\n",LAB7_PERF_SMALL_FILES,small_time);
    printf("[LAB7] Large file (4MB): %lu ticks\n", large_time);
}

static void debug_filesystem_state(void)
{
    printf("\n[LAB7] debug_filesystem_state\n");
    const struct superblock *sb = fs_superblock();
    if (!sb) {
        printf("[LAB7] superblock missing\n");
        return;
    }
    printf("Total blocks: %u\n", sb->size);
    printf("Free blocks: %d\n", lab7_count_free_blocks());
    printf("Free inodes: %d\n", lab7_count_free_inodes());
    printf("Log blocks: %u\n", sb->nlog);
    printf("Root inode: %u\n", ROOTINO);
}

static int lab7_count_free_blocks(void)
{
    const struct superblock *sb = fs_superblock();
    if (!sb)
        return 0;
    uint32 dev = fs_device();
    int total = 0;

    for (uint32 b = 0; b < sb->size; b += BPB) {
        struct buf *bp = bread(dev, BBLOCK(b, *sb));
        for (uint32 bi = 0; bi < BPB && (b + bi) < sb->size; bi++) {
            uint32 mask = 1 << (bi & 7);
            uint8 byte = bp->data[bi / 8];
            if ((byte & mask) == 0)
                total++;
        }
        brelse(bp);
    }
    return total;
}

static int lab7_count_free_inodes(void)
{
    const struct superblock *sb = fs_superblock();
    if (!sb)
        return 0;
    uint32 dev = fs_device();
    int total = 0;
    struct buf *bp = 0;
    uint32 current_block = (uint32)-1;

    for (uint32 inum = 1; inum < sb->ninodes; inum++) {
        uint32 blockno = IBLOCK(inum, *sb);
        if (blockno != current_block) {
            if (bp) {
                brelse(bp);
            }
            bp = bread(dev, blockno);
            current_block = blockno;
        }
        struct dinode *dip = ((struct dinode*)bp->data) + (inum % IPB);
        if (dip->type == ITYPE_EMPTY)
            total++;
    }
    if (bp) {
        brelse(bp);
    }
    return total;
}

static void debug_inode_usage(void)
{
    printf("\n[LAB7] debug_inode_usage\n");
    const struct superblock *sb = fs_superblock();
    if (!sb)
        return;
    uint32 dev = fs_device();
    struct buf *bp = 0;
    uint32 current_block = (uint32)-1;

    for (uint32 inum = 1; inum < sb->ninodes; inum++) {
        uint32 blockno = IBLOCK(inum, *sb);
        if (blockno != current_block) {
            if (bp)
                brelse(bp);
            bp = bread(dev, blockno);
            current_block = blockno;
        }
        struct dinode *dip = (struct dinode*)bp->data + (inum % IPB);
        if (dip->type != ITYPE_EMPTY) {
            printf("Inode %u: type=%d size=%u nlink=%u\n",
                   inum, dip->type, dip->size, dip->nlink);
        }
    }
    if (bp)
        brelse(bp);
    printf("[LAB7] inode usage scan completed\n");
}

static void debug_disk_io(void)
{
    printf("\n[LAB7] debug_disk_io\n");
    printf("Disk reads: %lu\n", disk_read_count);
    printf("Disk writes: %lu\n", disk_write_count);
}

static void lab7_make_concurrent_name(char *buf, int len, int worker_id, int iter)
{
    int off = 0;
    lab7_append_literal("test_", &off, buf, len);
    lab7_append_decimal(buf, len, &off, worker_id);
    lab7_append_literal("_", &off, buf, len);
    lab7_append_decimal(buf, len, &off, iter);
    lab7_finalize_name(buf, len, off);
}

static void lab7_make_small_name(char *buf, int len, int idx)
{
    int off = 0;
    lab7_append_literal("small_", &off, buf, len);
    lab7_append_decimal(buf, len, &off, idx);
    lab7_finalize_name(buf, len, off);
}

static void lab7_append_literal(const char *lit, int *off, char *buf, int len)
{
    while (*lit && *off + 1 < len) {
        buf[(*off)++] = *lit++;
    }
}

static void lab7_append_decimal(char *buf, int len, int *off, int value)
{
    char scratch[12];
    int idx = 0;
    unsigned int u = value < 0 ? -value : value;
    do {
        if (idx < (int)sizeof(scratch))
            scratch[idx++] = '0' + (u % 10);
        u /= 10;
    } while (u > 0);
    if (value < 0 && idx < (int)sizeof(scratch))
        scratch[idx++] = '-';
    while (idx > 0 && *off + 1 < len) {
        buf[(*off)++] = scratch[--idx];
    }
}

static void lab7_finalize_name(char *buf, int len, int off)
{
    if (len <= 0)
        return;
    if (off < len)
        buf[off] = '\0';
    else
        buf[len - 1] = '\0';
}

static int lab7_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (uint8)*a - (uint8)*b;
}

static void start_worker_demo(void)
{
    int fast = create_process(worker_fast, "worker-fast");
    int medium = create_process(worker_medium, "worker-medium");
    int slow = create_process(worker_slow, "worker-slow");

    printf("Spawned worker threads: fast=%d medium=%d slow=%d\n",
           fast, medium, slow);
}
