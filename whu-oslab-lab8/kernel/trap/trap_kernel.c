#include "lib/print.h"
#include "dev/timer.h"
#include "dev/uart.h"
#include "dev/plic.h"
#include "dev/virtio_disk.h"
#include "trap/trap.h"
#include "proc/proc.h"
#include "memlayout.h"
#include "mem/vmem.h"
#include "syscall.h"
#include "riscv.h"

struct kernel_trapinfo {
    uint64 sepc;
    uint64 sstatus;
    uint64 scause;
    uint64 stval;
};

extern char trampoline[];
extern char uservec[];
extern char userret[];

static void usertrap(void);
void usertrapret(void);
static int handle_interrupt(uint64 scause);

// 中断信息
static char* interrupt_info[16] = {
    "U-mode software interrupt",      // 0
    "S-mode software interrupt",      // 1
    "reserved-1",                     // 2
    "M-mode software interrupt",      // 3
    "U-mode timer interrupt",         // 4
    "S-mode timer interrupt",         // 5
    "reserved-2",                     // 6
    "M-mode timer interrupt",         // 7
    "U-mode external interrupt",      // 8
    "S-mode external interrupt",      // 9
    "reserved-3",                     // 10
    "M-mode external interrupt",      // 11
    "reserved-4",                     // 12
    "reserved-5",                     // 13
    "reserved-6",                     // 14
    "reserved-7",                     // 15
};

// 设备中断处理函数类型在 trap.h 里已经 typedef 了

#define MAX_IRQ 64
static interrupt_handler_t irq_table[MAX_IRQ];

// 异常信息
static char* exception_info[16] = {
    "Instruction address misaligned", // 0
    "Instruction access fault",       // 1
    "Illegal instruction",            // 2
    "Breakpoint",                     // 3
    "Load address misaligned",        // 4
    "Load access fault",              // 5
    "Store/AMO address misaligned",   // 6
    "Store/AMO access fault",         // 7
    "Environment call from U-mode",   // 8
    "Environment call from S-mode",   // 9
    "reserved-1",                     // 10
    "Environment call from M-mode",   // 11
    "Instruction page fault",         // 12
    "Load page fault",                // 13
    "reserved-2",                     // 14
    "Store/AMO page fault",           // 15
};

void register_interrupt(int irq, interrupt_handler_t handler)
{
    if (irq < 0 || irq >= MAX_IRQ) {
        return;
    }
    irq_table[irq] = handler;
}

// 对当前 hart 使能/屏蔽某个外设中断
void enable_interrupt(int irq)
{
    if (irq < 0 || irq >= 32) { // PLIC_SENABLE 是 32bit
        return;
    }
    int hartid = mycpuid();
    volatile uint32 *senable = (uint32*)PLIC_SENABLE(hartid);
    uint32 mask = (1u << irq);
    *senable |= mask;
}

void disable_interrupt(int irq)
{
    if (irq < 0 || irq >= 32) {
        return;
    }
    int hartid = mycpuid();
    volatile uint32 *senable = (uint32*)PLIC_SENABLE(hartid);
    uint32 mask = (1u << irq);
    *senable &= ~mask;
}

// in trap.S
// 内核中断处理流程
extern void kernel_vector();

//“新的”初始化接口
void trap_init(void)
{
    // 全局一次性初始化
    timer_create();
    plic_init();

    // 初始化中断向量表
    for (int i = 0; i < MAX_IRQ; i++) {
        irq_table[i] = 0;
    }

    // 注册 UART 的中断处理函数
    register_interrupt(UART_IRQ, uart_intr);
    register_interrupt(VIRTIO_IRQ, virtio_disk_intr);
}

void trap_inithart(void)
{
    // 设置内核态 trap 入口地址
    w_stvec((uint64)kernel_vector);

    // PLIC 本地初始化（只负责阈值等，不再硬编码开启某个设备）
    plic_inithart();

    // 为当前 hart 使能 UART 外设中断
    enable_interrupt(UART_IRQ);
    enable_interrupt(VIRTIO_IRQ);
}

// 保留原名字做一层兼容封装（其他文件还用的话也能编）
void trap_kernel_init(void)
{
    trap_init();
}

void trap_kernel_inithart(void)
{
    trap_inithart();
}

// // 外设中断处理 (基于PLIC)
// void external_interrupt_handler()
// {
//     int irq = plic_claim();
    
//     if(irq == 0) {
//         // irq为0表示没有待处理的中断(不应该发生)
//         //printf("Warning: spurious external interrupt\n");
//         return;
//     }

//     switch(irq) {
//         case UART_IRQ:
//             // 处理UART中断
//             uart_intr();
//             break;
//         /*    
//         case VIRTIO_IRQ:
//             // 处理VIRTIO磁盘中断
//             // virtio_disk_intr();
//             break;
//         */    
//         default:
//             // 未知的外设中断
//             printf("Unknown external interrupt: irq=%d\n", irq);
//             break;
//     }
    
    
//     plic_complete(irq);
// }

// 异常处理函数：统一放到这里，trap_kernel_handler 只负责分发
static void handle_exception(struct kernel_trapinfo *tf)
{
    uint64 cause = tf->scause;
    int exception_id = cause & 0xf;

    // 打印人类可读的信息
    printf("Exception in kernel: %s\n", exception_info[exception_id]);
    printf("sepc=%p stval=%p\n", tf->sepc, tf->stval);
    struct proc *p = myproc();
    if (p) {
        printf("proc pid=%d state=%d name=%s entry=%p kstack=%p ctx.ra=%p ctx.sp=%p\n",
               p->pid, p->state, p->name, p->entry, p->kstack, p->ctx.ra, p->ctx.sp);
    } else {
        printf("proc pid=<none>\n");
    }

    switch (cause) {
        case 2:  // Illegal instruction
            panic("Illegal instruction in kernel");
            break;

        case 12: // Instruction page fault
        case 13: // Load page fault
        case 15: // Store page fault
            panic("Page fault in kernel");
            break;

        default:
            // 暂时还没有用户态/系统调用，这里一律视为致命错误
            panic("Unexpected exception in kernel");
    }
}

void external_interrupt_handler(void)
{
    int irq = plic_claim();
    
    if (irq == 0) {
        // irq为0表示没有待处理的中断(不应该发生)
        // printf("Warning: spurious external interrupt\n");
        return;
    }

    // 优先走“注册表”
    if (irq > 0 && irq < MAX_IRQ && irq_table[irq]) {
        irq_table[irq]();
    } else {
        // 如果没有注册，就打印一下方便调试
        printf("Unknown external interrupt: irq=%d\n", irq);
    }
    
    plic_complete(irq);
}

static volatile int interrupt_count = 0;
static volatile int last_print_count = 0;

// 时钟中断处理 (基于CLINT)
void timer_interrupt_handler()
{
    // 只在CPU 0上更新系统时钟
    if(mycpuid() == 0) {
        timer_update();
    }

    struct proc *p = myproc();
    if (p && proc_tick()) {
        yield();
    }

    proc_age();
    static int boost_counter = 0;
    if (mycpuid() == 0) {
        boost_counter++;
        if (boost_counter >= 64) {
            boost_counter = 0;
            proc_boost();
        }
    }
}

static int handle_interrupt(uint64 scause)
{
    if ((scause & (1UL << 63)) == 0) {
        return 0;
    }

    int interrupt_id = scause & 0xf;
    switch (interrupt_id) {
    case 1:
        // S-mode 软件中断（时钟）
        w_sip(r_sip() & ~2);
        timer_interrupt_handler();
        return 1;
    case 9:
        external_interrupt_handler();
        return 1;
    default:
        printf("Unknown interrupt: %s (id=%d)\n",
               interrupt_info[interrupt_id], interrupt_id);
        return 0;
    }
}

// 在kernel_vector()里面调用
// 内核态trap处理的核心逻辑
void trap_kernel_handler(void)
{
    // 先把几个关键 CSR 摘出来，打包成一个 trapframe
    struct kernel_trapinfo tf;
    tf.sepc    = r_sepc();      // 发生 trap 时的 PC
    tf.sstatus = r_sstatus();   // 与特权模式和中断相关的状态
    tf.scause  = r_scause();    // trap 原因
    tf.stval   = r_stval();     // 附加信息（例如 fault 地址）

    // 确认 trap 来自 S-mode 且此时 SIE 关着（防止嵌套乱套）
    assert(tf.sstatus & SSTATUS_SPP,
           "trap_kernel_handler: not from s-mode");
    assert(intr_get() == 0,
           "trap_kernel_handler: interrupt enabled");

    // 判断是中断还是异常
    if (tf.scause & (1UL << 63)) {
        handle_interrupt(tf.scause);
    } else {
        // 最高位为 0：异常，统一交给 handle_exception
        handle_exception(&tf);
    }

    // 恢复现场：目前我们没有在 C 里修改 sepc/sstatus，
    // 但以后如果在 handle_exception 里改了（比如跳过 ecall），
    // 这里写回的就是修改后的值
    w_sepc(tf.sepc);
    w_sstatus(tf.sstatus);
}

static void usertrap(void)
{
    struct proc *p = myproc();
    if (!p || !p->trapframe) {
        panic("usertrap: no process");
    }
    uint64 sstatus = r_sstatus();
    if (sstatus & SSTATUS_SPP) {
        panic("usertrap: not from user mode");
    }

    // trap 返回内核，使用内核向量
    w_stvec((uint64)kernel_vector);

    p->trapframe->epc = r_sepc();
    uint64 scause = r_scause();

    if (scause == 8) {
        if (p->killed) {
            exit_process(-1);
        }
        p->trapframe->epc += 4; // 跳过 ecall
        intr_on();
        syscall();
    } else if (handle_interrupt(scause)) {
        // 设备中断已处理
    } else if (scause == 13 || scause == 15) { // load/store page fault
        uint64 stval = r_stval();
        if (cow_handle(p->pagetable, stval) < 0) {
            printf("usertrap: page fault va=%p pid=%d\n", stval, p->pid);
            p->killed = 1;
        }
    } else {
        uint64 stval = r_stval();
        printf("usertrap: unexpected scause=%p stval=%p pid=%d\n",
               scause, stval, p->pid);
        p->killed = 1;
    }

    if (p->killed) {
        exit_process(-1);
    }

    usertrapret();
}

void usertrapret(void)
{
    struct proc *p = myproc();
    if (!p || !p->trapframe || !p->pagetable) {
        panic("usertrapret: invalid process state");
    }

    intr_off();

    uint64 trampoline_uservec = TRAMPOLINE + ((uint64)uservec - (uint64)trampoline);
    uint64 trampoline_userret = TRAMPOLINE + ((uint64)userret - (uint64)trampoline);

    w_stvec(trampoline_uservec);

    struct trapframe *tf = p->trapframe;
    tf->kernel_satp = r_satp();
    tf->kernel_sp = p->kstack + PGSIZE;
    tf->kernel_trap = (uint64)usertrap;
    tf->kernel_hartid = r_tp();

    uint64 x = r_sstatus();
    x &= ~SSTATUS_SPP;  // 将 SPP 设为用户态
    x |= SSTATUS_SPIE;  // 开启下一次 sret 后的中断
    w_sstatus(x);

    w_sepc(tf->epc);

    uint64 user_satp = MAKE_SATP(p->pagetable);
    void (*enter_user)(uint64) = (void (*)(uint64))trampoline_userret;
    enter_user(user_satp);

    panic("usertrapret: unreachable");
}
