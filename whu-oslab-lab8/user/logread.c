#include "user/user.h"

#define BUFSZ 512

int
main(int argc, char **argv)
{
    char buf[BUFSZ];
    while (1) {
        int n = klog(buf, sizeof(buf));
        if (n < 0) {
            write(2, "klog failed\n", 12);
            exit(-1);
        }
        if (n == 0) {
            // simple polling
            continue;
        }
        write(1, buf, n);
    }
    return 0;
}
