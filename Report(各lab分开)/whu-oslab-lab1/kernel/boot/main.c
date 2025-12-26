/* kernel/boot/main.c */
#include "riscv.h"
#include "lib/print.h"
#include "proc/cpu.h"

int main()
{
    /* main 在这个实验中通常不会被调用（start 会直接进入），
       这个文件保留一个基本实现以防构建依赖。 */
    print_init();
    puts("Hello OS from main\n");
    while (1) ;
    return 0;
}
