#include "user/user.h"

int strlen(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

void puts(const char *s)
{
    write(1, s, strlen(s));
}
