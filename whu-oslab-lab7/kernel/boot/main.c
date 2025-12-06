#include "riscv.h"
#include "lib/print.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "trap/trap.h"
#include "dev/timer.h"
#include "dev/uart.h"
#include "dev/plic.h"
#include "dev/virtio_blk.h"
#include "proc/proc.h"
#include "proc/trapframe.h"
#include "lib/lock.h"
#include "lib/string.h"
#include "memlayout.h"
#include "syscall/syscall.h"
#include "fs/fs.h"
#include "fs/fcntl.h"

extern void syscall(void);

// === Lab7: filesystem test entry points ===
static void run_all_tests(void);
static void test_filesystem_basic(void);
static void test_filesystem_integrity(void);
static void test_concurrent_access(void);
static void test_crash_recovery(void);
static void test_filesystem_performance(void);
static void debug_filesystem_state(void);
static void debug_inode_usage(void);
static void debug_disk_io(void);
static struct proc* wait_for_init_process(void);
static uint64 invoke_syscall(struct proc *target, int num, uint64 arg0, uint64 arg1, uint64 arg2);

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
        fs_init();
        kvm_init();
        kvm_inithart();
        trap_init();
        trap_inithart();
        uart_init();
        proc_init();
        userinit();

        printf("\n========================================\n");
        printf("  Lab7: Filesystem\n");
        printf("========================================\n\n");

        int tester = create_process(run_all_tests, "lab7-tests");
        if (tester < 0) {
            panic("Failed to create lab7 test process");
        }
        intr_on();
        printf("Interrupts enabled (sstatus.SIE = %d)\n", intr_get());
        printf("Spawned lab7 test process pid=%d, entering scheduler...\n", tester);
        scheduler();
    }
    return 0;
}

// ---------------------------
// 测试入口与实现
// ---------------------------
// Lab7 tests only
// ---------------------------

static void run_all_tests(void)
{
    printf("\n[LAB7] Starting filesystem tests...\n");
    test_filesystem_basic();
    test_filesystem_integrity();
    test_concurrent_access();
    test_crash_recovery();
    test_filesystem_performance();
    debug_filesystem_state();
    debug_inode_usage();
    debug_disk_io();
    printf("[LAB7] Filesystem tests completed\n");
    exit_process(0);
}

// ---------------------------
// Lab7 syscall helpers
// ---------------------------

static struct proc* wait_for_init_process(void)
{
    for (int attempt = 0; attempt < 1000; attempt++) {
        for (int i = 0; i < NPROC; i++) {
            struct proc *p = &proc_table[i];
            if (p->state == PROC_UNUSED)
                continue;
            if (strncmp(p->name, "init", sizeof(p->name)) == 0 && p->tf && p->pagetable) {
                return p;
            }
        }
        yield();
    }
    return NULL;
}

static uint64 invoke_syscall(struct proc *target, int num, uint64 arg0, uint64 arg1, uint64 arg2)
{
    if (!target || !target->tf)
        return (uint64)-1;

    struct cpu *c = mycpu();
    struct proc *saved_proc = c->proc;
    struct trapframe saved_tf = *target->tf;

    c->proc = target;

    target->tf->a0 = arg0;
    target->tf->a1 = arg1;
    target->tf->a2 = arg2;
    target->tf->a3 = 0;
    target->tf->a4 = 0;
    target->tf->a5 = 0;
    target->tf->a7 = num;

    syscall();

    uint64 ret = target->tf->a0;

    *target->tf = saved_tf;
    c->proc = saved_proc;

    return ret;
}

static int copy_user_str(struct proc *p, uint64 dst_va, const char *s)
{
    return copyout(p->pagetable, dst_va, s, strlen(s) + 1);
}

static int copy_user_buf(struct proc *p, uint64 dst_va, const void *buf, int len)
{
    return copyout(p->pagetable, dst_va, buf, len);
}

static void utoa(int x, char *buf)
{
    char tmp[16];
    int n = 0;
    if (x == 0) {
        tmp[n++] = '0';
    } else {
        while (x > 0 && n < (int)sizeof(tmp)) {
            tmp[n++] = '0' + (x % 10);
            x /= 10;
        }
    }
    for (int i = 0; i < n; i++) {
        buf[i] = tmp[n - 1 - i];
    }
    buf[n] = '\0';
}

static void build_name(char *dst, const char *prefix, int idx)
{
    int pos = 0;
    while (prefix[pos]) {
        dst[pos] = prefix[pos];
        pos++;
    }
    char num[16];
    utoa(idx, num);
    for (int i = 0; num[i]; i++) {
        dst[pos++] = num[i];
    }
    dst[pos] = '\0';
}
static void test_filesystem_basic(void)
{
    printf("\n[LAB7] test_filesystem_basic\n");
    struct proc *user = wait_for_init_process();
    if (!user) {
        printf("  [WARN] init process unavailable\n");
        return;
    }

    const char *path = "/lab7.txt";
    const char *msg = "lab7 fs works";
    uint64 path_va = 0x180;
    uint64 buf_va = 0x200;
    int msg_len = strlen(msg);

    // Prepare path and buffer in user space.
    if (copy_user_str(user, path_va, path) < 0 ||
        copy_user_buf(user, buf_va, msg, msg_len + 1) < 0) {
        printf("  [FAIL] unable to seed user memory\n");
        return;
    }

    int fd = (int)invoke_syscall(user, SYS_open, path_va, O_CREATE | O_RDWR, 0);
    printf("  open(create) => %d\n", fd);
    if (fd < 0)
        return;

    uint64 w = invoke_syscall(user, SYS_write, fd, buf_va, msg_len);
    printf("  write => %lu bytes\n", w);

    invoke_syscall(user, SYS_close, fd, 0, 0);

    fd = (int)invoke_syscall(user, SYS_open, path_va, O_RDONLY, 0);
    printf("  reopen => %d\n", fd);
    if (fd < 0)
        return;

    // Read back into same buffer then copy to kernel for comparison.
    uint64 r = invoke_syscall(user, SYS_read, fd, buf_va, 64);
    char kbuf[64] = {0};
    if (copyin(user->pagetable, kbuf, buf_va, sizeof(kbuf)) < 0) {
        printf("  [FAIL] copyin read buffer\n");
    } else {
        kbuf[63] = '\0';
        printf("  read => %lu bytes, content=\"%s\"\n", r, kbuf);
    }
    invoke_syscall(user, SYS_close, fd, 0, 0);
    invoke_syscall(user, SYS_unlink, path_va, 0, 0);
}

static void test_filesystem_integrity(void)
{
    printf("\n[LAB7] test_filesystem_integrity\n");
    struct proc *user = wait_for_init_process();
    if (!user) {
        printf("  [WARN] init process unavailable\n");
        return;
    }

    const char *path = "/testfile";
    const char *msg = "Hello, filesystem!";
    char readbuf[64] = {0};
    uint64 path_va = 0x280;
    uint64 buf_va = 0x300;
    if (copy_user_str(user, path_va, path) < 0 ||
        copy_user_buf(user, buf_va, msg, strlen(msg) + 1) < 0) {
        printf("  [FAIL] unable to seed user memory\n");
        return;
    }

    int fd = (int)invoke_syscall(user, SYS_open, path_va, O_CREATE | O_RDWR, 0);
    if (fd < 0) {
        printf("  [FAIL] open create\n");
        return;
    }
    int written = (int)invoke_syscall(user, SYS_write, fd, buf_va, strlen(msg));
    invoke_syscall(user, SYS_close, fd, 0, 0);
    fd = (int)invoke_syscall(user, SYS_open, path_va, O_RDONLY, 0);
    int r = (int)invoke_syscall(user, SYS_read, fd, buf_va, sizeof(readbuf) - 1);
    if (copyin(user->pagetable, readbuf, buf_va, sizeof(readbuf)) < 0) {
        printf("  [FAIL] copyin read\n");
    } else {
        readbuf[sizeof(readbuf) - 1] = '\0';
        printf("  wrote %d bytes, read %d bytes, content=\"%s\"\n", written, r, readbuf);
    }
    invoke_syscall(user, SYS_close, fd, 0, 0);
    invoke_syscall(user, SYS_unlink, path_va, 0, 0);
}

static void test_concurrent_access(void)
{
    printf("\n[LAB7] test_concurrent_access (sequential simulation)\n");
    struct proc *user = wait_for_init_process();
    if (!user) {
        printf("  [WARN] init process unavailable\n");
        return;
    }
    char path[32];
    char data[32];
    uint64 path_va = 0x380;
    uint64 buf_va = 0x3c0;

    for (int i = 0; i < 4; i++) {
        build_name(path, "/concurrent_", i);
        build_name(data, "file_", i);
        copy_user_str(user, path_va, path);
        copy_user_buf(user, buf_va, data, strlen(data) + 1);

        int fd = (int)invoke_syscall(user, SYS_open, path_va, O_CREATE | O_RDWR, 0);
        if (fd < 0) {
            printf("  [WARN] open failed for %s\n", path);
            continue;
        }
        invoke_syscall(user, SYS_write, fd, buf_va, strlen(data));
        invoke_syscall(user, SYS_close, fd, 0, 0);

        fd = (int)invoke_syscall(user, SYS_open, path_va, O_RDONLY, 0);
        invoke_syscall(user, SYS_read, fd, buf_va, sizeof(data));
        invoke_syscall(user, SYS_close, fd, 0, 0);
        invoke_syscall(user, SYS_unlink, path_va, 0, 0);
    }
}

static void test_crash_recovery(void)
{
    printf("\n[LAB7] test_crash_recovery\n");
    printf("  virtio-backed FS: simulate crash by writing log entries but not flushing\n");
    printf("  (manual inspection needed; automatic recovery not implemented)\n");
}

static void test_filesystem_performance(void)
{
    printf("\n[LAB7] test_filesystem_performance\n");
    struct proc *user = wait_for_init_process();
    if (!user) {
        printf("  [WARN] init process unavailable\n");
        return;
    }

    char path[32];
    uint64 path_va = 0x440;
    uint64 buf_va_small = 0x480;
    uint64 buf_va_chunk = 0x4c0;
    char small_buf[16] = "perf";
    char chunk[64];
    memset(chunk, 'L', sizeof(chunk));

    uint64 start = timer_get_ticks();
    for (int i = 0; i < 10; i++) {
        build_name(path, "/small_", i);
        copy_user_str(user, path_va, path);
        int fd = (int)invoke_syscall(user, SYS_open, path_va, O_CREATE | O_RDWR, 0);
        printf("  [perf] writing small file %s fd=%d\n", path, fd);
        if (copy_user_buf(user, buf_va_small, small_buf, 4) < 0) {
            printf("  [FAIL] copy small buffer\n");
            invoke_syscall(user, SYS_close, fd, 0, 0);
            return;
        }
        invoke_syscall(user, SYS_write, fd, buf_va_small, 4);
        invoke_syscall(user, SYS_close, fd, 0, 0);
    }
    uint64 small_time = timer_get_ticks() - start;

    copy_user_str(user, path_va, "/large_file");
    int fd = (int)invoke_syscall(user, SYS_open, path_va, O_CREATE | O_RDWR, 0);
    start = timer_get_ticks();
    int chunks = 256; // 16KB
    for (int i = 0; i < chunks; i++) {
        if (copy_user_buf(user, buf_va_chunk, chunk, sizeof(chunk)) < 0) {
            printf("  [FAIL] copy chunk %d\n", i);
            invoke_syscall(user, SYS_close, fd, 0, 0);
            return;
        }
        if (i % 32 == 0) {
            printf("  [perf] writing large chunk %d/%d\n", i + 1, chunks);
        }
        invoke_syscall(user, SYS_write, fd, buf_va_chunk, sizeof(chunk));
    }
    uint64 large_time = timer_get_ticks() - start;
    invoke_syscall(user, SYS_close, fd, 0, 0);

    printf("  small files (10x4B): %lu ticks\n", small_time);
    printf("  large file (16KB via 64B chunks): %lu ticks\n", large_time);

    for (int i = 0; i < 10; i++) {
        build_name(path, "/small_", i);
        copy_user_str(user, path_va, path);
        invoke_syscall(user, SYS_unlink, path_va, 0, 0);
    }
    copy_user_str(user, path_va, "/large_file");
    invoke_syscall(user, SYS_unlink, path_va, 0, 0);

    printf("[LAB7] test_filesystem_performance done\n");
}

static void debug_filesystem_state(void)
{
    struct superblock sbinfo;
    fs_get_superblock(&sbinfo);
    int free_blocks = fs_count_free_blocks();
    int free_inodes = fs_count_free_inodes();
    printf("\n[LAB7] debug_filesystem_state\n");
    printf("  superblock: size=%u nblocks=%u ninodes=%u nlog=%u logstart=%u inodestart=%u bmapstart=%u\n",
           sbinfo.size, sbinfo.nblocks, sbinfo.ninodes, sbinfo.nlog,
           sbinfo.logstart, sbinfo.inodestart, sbinfo.bmapstart);
    printf("  free blocks=%d free inodes=%d\n", free_blocks, free_inodes);
    printf("  buffer cache hits=%lu misses=%lu\n", bcache_get_hits(), bcache_get_misses());
}

static void debug_inode_usage(void)
{
    printf("\n[LAB7] debug_inode_usage\n");
    fs_debug_icache();
}

static void debug_disk_io(void)
{
    printf("\n[LAB7] debug_disk_io\n");
    printf("  bcache hits=%lu misses=%lu\n", bcache_get_hits(), bcache_get_misses());
}
