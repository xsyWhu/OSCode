#include "ipc/msg.h"
#include "lib/string.h"
#include "proc/proc.h"

static struct msgqueue queues[MSG_MAX_QUEUES];

void msg_init(void)
{
    for (int i = 0; i < MSG_MAX_QUEUES; i++) {
        spinlock_init(&queues[i].lock, "msgq");
        queues[i].used = 0;
        queues[i].key = 0;
        queues[i].head = queues[i].tail = queues[i].count = 0;
        memset(queues[i].msgs, 0, sizeof(queues[i].msgs));
    }
}

static struct msgqueue* get_queue(int qid)
{
    if (qid < 0 || qid >= MSG_MAX_QUEUES)
        return NULL;
    return &queues[qid];
}

int msg_get(int key)
{
    // Try to find existing queue with same key
    for (int i = 0; i < MSG_MAX_QUEUES; i++) {
        struct msgqueue *q = &queues[i];
        spinlock_acquire(&q->lock);
        if (q->used && q->key == key) {
            spinlock_release(&q->lock);
            return i;
        }
        spinlock_release(&q->lock);
    }

    // Allocate a new one
    for (int i = 0; i < MSG_MAX_QUEUES; i++) {
        struct msgqueue *q = &queues[i];
        spinlock_acquire(&q->lock);
        if (!q->used) {
            q->used = 1;
            q->key = key;
            q->head = q->tail = q->count = 0;
            spinlock_release(&q->lock);
            return i;
        }
        spinlock_release(&q->lock);
    }
    return -1;
}

int msg_send(int qid, const char *data, int len)
{
    if (len < 0 || len > MSG_MAX_SIZE) {
        return -1;
    }
    struct msgqueue *q = get_queue(qid);
    if (!q)
        return -1;

    spinlock_acquire(&q->lock);
    while (q->used && q->count == MSG_QUEUE_DEPTH) {
        sleep(q, &q->lock);
    }
    if (!q->used) {
        spinlock_release(&q->lock);
        return -1;
    }
    struct message *m = &q->msgs[q->tail];
    m->len = len;
    if (len > 0) {
        memmove(m->data, data, len);
    }
    q->tail = (q->tail + 1) % MSG_QUEUE_DEPTH;
    q->count++;
    wakeup(q);
    spinlock_release(&q->lock);
    return len;
}

int msg_recv(int qid, char *data, int maxlen)
{
    if (maxlen <= 0) {
        return -1;
    }
    struct msgqueue *q = get_queue(qid);
    if (!q)
        return -1;

    spinlock_acquire(&q->lock);
    while (q->used && q->count == 0) {
        sleep(q, &q->lock);
    }
    if (!q->used) {
        spinlock_release(&q->lock);
        return -1;
    }
    struct message *m = &q->msgs[q->head];
    int n = m->len;
    if (n > maxlen) {
        n = maxlen;
    }
    if (n > 0) {
        memmove(data, m->data, n);
    }
    q->head = (q->head + 1) % MSG_QUEUE_DEPTH;
    q->count--;
    wakeup(q);
    spinlock_release(&q->lock);
    return n;
}
