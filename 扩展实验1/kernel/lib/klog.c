#include "lib/klog.h"
#include "lib/lock.h"
#include "lib/string.h"
#include <stdarg.h>

struct klog_buffer klog_buf;

void klog_init(void)
{
    spinlock_init(&klog_buf.lock, "klog");
    klog_buf.read_pos = 0;
    klog_buf.write_pos = 0;
    klog_buf.level = LOG_LEVEL_INFO;
    klog_buf.dropped = 0;
}

static void klog_write_bytes(const char *buf, int len)
{
    for (int i = 0; i < len; i++) {
        klog_buf.buf[klog_buf.write_pos] = buf[i];
        klog_buf.write_pos = (klog_buf.write_pos + 1) % LOG_BUF_SIZE;
        if (klog_buf.write_pos == klog_buf.read_pos) {
            klog_buf.read_pos = (klog_buf.read_pos + 1) % LOG_BUF_SIZE;
            klog_buf.dropped++;
        }
    }
}

static void klog_putc(char *buf, int *idx, int max, char c)
{
    if (*idx < max - 1) {
        buf[*idx] = c;
        (*idx)++;
    }
}

static void klog_vprintf(int level, const char *fmt, va_list ap)
{
    char tmp[MAX_LOG_LEN];
    int idx = 0;
    for (const char *p = fmt; *p && idx < MAX_LOG_LEN - 1; p++) {
        if (*p != '%') {
            klog_putc(tmp, &idx, MAX_LOG_LEN, *p);
            continue;
        }
        p++;
        if (!*p)
            break;
        if (*p == 's') {
            const char *s = va_arg(ap, const char*);
            if (!s)
                s = "(null)";
            while (*s && idx < MAX_LOG_LEN - 1) {
                klog_putc(tmp, &idx, MAX_LOG_LEN, *s++);
            }
        } else if (*p == 'd' || *p == 'x' || *p == 'p' || *p == 'u') {
            uint64 v = (*p == 'd') ? (uint64)va_arg(ap, int) :
                        (*p == 'u') ? (uint64)va_arg(ap, unsigned int) :
                        va_arg(ap, uint64);
            char buf[32];
            int base = (*p == 'x' || *p == 'p') ? 16 : 10;
            int neg = 0;
            if (*p == 'd' && (int)v < 0) {
                neg = 1;
                v = (uint64)(-(int)v);
            }
            int bi = 0;
            do {
                int digit = v % base;
                buf[bi++] = (digit < 10) ? '0' + digit : 'a' + (digit - 10);
                v /= base;
            } while (v && bi < (int)sizeof(buf));
            if (neg && bi < (int)sizeof(buf)) {
                buf[bi++] = '-';
            }
            while (bi > 0 && idx < MAX_LOG_LEN - 1) {
                klog_putc(tmp, &idx, MAX_LOG_LEN, buf[--bi]);
            }
        } else if (*p == 'l') {
            // Minimal support for %lu (unsigned long).
            p++;
            if (*p == 'u') {
                uint64 v = va_arg(ap, uint64);
                char buf[32];
                int bi = 0;
                do {
                    buf[bi++] = '0' + (v % 10);
                    v /= 10;
                } while (v && bi < (int)sizeof(buf));
                while (bi > 0 && idx < MAX_LOG_LEN - 1) {
                    klog_putc(tmp, &idx, MAX_LOG_LEN, buf[--bi]);
                }
            }
        } else if (*p == '%') {
            klog_putc(tmp, &idx, MAX_LOG_LEN, '%');
        }
    }
    // Ensure each klog entry ends with a newline to keep logread output readable.
    if (idx < MAX_LOG_LEN - 1 && (idx == 0 || tmp[idx - 1] != '\n')) {
        klog_putc(tmp, &idx, MAX_LOG_LEN, '\n');
    }
    tmp[idx] = '\0';
    klog_write_bytes(tmp, idx);
}

void klog(int level, const char *fmt, ...)
{
    if (level < klog_buf.level) {
        return;
    }
    spinlock_acquire(&klog_buf.lock);
    va_list ap;
    va_start(ap, fmt);
    klog_vprintf(level, fmt, ap);
    va_end(ap);
    spinlock_release(&klog_buf.lock);
}

int klog_read(char *dst, int n)
{
    spinlock_acquire(&klog_buf.lock);
    int available = 0;
    if (klog_buf.write_pos >= klog_buf.read_pos) {
        available = klog_buf.write_pos - klog_buf.read_pos;
    } else {
        available = LOG_BUF_SIZE - klog_buf.read_pos + klog_buf.write_pos;
    }

    if (available == 0) {
        spinlock_release(&klog_buf.lock);
        return 0;
    }
    if (n > available) {
        n = available;
    }
    for (int i = 0; i < n; i++) {
        dst[i] = klog_buf.buf[(klog_buf.read_pos + i) % LOG_BUF_SIZE];
    }
    klog_buf.read_pos = (klog_buf.read_pos + n) % LOG_BUF_SIZE;
    spinlock_release(&klog_buf.lock);
    return n;
}

void klog_set_level(int level)
{
    if (level < LOG_LEVEL_DEBUG)
        level = LOG_LEVEL_DEBUG;
    if (level > LOG_LEVEL_ERROR)
        level = LOG_LEVEL_ERROR;
    klog_buf.level = level;
}
