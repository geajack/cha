#include <stdio.h>

int main()
{
    FILE *f = fopen("n", "w");
    int n = 0;
    while (1)
    {
        fseek(f, 0, SEEK_SET);
        fwrite(&n, sizeof(n), 1, f);
        printf("hello world\n");
        n += 1;
        if (n > 1000) n = 0;
    }
}