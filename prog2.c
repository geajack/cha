#include <stdio.h>
#include <stdlib.h>

void readline(char *buffer)
{
    int i = 0;
    while (1)
    {
        char c = getc(stdin);

        if (c == '\n')
        {
            buffer[i] = 0;
            return;
        }

        buffer[i++] = c;
    }    
}

int main()
{
    int n = 0;
    char buffer[1024];
    size_t length = 1024;
    while (1)
    {
        // FILE *f = fopen("n", "r");
        // fscanf(f, "%d", &n);
        // fclose(f);
        n += 1;

        readline(buffer);
        printf("%d: %s\n", n, buffer);
    }
}