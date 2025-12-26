#include "user/user.h"

static void println(const char *s)
{
    write(1, s, strlen(s));
    write(1, "\n", 1);
}

int
main(void)
{
    int key = 1234;
    int qid = msgget(key);
    if (qid < 0) {
        println("[msgdemo] msgget failed");
        return -1;
    }
    println("[msgdemo] queue created");

    int pid = fork();
    if (pid < 0) {
        println("[msgdemo] fork failed");
        return -1;
    }
    if (pid == 0) {
        // child: receiver
        char buf[64];
        int n = msgrecv(qid, buf, sizeof(buf));
        if (n < 0) {
            println("[msgdemo-child] msgrecv failed");
            exit(-1);
        }
        buf[n] = 0;
        write(1, "[msgdemo-child] got: ", 23);
        write(1, buf, n);
        write(1, "\n", 1);
        exit(0);
    } else {
        const char *msg = "hello-from-parent";
        int n = msgsend(qid, msg, strlen(msg));
        if (n < 0) {
            println("[msgdemo-parent] msgsend failed");
        } else {
            println("[msgdemo-parent] sent message");
        }
        int st = 0;
        wait(&st);
        write(1, "[msgdemo-parent] child exit status=", 36);
        char numbuf[16];
        int v = st;
        int idx = 0;
        if (v == 0) {
            numbuf[idx++] = '0';
        } else {
            if (v < 0) v = -v;
            while (v > 0 && idx < (int)sizeof(numbuf)) {
                numbuf[idx++] = '0' + (v % 10);
                v /= 10;
            }
            for (int i = 0; i < idx / 2; i++) {
                char t = numbuf[i];
                numbuf[i] = numbuf[idx - 1 - i];
                numbuf[idx - 1 - i] = t;
            }
        }
        write(1, numbuf, idx);
        write(1, "\n", 1);
    }
    return 0;
}
