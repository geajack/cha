#include <stdio.h>

int main()
{
    int n = 0;
    while (n < 10)
    {
        printf("hello world\n");
        n += 1;
    }

    fflush(stdout);

    while (1) {}
}