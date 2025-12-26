#include "syscall.h"
#include "ipc/msg.h"
#include "proc/proc.h"
#include "mem/vmem.h"

int sys_msgget(void)
{
    int key;
    if (argint(0, &key) < 0) {
        return -1;
    }
    return msg_get(key);
}

int sys_msgsend(void)
{
    int qid, len;
    uint64 uaddr;
    if (argint(0, &qid) < 0 || argaddr(1, &uaddr) < 0 || argint(2, &len) < 0) {
        return -1;
    }
    if (len < 0 || len > MSG_MAX_SIZE) {
        return -1;
    }
    char buf[MSG_MAX_SIZE];
    if (len > 0) {
        struct proc *p = myproc();
        if (!p || copyin(p->pagetable, buf, uaddr, len) < 0) {
            return -1;
        }
    }
    return msg_send(qid, buf, len);
}

int sys_msgrecv(void)
{
    int qid, maxlen;
    uint64 uaddr;
    if (argint(0, &qid) < 0 || argaddr(1, &uaddr) < 0 || argint(2, &maxlen) < 0) {
        return -1;
    }
    if (maxlen <= 0) {
        return -1;
    }
    if (maxlen > MSG_MAX_SIZE) {
        maxlen = MSG_MAX_SIZE;
    }
    char buf[MSG_MAX_SIZE];
    int n = msg_recv(qid, buf, maxlen);
    if (n < 0) {
        return -1;
    }
    struct proc *p = myproc();
    if (!p || copyout(p->pagetable, uaddr, buf, n) < 0) {
        return -1;
    }
    return n;
}
