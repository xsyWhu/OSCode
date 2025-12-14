#include "user/user.h"

static void println(const char *s)
{
    write(1, s, strlen(s));
    write(1, "\n", 1);
}

static void busy(int rounds)
{
    volatile int x = 0;
    for (int i = 0; i < rounds; i++) {
        x += i;
    }
    (void)x;
}

int
main(void)
{
    println("[rtdemo] start EDF showcase");
    struct {
        int deadline;
        const char *name;
    } jobs[] = {
        { 50,  "rt-fast" },
        { 150, "rt-mid" },
        { 300, "rt-slow" },
    };
    for (int i = 0; i < 3; i++) {
        int pid = fork();
        if (pid < 0) {
            println("[rtdemo] fork failed");
            return -1;
        }
        if (pid == 0) {
            setrealtime(getpid(), jobs[i].deadline);
            write(1, "[rtdemo-child] ", 15);
            write(1, jobs[i].name, strlen(jobs[i].name));
            write(1, " deadline=", 11);
            char buf[16];
            int d = jobs[i].deadline;
            int idx = 0;
            while (d > 0 && idx < (int)sizeof(buf)) {
                buf[idx++] = '0' + (d % 10);
                d /= 10;
            }
            for (int k = 0; k < idx / 2; k++) {
                char t = buf[k];
                buf[k] = buf[idx - 1 - k];
                buf[idx - 1 - k] = t;
            }
            write(1, buf, idx);
            write(1, "\n", 1);
            busy(800000);
            write(1, "[rtdemo-child] done ", 21);
            write(1, jobs[i].name, strlen(jobs[i].name));
            write(1, "\n", 1);
            exit(0);
        }
    }
    for (int i = 0; i < 3; i++) {
        int st = 0;
        int pid = wait(&st);
        write(1, "[rtdemo-parent] wait pid=", 26);
        char buf[16]; int idx=0; int v=pid;
        if (v==0) buf[idx++]='0'; else { while(v>0&&idx<(int)sizeof(buf)){buf[idx++]= '0'+(v%10); v/=10;} for(int k=0;k<idx/2;k++){char t=buf[k];buf[k]=buf[idx-1-k];buf[idx-1-k]=t;} }
        write(1, buf, idx);
        write(1, " status=", 8);
        idx=0; v=st; if(v==0) buf[idx++]='0'; else { if(v<0){write(1,"-",1); v=-v;} while(v>0&&idx<(int)sizeof(buf)){buf[idx++]= '0'+(v%10); v/=10;} for(int k=0;k<idx/2;k++){char t=buf[k];buf[k]=buf[idx-1-k];buf[idx-1-k]=t;} }
        write(1, buf, idx);
        write(1, "\n", 1);
    }
    println("[rtdemo] done");
    return 0;
}
