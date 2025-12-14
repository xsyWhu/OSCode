#include "user/user.h"

static void write_str(const char *s)
{
    int len = 0;
    while (s[len])
        len++;
    if (len > 0) {
        write(1, s, len);
    }
}

static void write_dec(int v)
{
    char buf[16];
    int idx = 0;
    if (v == 0) {
        buf[idx++] = '0';
    } else {
        if (v < 0) {
            write(1, "-", 1);
            v = -v;
        }
        while (v > 0 && idx < (int)sizeof(buf)) {
            buf[idx++] = '0' + (v % 10);
            v /= 10;
        }
        for (int i = 0; i < idx / 2; i++) {
            char t = buf[i];
            buf[i] = buf[idx - 1 - i];
            buf[idx - 1 - i] = t;
        }
    }
    if (idx > 0) {
        write(1, buf, idx);
    }
}

static int parse_int(const char *s)
{
    int neg = 0;
    int val = 0;
    if (*s == '-') {
        neg = 1;
        s++;
    }
    while (*s) {
        if (*s < '0' || *s > '9')
            break;
        val = val * 10 + (*s - '0');
        s++;
    }
    return neg ? -val : val;
}

int
main(int argc, char **argv)
{
    if (argc != 2 && argc != 3) {
        write_str("Usage: nice pid [priority]\n");
        exit(-1);
    }

    int pid = parse_int(argv[1]);

    if (argc == 2) {
        int prio = getpriority(pid);
        if (prio < 0) {
            write_str("getpriority failed\n");
            exit(-1);
        }
        write_str("pid ");
        write_dec(pid);
        write_str(" priority=");
        write_dec(prio);
        write_str("\n");
        exit(0);
    }

    int prio = parse_int(argv[2]);
    int rc = setpriority(pid, prio);
    if (rc < 0) {
        write_str("setpriority failed\n");
        exit(-1);
    }
    write_str("set pid ");
    write_dec(pid);
    write_str(" priority to ");
    write_dec(prio);
    write_str("\n");
    exit(0);
}
