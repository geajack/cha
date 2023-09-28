#include <stdio.h>
#include <stdlib.h>

int readline(char *buffer)
{
    int i = 0;
    while (1)
    {
        int c = getc(stdin);

        if (c == '\n' || c == EOF)
        {
            buffer[i] = 0;
            return c;
        }

        buffer[i] = c;
        i += 1;
    }    
}

int main()
{
    int n = 0;
    char buffer[1024];
    while (1)
    {
        int stop_character = readline(buffer);
        n += 1;

        if (stop_character == EOF) break;
        printf("Read %d lines\n", n);
    }
}