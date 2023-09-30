#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wait.h>

#include "parser.c"

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

PipeBuffer pipe_buffers[1];

struct EvaluationContext
{
    struct Value *values[2];
    int n_values_needed;
    int n_values_filled;
    ASTNode *previous_child;
};

typedef struct EvaluationContext EvaluationContext;

struct InterpreterThread
{
    struct InterpreterThread *parent;
    int n_pending_children;
    int finished;

    ASTNode *root;
    ASTNode *current;
    EvaluationContext context_stack[32];
    int context_stack_size;
    struct Value *returned_value;

    int awaiting_pid;

    PipeBuffer *write_pipe;
    PipeBuffer *read_pipe;
};

typedef struct InterpreterThread InterpreterThread;

InterpreterThread thread_pool[64];
int n_threads = 0;

enum ValueType
{
    VALUE_TYPE_STRING,
    VALUE_TYPE_NUMBER,
    VALUE_TYPE_BOOLEAN
};

struct Value
{
    enum ValueType type;
    union
    {
        char *string_value;
        int integer_value;
        int boolean_value;
    };
};

typedef struct Value Value;

struct Symbol
{
    char *name;
    Value *value;
};

typedef struct Symbol Symbol;

Symbol symbol_table[64];
int n_symbols = 0;

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

Value *alloc_value(int type)
{
    Value *value = malloc(sizeof(Value));
    value->type = type;
    return value;
}

void set_symbol(char *name, Value *value)
{
    int i;
    for (i = 0; i < n_symbols; i++)
    {
        char *symbol_name = symbol_table[i].name;
        if (streq(symbol_name, name))
        {
            break;
        }
    }

    symbol_table[i].name = name;
    symbol_table[i].value = value;
    
    if (i == n_symbols) n_symbols += 1;
}

Value *readline(InterpreterThread *context)
{
    char buffer[128];
    
    int j = 0;
    int i = context->read_pipe->read_offset;
    char *data = context->read_pipe->data;
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


        if (i >= context->read_pipe->write_offset)
        {
            is_more_data = context->read_pipe->overflow_flag;
        }

        if (i >= sizeof(context->read_pipe->data))
        {
            i = 0;
            context->read_pipe->overflow_flag = 0;
        }
    }

    if (!is_more_data)
    {
        // we ran out of data looking for a newline
        return 0;
    }

    buffer[j] = 0;

    context->read_pipe->read_offset = i;

    Value *value = alloc_value(VALUE_TYPE_STRING);
    value->string_value = save_string_to_heap(buffer);

    return value;
}

Value *lookup_symbol(char *name)
{
    if (streq(name, "true"))
    {
        Value *value = alloc_value(VALUE_TYPE_BOOLEAN);
        value->boolean_value = 1;
        return value;
    }
    else if (streq(name, "false"))
    {
        Value *value = alloc_value(VALUE_TYPE_BOOLEAN);
        value->boolean_value = 0;
        return value;
    }

    for (int i = 0; i < n_symbols; i++)
    {
        char *symbol_name = symbol_table[i].name;
        if (streq(symbol_name, name)) return symbol_table[i].value;
    }

    printf("ERROR: Undefined variable \"%s\"\n", name);
    return 0;
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
}

void pipe_try_to_flush_unsent(PipeBuffer *pipe)
{
    int success = pipe_write(pipe, pipe->unsent_data, pipe->n_unsent_bytes);
    if (success)
    {
        pipe->n_unsent_bytes = 0;
    }
}

// incomplete: this never returns 0 if it's outputting to stdout, but probably stdout can get clogged too?
int print(InterpreterThread *context, Value *value)
{
    char temp[256];

    if (value->type == VALUE_TYPE_STRING)
    {
        sprintf(temp, "%s\n", value->string_value);
    }
    else if (value->type == VALUE_TYPE_NUMBER)
    {
        sprintf(temp, "%d\n", value->integer_value);
    }
    else if (value->type == VALUE_TYPE_BOOLEAN)
    {
        if (value->boolean_value)
        {
            sprintf(temp, "true\n");
        }
        else
        {
            sprintf(temp, "false\n");
        }
    }

    if (context->write_pipe == 0)
    {
        printf("%s", temp);
    }
    else
    {
        return pipe_write(context->write_pipe, temp, strlen(temp));
    }

    return 1;
}

int n_processes = 0;
int execute_host_program(char *program, char **arguments, int n_arguments)
{
    if (n_processes >= 3)
    {
        printf("Too many processes running, I won't run another one.\n");
        return 0;
    }

    // printf("Executing host program \"%s\"", program);
    // if (n_arguments > 0) printf(" with arguments ");
    // for (int i = 0; i < n_arguments; i++)
    // {
    //     printf("[%s] ", arguments[i]);
    // }
    // printf("\n");

    arguments[n_arguments] = 0;

    int pid = fork();
    n_processes += 1;
    if (pid == 0)
    {
        execvp(arguments[0], arguments);
        printf("ERROR: Could not start process \"%s\".\n", program);
        exit(0);
    }
    return pid;
}

int is_truthy(Value *value)
{
    if (value->type == VALUE_TYPE_BOOLEAN)
    {
        return value->boolean_value;
    }
    else if (value->type == VALUE_TYPE_NUMBER)
    {
        return value->integer_value != 0;
    }
    else if (value->type == VALUE_TYPE_STRING)
    {
        return value->string_value[0] != 0;
    }

    return 0;
}

InterpreterThread *spawn_child_thread(InterpreterThread *parent, ASTNode *root)
{
    thread_pool[n_threads].root = root;
    thread_pool[n_threads].current = root;
    thread_pool[n_threads].n_pending_children = 0;
    thread_pool[n_threads].finished = 0;
    thread_pool[n_threads].parent = parent;
    thread_pool[n_threads].context_stack_size = 0;
    thread_pool[n_threads].returned_value = 0;
    thread_pool[n_threads].awaiting_pid = 0;
    
    InterpreterThread *child = &thread_pool[n_threads];
    if (parent)
    {
        parent->n_pending_children += 1;
    }
    n_threads += 1;

    return child;
}

void resume_execution(InterpreterThread *thread)
{
    int done = 0;
    ASTNode *current_node = thread->current;
    while (!done)
    {
        ASTNode *down = 0;

        int current_context_is_mine = thread->returned_value != 0;
        if (!thread->returned_value)
        {
            int n_required_values = 0;
            switch (current_node->type)
            {
                case PRINT_NODE:
                case SET_NODE:
                case IF_NODE:
                case WHILE_NODE:
                n_required_values = 1;
                break;
                
                case ADD_NODE:
                case MULTIPLY_NODE:
                case LESSTHAN_NODE:
                case EQUALS_NODE:
                n_required_values = 2;
                break;
                
                default:
                break;
            }

            if (n_required_values)
            {
                // push context                
                thread->context_stack[thread->context_stack_size].n_values_needed = n_required_values;
                thread->context_stack[thread->context_stack_size].n_values_filled = 0;
                thread->context_stack[thread->context_stack_size].previous_child = 0;
                thread->context_stack_size += 1;

                current_context_is_mine = 1;
            }
        }

        EvaluationContext *context = &thread->context_stack[thread->context_stack_size - 1];        

        int do_pop_context = 0;
        int execute_current_node = 1;
        if (current_context_is_mine)
        {
            do_pop_context = 1;

            // push value to context
            if (thread->returned_value)
            {
                int n = context->n_values_filled;
                context->values[n] = thread->returned_value;
                context->n_values_filled += 1;
            }

            int have = context->n_values_filled;
            int need = context->n_values_needed;
            if (have < need)
            {
                do_pop_context = 0;
                execute_current_node = 0;
                if (context->previous_child)
                    down = context->previous_child->next_sibling;
                else
                    down = current_node->first_child;

                context->previous_child = down;
            }
        }

        thread->returned_value = 0;

        if (execute_current_node)
        {
            if (current_node->type == PROGRAM_NODE)
            {
                down = current_node->first_child;
            }
            else if (current_node->type == PRINT_NODE)
            {
                Value *value = context->values[0];
                int success = print(thread, value);
                if (!success)
                {
                    // pipe is full - we have to retry later
                    down = current_node;
                    do_pop_context = 1;
                }
            }
            else if (current_node->type == SET_NODE)
            {
                char *name = current_node->first_child->next_sibling->name;
                Value *value = context->values[0];
                set_symbol(name, value);
            }
            else if (current_node->type == HOST_NODE)
            {
                if (!thread->waiting_on_host_process)
                {
                    char *program = current_node->first_child->string;
                    char *arguments[64];
                    arguments[0] = program;
                    int n_arguments = 1;
                    ASTNode *argument = current_node->first_child->next_sibling;
                    while (argument)
                    {
                        arguments[n_arguments] = argument->string;
                        n_arguments += 1;
                        argument = argument->next_sibling;
                    }                    
                    int pid = execute_host_program(program, arguments, n_arguments);
                    if (pid > 0)
                    {
                        thread->awaiting_pid = pid;
                        thread->waiting_on_host_process = 1;
                    }
                    else
                    {
                        printf("ERROR: Error launching process \"%s\" (%s:%d)\n", program, __FILE__, __LINE__);
                    }

                    down = current_node;
                }
                else
                {
                    if (thread->write_pipe->n_unsent_bytes > 0)
                    {
                        // try to send data
                        pipe_try_to_flush_unsent(thread->write_pipe);
                    }

                    if (thread->awaiting_pid > 0)
                    {
                        if (thread->write_pipe->n_unsent_bytes == 0)
                        {
                            // try to read data from process
                            
                        }

                        int exit_code;
                        int result = waitpid(thread->awaiting_pid, &exit_code, WNOHANG);                    
                        if (result > 0)
                        {
                            // process is done
                            thread->awaiting_pid = 0;
                        }
                    }

                    if (thread->awaiting_pid == 0 && thread->write_pipe.n_unsent_bytes == 0)
                    {
                        thread->waiting_on_host_process = 0;
                    }
                    else
                    {
                        // process is still running
                        down = current_node;
                    }
                }
            }
            else if (current_node->type == CODEBLOCK_NODE)
            {
                down = current_node->first_child;
            }
            else if (current_node->type == IF_NODE)
            {
                Value *value = context->values[0];
                if (is_truthy(value))
                {
                    ASTNode *condition = current_node->first_child;
                    ASTNode *body = condition->next_sibling;
                    down = body;
                }
            }
            else if (current_node->type == WHILE_NODE)
            {
                Value *value = context->values[0];
                if (is_truthy(value))
                {
                    ASTNode *condition = current_node->first_child;
                    ASTNode *body = condition->next_sibling;
                    down = body;
                }
            }
            else if (current_node->type == PIPE_NODE)
            {
                ASTNode *left = current_node->first_child;
                ASTNode *right = current_node->first_child->next_sibling;
                InterpreterThread *left_thread = spawn_child_thread(thread, left);
                InterpreterThread *right_thread = spawn_child_thread(thread, right);
                left_thread->write_pipe = &pipe_buffers[0];
                right_thread->read_pipe = &pipe_buffers[0];
                // print_pipe_state(&pipe_buffers[0]);
                done = 1;
            }
            else if (current_node->type == NUMBER_NODE)
            {
                thread->returned_value = alloc_value(VALUE_TYPE_NUMBER);
                thread->returned_value->integer_value = current_node->number;
            }
            else if (current_node->type == STRING_NODE)
            {
                thread->returned_value = alloc_value(VALUE_TYPE_STRING);
                thread->returned_value->string_value = current_node->string;
            }
            else if (current_node->type == NAME_NODE)
            {
                thread->returned_value = lookup_symbol(current_node->name);
            }
            else if (current_node->type == FUNCTION_CALL_NODE)
            {
                if (streq(current_node->name, "readline"))
                {
                    Value *value = readline(thread);
                    if (value)
                    {
                        thread->returned_value = value;
                    }
                    else
                    {
                        // not enough data - we need to try again later
                        down = current_node;
                        do_pop_context = 0;
                    }
                }
                else
                {
                    printf("PARSE ERROR: Unknown function \"%s\" (%s:%d)\n", current_node->name, __FILE__, __LINE__);
                }
            }
            else if (current_node->type == MULTIPLY_NODE)
            {
                Value *left = context->values[0];
                Value *right = context->values[1];

                if (left->type == VALUE_TYPE_NUMBER && right->type == VALUE_TYPE_NUMBER)
                {
                    int product = left->integer_value * right->integer_value;
                    thread->returned_value = alloc_value(VALUE_TYPE_NUMBER);
                    thread->returned_value->integer_value = product;
                }
            }
            else if (current_node->type == ADD_NODE)
            {
                Value *left = context->values[0];
                Value *right = context->values[1];

                Value *result;

                if (left->type == VALUE_TYPE_NUMBER && right->type == VALUE_TYPE_NUMBER)
                {
                    int sum = left->integer_value + right->integer_value;
                    result = alloc_value(VALUE_TYPE_NUMBER);
                    result->integer_value = sum;
                }
                else if (left->type == VALUE_TYPE_STRING && right->type == VALUE_TYPE_NUMBER)
                {
                    char buffer[256];
                    sprintf(buffer, "%s%d",left->string_value, right->integer_value);
                    result = alloc_value(VALUE_TYPE_STRING);
                    result->string_value = save_string_to_heap(buffer);
                }
                else if (left->type == VALUE_TYPE_STRING && right->type == VALUE_TYPE_STRING)
                {
                    char buffer[256];
                    sprintf(buffer, "%s%s",left->string_value, right->string_value);
                    result = alloc_value(VALUE_TYPE_STRING);
                    result->string_value = save_string_to_heap(buffer);
                }

                if (!result)
                {
                    printf("ERROR: Unable to perform addition operation (parser.c:%d)\n", __LINE__);
                }

                thread->returned_value = result;
            }
            else if (current_node->type == LESSTHAN_NODE)
            {
                Value *left = context->values[0];
                Value *right = context->values[1];

                Value *result;

                if (left->type == VALUE_TYPE_NUMBER && right->type == VALUE_TYPE_NUMBER)
                {
                    int comparison = left->integer_value < right->integer_value;
                    result = alloc_value(VALUE_TYPE_BOOLEAN);
                    result->boolean_value = comparison;
                }

                thread->returned_value = result;
            }
            else
            {
                printf("ERROR: Unimplemented AST node (%s:%d)\n", __FILE__, __LINE__);
            }
        }

        if (do_pop_context)
        {
            thread->context_stack_size -= 1;
        }

        ASTNode *next = 0;
        if (down)
        {
            next = down;
        }

        if (thread->returned_value)
        {
            next = current_node->parent;
        }

        ASTNode *node = current_node;
        while (!next)
        {
            if (node == thread->root)
            {
                thread->finished = 1;
                done = 1;
                break;
            }
            else if (node->next_sibling)
            {
                next = node->next_sibling;
            }
            else
            {
                ASTNode *parent = node->parent;
                if (parent->type == WHILE_NODE)
                {
                    next = parent;
                }
                else
                {
                    node = parent;
                }
            }
        }

        current_node = next;

        done = 1;
    }

    thread->current = current_node;
}

void run_program(ASTNode *program)
{
    pipe_buffers[0].write_offset = 0;
    pipe_buffers[0].read_offset = 0;
    pipe_buffers[0].overflow_flag = 0;

    spawn_child_thread(0, program);

    int done = 0;
    while (!done)
    {
        int executed_anything = 0;
        for (int i = 0; i < n_threads; i++)
        {
            InterpreterThread *thread = &thread_pool[i];
            
            if (thread->n_pending_children > 0)
            {
                continue;
            }

            if (thread->finished)
            {
                if (thread->parent)
                {
                    thread->parent->n_pending_children -= 1;
                }
                continue;
            }

            resume_execution(thread);
            executed_anything = 1;
        }

        if (!executed_anything) done = 1;
    }
}

char input[1024 * 1024];

int main(int argc, char **argv)
{
    FILE *file = fopen("input.txt", "r");
    int input_length = fread(input, 1, sizeof(input), file);
    
    ASTNode *program = parse(input, input_length);

    int do_interpret = 1;
    if (argc > 1) if (argv[1][0] == 't') do_interpret = 0;

    if (do_interpret)
        run_program(program);
    else
        print_ast(program);

    return 0;
}