#include "user.h"

int
main(void)
{
    write(1, "Hello from user init!\n", 22);
    exit(0);
}
