#include <stdio.h>

const int PIPE_BUFFER_SIZE = 1024;

struct PipeBuffer
{
    char data[PIPE_BUFFER_SIZE];
    char unsent_data[PIPE_BUFFER_SIZE];
    int write_offset;
    int read_offset;
    int overflow_flag;
    int n_unsent_bytes;
};

typedef struct PipeBuffer PipeBuffer;

PipeBuffer pipe_buffers[64];
int n_pipes_in_use = 0;

PipeBuffer *acquire_internal_pipe()
{
    PipeBuffer *pipe = &pipe_buffers[n_pipes_in_use];
    n_pipes_in_use += 1;
    return pipe;
}

int pipe_read_line(PipeBuffer *read_pipe, char *buffer)
{
    int j = 0;
    int i = read_pipe->read_offset;
    int original_offset = read_pipe->read_offset;
    char *data = read_pipe->data;
    int is_more_data = 1;
    while (is_more_data)
    {
        char c = data[i];
        i += 1;
        if (c == '\n')
        {
            break;
        }
        buffer[j] = c;
        j += 1;

        if (i >= read_pipe->write_offset)
        {
            is_more_data = read_pipe->overflow_flag;
        }

        if (i >= PIPE_BUFFER_SIZE)
        {
            i = 0;
            read_pipe->overflow_flag = 0;
        }
    }

    read_pipe->read_offset = i;
    buffer[j] = 0;

    if (!is_more_data)
    {
        // we ran out of data looking for a newline
        read_pipe->read_offset = original_offset;
        return 0;
    }

    return 1;
}

int pipe_write(PipeBuffer *write_pipe, char *data, int n_bytes)
{
    const int read_offset = write_pipe->read_offset;

    {
        const int write_offset = write_pipe->write_offset;
        const int is_overflow = write_pipe->overflow_flag;

        int n_bytes_of_space;
        if (is_overflow)
        {
            n_bytes_of_space = read_offset - write_offset;
        }
        else
        {
            int n_available_before_wraparound = PIPE_BUFFER_SIZE - write_offset;
            int n_available_after_wraparound = read_offset;
            n_bytes_of_space = n_available_before_wraparound + n_available_after_wraparound;
        }

        if (n_bytes > n_bytes_of_space)
        {
            return 0;
        }
    }

    int buffer_offset = write_pipe->write_offset;
    int source_offset = 0;
    int source_length = n_bytes;
    
    int allowed_to_write = 1;
    int is_data_left = source_offset < source_length;
    while (allowed_to_write && is_data_left)
    {
        if (write_pipe->overflow_flag && buffer_offset >= read_offset)
        {
            allowed_to_write = 0;
        }
        else
        {
            char *destination = &write_pipe->data[buffer_offset];
            *destination = data[source_offset];
            source_offset += 1;
            is_data_left = source_offset < source_length;

            buffer_offset += 1;
            if (buffer_offset >= sizeof(write_pipe->data))
            {
                buffer_offset = 0;
                write_pipe->overflow_flag = 1;
            }
        }
    }

    write_pipe->write_offset = buffer_offset;

    if (is_data_left)
    {
        // error - should never happen since we make sure we have enough space before writing
        printf("ERROR: Buffer overflow (interpreter.c:%d)\n", __LINE__);
    }

    return 1;
}

void pipe_try_to_flush_unsent(PipeBuffer *pipe)
{
    int success = pipe_write(pipe, pipe->unsent_data, pipe->n_unsent_bytes);
    if (success)
    {
        pipe->n_unsent_bytes = 0;
    }
}

void print_pipe_state(PipeBuffer *pipe)
{
    int size = sizeof(pipe->data);
    for (int i = 0; i < size; i += 1)
    {
        char c = pipe->data[i];
        if ('a' <= c && c <= 'z')
        {
            putc(c, stdout);
        }
        else if (c == '\n')
        {
            putc(' ', stdout);
        }
        else
        {
            putc('_', stdout);
        }
    }

    if (pipe->overflow_flag)
    {
        printf("  OVERFLOW");
    }
    printf("\n");

    for (int i = 0; i < size; i += 1)
    {
        if (i == pipe->write_offset && i == pipe->read_offset)
        {
            putc('X', stdout);
        }
        else if (i == pipe->write_offset)
        {
            putc('^', stdout);
        }
        else if (i == pipe->read_offset)
        {
            putc('v', stdout);
        }
        else
        {
            putc(' ', stdout);
        }
    }

    printf("\n");
    printf("\n");
}