#include <stdio.h>
#include <string.h>

struct StringList
{
    char storage[1024];
    int capacity;
    int size;
    int count;
};

struct StringBuffer
{
    char storage[1024];
    int capacity;
    int size;
};

int string_list_append(struct StringList *list, char *string, int length)
{
    memcpy(&list->storage[list->size], string, length);
    list->storage[list->size + length] = 0;
    list->size += length + 1;
    list->count += 1;
    return 0;
}

int string_list_extend(struct StringList *list, char *string, int length)
{
    memcpy(&list->storage[list->size - 1], string, length);
    list->storage[list->size - 1 + length] = 0;
    list->size += length + 1;
    list->count += 1;
    return 0;
}

char *string_list_iter(struct StringList *list, char *string)
{
    if (string == 0) return list->storage;
    
    while (*string != 0) string += 1;

    if (((size_t) string - (size_t) list->storage) >= list->size - 1) return 0;

    return string + 1;
}

int string_buffer_append(struct StringBuffer *buffer, char c)
{
    buffer->storage[buffer->size] = c;
    buffer->size += 1;
    return 0;
}

int string_buffer_cat(struct StringBuffer *buffer, char *string, int length)
{
    memcpy(&buffer->storage[buffer->size], string, length);
    buffer->size += length;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("ERROR: No input file.\n");
        return 1;
    }

    FILE *input_file = fopen(argv[1], "r");
    if (!input_file)
    {
        printf("ERROR: Could not read input file\n");
        return 1;
    }

    struct StringList strings;
    strings.size = 0;
    strings.count = 0;
    struct StringBuffer string;
    string.size = 0;
    struct StringBuffer line;
    line.size = 0;
    int n_dots = 0;

    int waiting_for_dots = 0;

    int c = fgetc(input_file);
    while (c != EOF)
    {
        int consume = 0;

        if (!waiting_for_dots)
        {
            if (c == '\n')
            {
                line.size = 0;
                waiting_for_dots = 1;
                consume = 1; 
            }
            else
            {
                string_buffer_append(&string, c);
                consume = 1;
            }
        }
        else
        {
            if (c == ' ')
            {
                string_buffer_append(&line, c);
                consume = 1;
            }
            else if (c == '.')
            {
                string_buffer_append(&line, c);
                n_dots += 1;
                consume = 1;

                if (n_dots == 3)
                {
                    n_dots = 0;
                    waiting_for_dots = 0;
                }
            }
            else
            {
                string_buffer_cat(&string, line.storage, line.size);
                string_list_append(&strings, string.storage, string.size);
                string.size = 0;
                waiting_for_dots = 0;
                n_dots = 0;
                consume = 0;
            }
        }

        if (consume) c = fgetc(input_file);
    }

    string_list_append(&strings, string.storage, string.size);

    {
        char *string = string_list_iter(&strings, 0);
        while (string != 0)
        {
            printf("%s\n", string);
            string = string_list_iter(&strings, string);
        }
    }

    return 0;
}