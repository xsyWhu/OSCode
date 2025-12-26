#ifndef __IPC_MSG_H__
#define __IPC_MSG_H__

#include "common.h"
#include "lib/lock.h"

#define MSG_MAX_SIZE      128
#define MSG_QUEUE_DEPTH   16
#define MSG_MAX_QUEUES    32

struct message {
    int len;
    char data[MSG_MAX_SIZE];
};

struct msgqueue {
    struct spinlock lock;
    int key;
    int used;
    int head;
    int tail;
    int count;
    struct message msgs[MSG_QUEUE_DEPTH];
};

void msg_init(void);
int msg_get(int key);
int msg_send(int qid, const char *data, int len);
int msg_recv(int qid, char *data, int maxlen);

#endif
