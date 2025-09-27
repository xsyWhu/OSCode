/* kernel/boot/start.c */
#include "riscv.h"
#include "lib/print.h"
#include "proc/cpu.h"

__attribute__ ((aligned (16))) uint8 CPU_stack[4096 * NCPU];

void start()
{
    unsigned long hartid;
    asm volatile("csrr %0, mhartid" : "=r"(hartid));

    if (hartid == 0) {
        /* 仅主核初始化打印并输出引导信息 */
        print_init();
        puts("Hello OS");
        /* 如果想让每个核都打印，可以在这里移除 hartid==0 的判断，
           并确保 print.c 中 printf 已有锁保护。 */
    }

    /* 后续：直接进入低功耗等待循环（或进入调度） */
    while (1) {
        asm volatile("wfi");
    }
}
//测试有几个核，结果显示有两个
/*void start()
{
    unsigned long hartid;
    asm volatile("csrr %0, mhartid" : "=r"(hartid));

    print_init();          // 让最后一个核初始化即可，见下方“安全做法”
    printf("Hello OS from hartid=%d\n", hartid);
    while (1) asm volatile("wfi");
}*/
