/* kernel/boot/start.c */
#include "riscv.h"
#include "lib/print.h"
#include "proc/cpu.h"

__attribute__ ((aligned (16))) uint8 CPU_stack[4096 * NCPU];

void start()
{
    unsigned long hartid;
    asm volatile("csrr %0, mhartid" : "=r"(hartid));
    /*
    mhartid是只读CSR，存放“硬件线程编号”
    返回值 hartid == 0叫做bootstrap Processor（BSP）其余为AP
    */
    /* 仅主核初始化打印并输出引导信息 */
    if (hartid == 0) {
        print_init();
        printf("Hello OS\n");
    /* 在裸机里面进行光标移动和字符颜色变化 */
    if (hartid == 0) {
        /* Demo: printf/clear_screen */
        clear_screen();
        set_color(2, -1); /* green text */
        printf("=== OS Lab: Kernel printf & ANSI Demo ===\n"); //This Text should be green
        reset_color();

        printf("cpuid=%d, hex=0x%x, char=%c, str=%s, percent=%%\n", mycpuid(), 0xABC, 'X', "Hello");
        /*验证print_number函数*/
        printf("INT_MIN test: %d, INT_MAX: %d\n", (int)0x80000000, (int)0x7fffffff);

        printf("\nMoving cursor to (6,10) and printing there...\n");
        goto_xy(6, 10);
        printf("Here at (6,10)\n");

        printf("\nClearing screen "); 
        for (int i=3;i>0;i--){ printf("%d ", i); }
        printf("\n");
        clear_screen();
        printf("Screen cleared.\n");

        printf("d: %d %d %d\n", 0, -1, -2147483648);
        printf("x: %x %x\n", 0x12, 0xdeadbeef);
        int *p = (int*)0x80200000;
        printf("p: %p\n", p);
        printf("End of demo.\n");
    }

    /* 如果想让每个核都打印，可以在这里移除 hartid==0 的判断，
    并确保 print.c 中 printf 已有锁保护。 */
    }

    /* 后续：直接进入低功耗等待循环（或进入调度） */
    while (1) {
        asm volatile("wfi");
    }
}
