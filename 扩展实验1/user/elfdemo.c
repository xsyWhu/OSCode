#include "user/user.h"

static void put_hex(uint64 v)
{
    char buf[17];
    const char *hex = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        buf[15 - i] = hex[v & 0xF];
        v >>= 4;
    }
    buf[16] = '\0';
    write(1, buf, 16);
}

static void put_dec(int v)
{
    char buf[16];
    int idx = 0;
    int x = v;
    if (x == 0) {
        buf[idx++] = '0';
    } else {
        if (x < 0) {
            write(1, "-", 1);
            x = -x;
        }
        while (x > 0 && idx < (int)sizeof(buf)) {
            buf[idx++] = '0' + (x % 10);
            x /= 10;
        }
    }
    for (int i = 0; i < idx / 2; i++) {
        char t = buf[i];
        buf[i] = buf[idx - 1 - i];
        buf[idx - 1 - i] = t;
    }
    write(1, buf, idx);
}

static void puts_ln(const char *s)
{
    write(1, s, strlen(s));
    write(1, "\n", 1);
}

static int data_var = 42;
static char msg[] = "ELF data segment OK";

int
main(int argc, char **argv)
{
    puts_ln("[elfdemo] hello from ELF loader");
    write(1, "[elfdemo] pid=", 15);
    put_dec(getpid());
    write(1, " argc=", 6);
    put_dec(argc);
    write(1, " argv0=", 7);
    if (argc > 0 && argv && argv[0]) {
        write(1, argv[0], strlen(argv[0]));
    }
    write(1, "\n", 1);

    write(1, "[elfdemo] data_var=", 20);
    put_dec(data_var);
    write(1, " msg=", 5);
    write(1, msg, strlen(msg));
    write(1, "\n", 1);

    write(1, "[elfdemo] code addr=0x", 23);
    put_hex((uint64)main);
    write(1, " data addr=0x", 13);
    put_hex((uint64)&data_var);
    write(1, "\n", 1);

    return 0;
}
