#include "user/user.h"

#ifndef ENABLE_LOGREAD
#define ENABLE_LOGREAD 0
#endif

#define PRIORITY_BUSY_ITERS (20000000UL)
#define PRIORITY_PARENT_SPIN (4000000UL)
#define PRIORITY_MIN 0
#define PRIORITY_MAX 10

static void write_str(const char *s)
{
    int len = 0;
    while (s[len])
        len++;
    if (len > 0) {
        write(1, s, len);
    }
}

static void write_dec(int value)
{
    char buf[16];
    int idx = 0;
    if (value == 0) {
        buf[idx++] = '0';
    } else {
        if (value < 0) {
            write(1, "-", 1);
            value = -value;
        }
        while (value > 0 && idx < (int)sizeof(buf)) {
            buf[idx++] = '0' + (value % 10);
            value /= 10;
        }
        for (int i = 0; i < idx / 2; i++) {
            char tmp = buf[i];
            buf[i] = buf[idx - 1 - i];
            buf[idx - 1 - i] = tmp;
        }
    }
    if (idx > 0) {
        write(1, buf, idx);
    }
}

static void busy_child(const char *label)
{
    write_str(label);
    write_str(" started (pid=");
    write_dec(getpid());
    write_str(")\n");

    volatile unsigned long counter = 0;
    while (counter < PRIORITY_BUSY_ITERS) {
        counter++;
    }

    write_str(label);
    write_str(" exiting\n");
    exit(0);
}

static void priority_test(void)
{
    int high = fork();
    if (high < 0) {
        write_str("[priority_test] fork failed\n");
        exit(1);
    }
    if (high == 0) {
        busy_child("[priority_test-high]");
    }

    int high_rc = setpriority(high, PRIORITY_MAX);
    write_str("[priority_test] setpriority(high) -> ");
    write_dec(high_rc);
    write_str("\n");

    int low = fork();
    if (low < 0) {
        write_str("[priority_test] second fork failed\n");
        exit(1);
    }
    if (low == 0) {
        busy_child("[priority_test-low]");
    }

    int low_rc = setpriority(low, PRIORITY_MIN);
    write_str("[priority_test] setpriority(low) -> ");
    write_dec(low_rc);
    write_str("\n");

    write_str("[priority_test] spinning parent before wait\n");
    volatile unsigned long spin = 0;
    while (spin < PRIORITY_PARENT_SPIN) {
        spin++;
    }

    int bad_rc = setpriority(-999, PRIORITY_MAX);
    write_str("[priority_test] setpriority(bad) -> ");
    write_dec(bad_rc);
    write_str("\n");

    for (int i = 0; i < 2; i++) {
        int status = 0;
        int child = wait(&status);
        write_str("[priority_test] wait returned pid=");
        write_dec(child);
        write_str(" status=");
        write_dec(status);
        write_str("\n");
    }

    write_str("[priority_test] done\n");
}

static void run_elfdemo(void)
{
    write_str("[init] running elfdemo (ELF loader test)\n");
    int pid = fork();
    if (pid < 0) {
        write_str("[init] fork elfdemo failed\n");
        return;
    }
    if (pid == 0) {
        const char *argv[] = { "elfdemo", "arg1", "arg2", 0 };
        exec("/elfdemo", (char**)argv);
        write_str("exec elfdemo failed\n");
        exit(-1);
    }
    int status = 0;
    int w = wait(&status);
    write_str("[init] elfdemo wait pid=");
    write_dec(w);
    write_str(" status=");
    write_dec(status);
    write_str("\n");
}

static void run_msgdemo(void)
{
    write_str("[init] running msgdemo (IPC message queue test)\n");
    int pid = fork();
    if (pid < 0) {
        write_str("[init] fork msgdemo failed\n");
        return;
    }
    if (pid == 0) {
        const char *argv[] = { "msgdemo", 0 };
        exec("/msgdemo", (char**)argv);
        write_str("exec msgdemo failed\n");
        exit(-1);
    }
    int status = 0;
    int w = wait(&status);
    write_str("[init] msgdemo wait pid=");
    write_dec(w);
    write_str(" status=");
    write_dec(status);
    write_str("\n");
}

int
main(void)
{
#if ENABLE_LOGREAD
    int lr = fork();
    if (lr == 0) {
        const char *argv[] = { "logread", 0 };
        exec("/logread", (char**)argv);
        write_str("exec logread failed\n");
        exit(-1);
    }
    (void)lr;
#endif
    write_str("[init] running priority syscall test\n");
    priority_test();
    run_elfdemo();
    run_msgdemo();
    exit(0);
}
