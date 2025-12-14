#include "user/user.h"

static void println(const char *s)
{
    write(1, s, strlen(s));
    write(1, "\n", 1);
}

int
main(void)
{
    char *buf = sbrk(4096);
    if (buf == (char*)-1) {
        println("[cowtest] sbrk failed");
        return -1;
    }
    buf[0] = 'A';
    buf[1] = 0;

    int pid = fork();
    if (pid < 0) {
        println("[cowtest] fork failed");
        return -1;
    }

    if (pid == 0) {
        // child writes; should not affect parent
        buf[0] = 'C';
        println("[cowtest-child] wrote C");
        exit(0);
    }

    int status = 0;
    wait(&status);
    write(1, "[cowtest-parent] buf after child=", 33);
    write(1, buf, 1);
    write(1, "\n", 1);
    int ok = (buf[0] == 'A');
    if (ok) {
        println("[cowtest-parent] PASS");
    } else {
        println("[cowtest-parent] FAIL");
    }
    buf[0] = 'P';
    println("[cowtest-parent] wrote P");
    return ok ? 0 : -1;
}
