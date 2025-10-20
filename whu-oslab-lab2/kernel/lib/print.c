/* kernel/lib/print.c */
#include <stdarg.h>
#include <stddef.h>
#include "lib/print.h"
#include "dev/uart.h"
#include "lib/lock.h"
#include "dev/console.h"

/* 全局 panic 标志（需要在 BSS 清零后为 0） */
int panicked = 0;

/* 用于保护打印的自旋锁 */
static spinlock_t print_lk;

/* 低级单字符打印 */
// static void print_putc(char c) {
//     /* 在 panic 状态下仍尽量输出 */
//     if (!panicked) {
//         uart_putc_sync((int)c);
//     }
// }

/*低级字符打印（经由console层）*/
static void print_putc(char c) {
    /* 在 panic 状态下仍尽量输出 */
    if (!panicked) {
        console_putc((int)c);
    }
}

/* 初始化打印子系统（只要幂等即可） */
void print_init(void)
{
    //uart_init(); //配置波特率，FIFO，引脚复用等
    console_init(); //初始化控制台,通过console层完成底层UART初始化
    spinlock_init(&print_lk, "print"); 
}

/* 简单的内核 puts */
void puts(const char *s)
{
    if (!s) return;
    spinlock_acquire(&print_lk);
    while (*s) {
        print_putc(*s++);
    }
    spinlock_release(&print_lk);
}

/* 输出无符号数（base 可为 10 或 16） */
static void print_number(unsigned long num, int base, int sign)
{
    char buf[32];
    int i = 0;
    if (num == 0) {
        print_putc('0');
        return;
    }
    if (sign && (long)num < 0) {
        print_putc('-');
        num = (unsigned long)(-(long)num);
    }
    while (num) {
        int d = num % base;
        buf[i++] = "0123456789abcdef"[d];
        num /= base;
    }
    while (i--) print_putc(buf[i]);
}

/* 简单 printf（支持 %s %d %x %p %c %%） */
void printf(const char *fmt, ...) //可变参数函数
{
    va_list ap; //声明一个参数指针
    va_start(ap, fmt); // 让ap指向第一个可变参数，fmt后面的参数

    spinlock_acquire(&print_lk); //自旋锁

    const char *p = fmt;
    while (p && *p) {
        if (*p != '%') {
            print_putc(*p++);
            continue;
        }
        p++;
        if (*p == 's') { //处理%s —— 字符串
            char *s = va_arg(ap, char*);
            if (!s) //s为空，输出NUll
                s = "(null)";
            while (*s) //否则，逐字符输出字符串
                print_putc(*s++);
        } else if (*p == 'd') { //处理十进制
            int d = va_arg(ap, int); //取出一个参数
            /*修复，先转成long再转无符号，保留符号位用于附属判断*/
            //print_number((unsigned long)d, 10, 1);
            print_number((unsigned long)(long)d, 10, 1);
        } else if (*p == 'x') { //十六进制
            unsigned int x = va_arg(ap, unsigned int);
            print_number((unsigned long)x, 16, 0);
        } else if (*p == 'p') {
            unsigned long x = va_arg(ap, unsigned long);
            print_number(x, 16, 0);
        } else if (*p == 'c') { //字符
            int c = va_arg(ap, int);
            print_putc((char)c);
        } else if (*p == '%') {
            print_putc('%');
        } else {
            /* 未知格式，原样输出 */
            print_putc('%');
            if (*p) print_putc(*p);
        }
        if (*p) p++;
    }

    spinlock_release(&print_lk);

    va_end(ap); //清理va_list
}

/* panic 与 assert 实现 */
void panic(const char *s)
{
    panicked = 1;
    /* 在 panic 中尽量不依赖锁（锁可能不安全），直接逐字符写串口 */
    if (s) {
        const char *p = "panic: ";
        while (*p) uart_putc_sync(*p++);
        while (*s) uart_putc_sync(*s++);
        uart_putc_sync('\n');
    } else {
        const char *p = "panic\n";
        while (*p) uart_putc_sync(*p++);
    }
    /* 停在这里 */
    for (;;)
        ;
}

void assert(bool condition, const char* warning)
{
    if (!condition) panic(warning);
}

/* === ANSI-based console helpers === */
static void print_puts(const char* s) { //不拿锁，在panic上下文也能用
    while (*s) 
        uart_putc_sync(*s++); 
}

void clear_screen(void) {
    /* ESC[2J clears the screen; ESC[H moves cursor to home */
    print_puts("\x1b[2J\x1b[H");
}

void goto_xy(int row, int col) {
    /* 1-based rows/cols */
    char buf[32];
    int r = 0, i = 0;
    if (row < 1) row = 1;
    if (col < 1) col = 1;
    /* construct ESC[row;colH */
    buf[r++] = 0x1b; buf[r++] = '[';
    /* simple itoa */
    int n = row, d[10]; if (n==0) d[i++]=0; else while (n>0){ d[i++]=n%10; n/=10; }
    for (int k=i-1;k>=0;k--) buf[r++] = '0'+d[k];
    buf[r++] = ';'; i = 0;
    n = col; if (n==0) d[i++]=0; else while (n>0){ d[i++]=n%10; n/=10; }
    for (int k=i-1;k>=0;k--) buf[r++] = '0'+d[k];
    buf[r++] = 'H';
    buf[r] = 0;
    print_puts(buf);
}

void set_color(int fg, int bg) {
    /* ESC[3xm for fg, ESC[4xm for bg; x in 0..7 */
    char buf[32];
    if (fg < 0) fg = 7;
    if (bg < 0) bg = 0;
    int r = 0;
    buf[r++] = 0x1b; buf[r++] = '['; buf[r++] = '3';
    buf[r++] = '0' + (fg % 8);
    buf[r++] = 'm';
    buf[r++] = 0x1b; buf[r++] = '['; buf[r++] = '4';
    buf[r++] = '0' + (bg % 8);
    buf[r++] = 'm';
    buf[r] = 0;
    print_puts(buf);
}

void reset_color(void) {
    print_puts("\x1b[0m");
}