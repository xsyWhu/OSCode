#include "riscv.h"
#include "lib/print.h"
#include "lib/klog.h"
#include "ipc/msg.h"
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

volatile static int started = 0;

// === Test entry points ===
static void run_all_tests(void);
static void run_priority_mlfq_demo(void);
static void priority_demo_worker(void);

int main()
{
    int cpuid = r_tp();

    if(cpuid == 0) {
        print_init();
        printf("\n=== OS Kernel Booting ===\n\n");

        // 初始化
        klog_init();
        klog_set_level(LOG_LEVEL_DEBUG);
        pmem_init();
        //printf("Physical memory manager initialized.\n");
        kvm_init();
        //printf("Kernel virtual memory initialized.\n");
        kvm_inithart();
        //printf("Kernel page table for hart %d initialized.\n", cpuid);
        trap_init();
        //printf("Trap vectors initialized.\n");
        trap_inithart();
        //printf("Trap handling for hart %d initialized.\n", cpuid);
        uart_init();
        //printf("UART initialized.\n");
        virtio_disk_init();
        //printf("Virtio disk initialized.\n");
        binit();
        //printf("Buffer cache initialized.\n");
        fs_init(ROOTDEV);
        //printf("File system initialized.\n");
        proc_init();
        //printf("Process table initialized.\n");
        fileinit();
        //printf("File table initialized.\n");
        msg_init();
        //printf("IPC message queues initialized.\n");
        userinit();
        //printf("First user process initialized.\n");

        printf("\n========================================\n");
        printf("  MLFQ Priority Scheduler Demo\n");
        printf("========================================\n\n");

        int tester = create_process(run_all_tests, "proc-tests");
        if (tester < 0) {
            panic("Failed to create test runner process");
        }
        printf("Started priority demo runner pid=%d\n", tester);

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

// ---------------------------
// 测试入口与实现
// ---------------------------

static void run_all_tests(void)
{
    printf("\n[PRIORITY-DEMO] Running MLFQ priority scheduler showcase\n");
    klog(LOG_LEVEL_INFO, "[PRIORITY-DEMO] start showcase");
    run_priority_mlfq_demo();
    printf("[PRIORITY-DEMO] Scheduler showcase complete\n");
    klog(LOG_LEVEL_INFO, "[PRIORITY-DEMO] showcase complete");
    exit_process(0);
}

#define PRIORITY_DEMO_WORKERS 6

struct priority_demo_job {
    const char *name;
    int priority;
    uint64 workload;
};

static const struct priority_demo_job priority_demo_jobs[PRIORITY_DEMO_WORKERS] = {
    { "mlfq-high-1", PRIORITY_MAX, 8000000UL },
    { "mlfq-high-2", PRIORITY_MAX - 1, 7200000UL },
    { "mlfq-mid-1", PRIORITY_MAX / 2 + 1, 6000000UL },
    { "mlfq-mid-2", PRIORITY_MAX / 2, 5600000UL },
    { "mlfq-low-1", PRIORITY_MIN + 2, 4200000UL },
    { "mlfq-low-2", PRIORITY_MIN, 4000000UL },
};

static volatile int priority_demo_next_index = 0;

static void priority_demo_worker(void)
{
    int idx = __sync_fetch_and_add(&priority_demo_next_index, 1);
    if (idx < 0 || idx >= PRIORITY_DEMO_WORKERS) {
        printf("[PRIORITY-DEMO] worker index out of range (%d)\n", idx);
        exit_process(-1);
    }
    const struct priority_demo_job *job = &priority_demo_jobs[idx];
    printf("[PRIORITY-DEMO] %s (pid=%d) starting workload=%lu\n",
           job->name, myproc()->pid, (unsigned long)job->workload);
    klog(LOG_LEVEL_INFO, "[PRIORITY-DEMO] start name=%s pid=%d prio=%d lvl=%d work=%lu",
         job->name, myproc()->pid, job->priority, priority_to_level(job->priority), (unsigned long)job->workload);
    volatile uint64 counter = 0;
    const uint64 yield_stride = 1UL << 12;
    while (counter < job->workload) {
        counter++;
        if ((counter & (yield_stride - 1)) == 0) {
            yield();
        }
    }
    printf("[PRIORITY-DEMO] %s (pid=%d) finished\n", job->name, myproc()->pid);
    klog(LOG_LEVEL_INFO, "[PRIORITY-DEMO] finish name=%s pid=%d", job->name, myproc()->pid);
    exit_process(0);
}

static void run_priority_mlfq_demo(void)
{
    const int job_count = PRIORITY_DEMO_WORKERS;
    printf("[PRIORITY-DEMO] Spawning %d workers to exercise MLFQ\n", job_count);
    priority_demo_next_index = 0;
    for (int i = 0; i < job_count; i++) {
        const struct priority_demo_job *job = &priority_demo_jobs[i];
        int pid = create_process(priority_demo_worker, job->name);
        if (pid < 0) {
            panic("run_priority_mlfq_demo: failed to create worker");
        }
        if (setpriority(pid, job->priority) < 0) {
            panic("run_priority_mlfq_demo: failed to set worker priority");
        }
        printf("[PRIORITY-DEMO] worker %s pid=%d priority=%d level=%d work=%lu\n",
               job->name, pid, job->priority, priority_to_level(job->priority) ,(unsigned long)job->workload);
        klog(LOG_LEVEL_INFO, "[PRIORITY-DEMO] worker %s pid=%d prio=%d lvl=%d work=%lu",
             job->name, pid, job->priority, priority_to_level(job->priority), (unsigned long)job->workload);
    }

    for (int i = 0; i < job_count; i++) {
        int status = 0;
        int pid = wait_process(&status);
        printf("[PRIORITY-DEMO] worker pid=%d done (status=%d)\n", pid, status);
        klog(LOG_LEVEL_INFO, "[PRIORITY-DEMO] worker pid=%d done status=%d", pid, status);
    }
}
