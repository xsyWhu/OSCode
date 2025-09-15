void uart_puts(const char *s);
const char msg[] = "Hello 05\n";

void main(void) {
    uart_puts(msg);
    while (1) { }
}
