/* kernel/boot/start.c */
#include "riscv.h"
#include "lib/print.h"

void main(); // 声明内核主函数
void timerinit(); // 声明定时器初始化函数

__attribute__ ((aligned (16))) uint8 CPU_stack[4096 * NCPU];

void start()
{
  unsigned long x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK;
  x |= MSTATUS_MPP_S;
  w_mstatus(x);

  w_mepc((uint64)main);

  w_satp(0);

  w_medeleg(0xffff);
  w_mideleg(0xffff);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE);

  w_pmpaddr0(0x3fffffffffffffull);
  w_pmpcfg0(0xf);

  timerinit();

  int id = r_mhartid();
  w_tp(id);

  asm volatile("mret");
}

void
timerinit()
{
  // enable supervisor-mode timer interrupts.
  w_mie(r_mie() | MIE_STIE);
  
  // enable the sstc extension (i.e. stimecmp).
  w_menvcfg(r_menvcfg() | (1L << 63)); 
  
  // allow supervisor to use stimecmp and time.
  w_mcounteren(r_mcounteren() | 2);
  
  // ask for the very first timer interrupt.
  w_stimecmp(r_time() + 1000000);
}