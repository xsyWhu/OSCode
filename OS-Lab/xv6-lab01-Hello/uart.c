#define UART0 0x10000000L
#define RHR   (UART0 + 0)
#define THR   (UART0 + 0)
#define LSR   (UART0 + 5)

static inline void outb(unsigned long addr, unsigned char val) {
    *(volatile unsigned char *)addr = val;
}

static inline unsigned char inb(unsigned long addr) {
    return *(volatile unsigned char *)addr;
}

void uart_putc(char c) {
    while ((inb(LSR) & 0x20) == 0);  // 等待 THR 空
    outb(THR, c);
}

void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}
