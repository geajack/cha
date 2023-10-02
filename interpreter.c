#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wait.h>
#include <errno.h>
#include <fcntl.h>

#include "parser.c"
#include "pipes.c"

struct POSIXPipe
{
    int read;
    int write;
};

typedef struct POSIXPipe POSIXPipe;

const int GLOBAL_HOST_READ_BUFFER_SIZE = 1024;
char GLOBAL_HOST_READ_BUFFER[GLOBAL_HOST_READ_BUFFER_SIZE];

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
    int waiting_on_host_process;

    PipeBuffer *write_pipe;
    PipeBuffer *read_pipe;

    int host_write;
    int host_read;
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

Value *readline(InterpreterThread *thread)
{
    char buffer[128];
    
    if (thread->read_pipe)
    {
        PipeBuffer *read_pipe = thread->read_pipe;
        if (read_pipe->closed)
        {
            Value *value = alloc_value(VALUE_TYPE_BOOLEAN);
            value->boolean_value = 0;
            return value;
        }

        if (!pipe_read_line(read_pipe, buffer)) return 0;
    }
    else
    {
        int i = 0;
        while (1)
        {
            int success = read(STDIN_FILENO, &buffer[i], 1);
            if (!success)
            {
                // printf("ERROR: Ran out of input reading a line from stdin (%s:%d)\n", __FILE__, __LINE__);
                // out of input
                buffer[i] = 0;
                break;
            }

            if (buffer[i] == '\n')
            {
                buffer[i] = 0;
                break;
            }

            i += 1;

            if (i >= sizeof(buffer))
            {
                printf("ERROR: Buffer cannot hold line from stdin (%s:%d)\n", __FILE__, __LINE__);
                break;
            }
        }
    }

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

void thread_read_from_host(InterpreterThread *thread)
{
    if (thread->write_pipe)
    {
        PipeBuffer *pipe = thread->write_pipe;
        
        int result = read(thread->host_read, pipe->unsent_data, PIPE_BUFFER_SIZE);
        if (result != -1)
        {
            // result is number of bytes
            pipe->n_unsent_bytes = result;
            pipe_try_to_flush_unsent(pipe);
        }
    }
    else
    {
        int result = read(thread->host_read, GLOBAL_HOST_READ_BUFFER, GLOBAL_HOST_READ_BUFFER_SIZE);
        if (result != -1)
        {
            // result is number of bytes
            write(STDOUT_FILENO, GLOBAL_HOST_READ_BUFFER, result);
        }
        else
        {
            // printf("%s\n", strerror(errno));
        }
    }
}

void thread_write_to_host(InterpreterThread *thread)
{
    int n_read;
    char *buffer;

    if (thread->read_pipe)
    {   
        buffer = GLOBAL_PIPE_READ_BUFFER;
        n_read = pipe_read(thread->read_pipe);        

        if (n_read == 0)
        {
            if (thread->read_pipe->closed)
            {
                close(thread->host_write);
                return;
            }
        }
    }
    else
    {
        // this is for if we're piping input right into the script from the command line
        buffer = GLOBAL_HOST_READ_BUFFER;
        n_read = read(STDIN_FILENO, GLOBAL_HOST_READ_BUFFER, GLOBAL_HOST_READ_BUFFER_SIZE);
        // printf("%d\n", n_read);
        if (n_read == 0)
        {
            close(thread->host_write);
            return;
        }
    }

    int n_written = write(thread->host_write, buffer, n_read);

    if (n_written < n_read)
    {
        // host error, shouldn't happen
        printf("ERROR: Could not write to host process (%s:%d)\n", __FILE__, __LINE__);
    }
}

// incomplete: this never returns 0 if it's outputting to stdout, but probably stdout can get clogged too?
int print(InterpreterThread *thread, Value *value)
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

    if (thread->write_pipe == 0)
    {
        printf("%s", temp);
    }
    else
    {
        return pipe_write(thread->write_pipe, temp, strlen(temp));
    }

    return 1;
}

int n_processes = 0;
int execute_host_program(InterpreterThread *thread, char *program, char **arguments, int n_arguments)
{
    if (n_processes >= 10)
    {
        printf("Too many processes running, I won't run another one.\n");
        return 0;
    }

    arguments[n_arguments] = 0;

    typedef struct { int read; int write; } POSIXFDPair;
    POSIXFDPair script_to_host;
    POSIXFDPair host_to_script;

    pipe((int*) &script_to_host);
    pipe((int*) &host_to_script);
    
    {
        int flags = fcntl(host_to_script.read, F_GETFL);
        fcntl(host_to_script.read, F_SETFL, flags | O_NONBLOCK);
    }

    {
        int flags = fcntl(host_to_script.read, F_GETFD);
        fcntl(host_to_script.read, F_SETFD, flags | FD_CLOEXEC);
    }

    {
        int flags = fcntl(script_to_host.write, F_GETFD);
        fcntl(script_to_host.write, F_SETFD, flags | FD_CLOEXEC);
    }

    n_processes += 1;
    int pid = fork();
    if (pid == 0)
    {
        dup2(host_to_script.write, STDOUT_FILENO);
        dup2(script_to_host.read, STDIN_FILENO);

        execvp(arguments[0], arguments);
        printf("ERROR: Could not start process \"%s\".\n", program);
        exit(0);
    }

    close(host_to_script.write);
    close(script_to_host.read);

    thread->host_write = script_to_host.write;
    thread->host_read = host_to_script.read;

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
    thread_pool[n_threads].waiting_on_host_process = 0;
    thread_pool[n_threads].host_read = STDIN_FILENO;
    thread_pool[n_threads].host_write = STDOUT_FILENO;
    
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
                case EXIT_NODE:
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
                    done = 1;
                }
            }
            else if (current_node->type == EXIT_NODE)
            {
                Value *value = context->values[0];
                exit(value->integer_value);
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
                    int pid = execute_host_program(thread, program, arguments, n_arguments);
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
                    int may_continue = 1;
                    int may_read_data = 1;
                    if (thread->write_pipe)
                    {
                        // try to send data
                        pipe_try_to_flush_unsent(thread->write_pipe);
                        if (thread->write_pipe->n_unsent_bytes > 0)
                        {
                            may_read_data = 0;
                            may_continue = 0;
                        }
                    }

                    thread_write_to_host(thread);

                    if (thread->awaiting_pid > 0)
                    {
                        if (may_read_data)
                        {
                            // try to read data from process
                            thread_read_from_host(thread);
                        }

                        int exit_code;
                        int result = waitpid(thread->awaiting_pid, &exit_code, WNOHANG);                    
                        if (result > 0)
                        {
                            // process is done
                            thread->awaiting_pid = 0;
                            n_processes -= 1;
                        }
                        else
                        {
                            may_continue = 0;
                        }
                    }

                    if (may_continue)
                    {
                        thread->waiting_on_host_process = 0;
                    }
                    else
                    {
                        down = current_node;
                    }

                    /*
                    The job of executing a host node is to flush all data from the upstream thread to the host,
                    then flush all data from the host to the downstream thread, then check if the host process
                    has stopped. There's no need to do this more than once in a row.
                    */
                    done = 1;
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
                ASTNode *chain = current_node->first_child->next_sibling;
                InterpreterThread *left_thread = spawn_child_thread(thread, current_node->first_child);
                while (chain->type == PIPE_NODE)
                {
                    ASTNode *node = chain->first_child;
                    InterpreterThread *right_thread = spawn_child_thread(thread, node);
                    
                    PipeBuffer *pipe = acquire_internal_pipe();
                    left_thread->write_pipe = pipe;
                    right_thread->read_pipe = pipe;
                    pipe->n_writers = 1;
                    
                    left_thread = right_thread;
                    chain = chain->first_child->next_sibling;
                }

                InterpreterThread *right_thread = spawn_child_thread(thread, chain);
                PipeBuffer *pipe = acquire_internal_pipe();
                left_thread->write_pipe = pipe;
                right_thread->read_pipe = pipe;
                pipe->n_writers = 1;

                if (thread->write_pipe)
                {
                    right_thread->write_pipe = thread->write_pipe;
                    thread->write_pipe->n_writers += 1;
                }
                
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
                        done = 1;
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
            else if (current_node->type == EQUALS_NODE)
            {
                Value *left = context->values[0];
                Value *right = context->values[1];

                Value *result;
                result = alloc_value(VALUE_TYPE_BOOLEAN);

                if (left->type == VALUE_TYPE_STRING && right->type == VALUE_TYPE_STRING)
                {
                    int comparison = streq(left->string_value, right->string_value);
                    result->boolean_value = comparison;
                }
                else if (left->type == VALUE_TYPE_NUMBER && right->type == VALUE_TYPE_NUMBER)
                {
                    int comparison = left->integer_value == right->integer_value;
                    result->boolean_value = comparison;
                }
                else if (left->type == VALUE_TYPE_BOOLEAN && right->type == VALUE_TYPE_BOOLEAN)
                {
                    int comparison = left->boolean_value == right->boolean_value;
                    result->boolean_value = comparison;
                }
                else
                {
                    result->boolean_value = 0;
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

                if (thread->write_pipe)
                {
                    thread->write_pipe->n_writers -= 1;
                    if (thread->write_pipe->n_writers == 0)
                    {
                        thread->write_pipe->closed = 1;
                    }
                }

                if (thread->read_pipe)
                {
                    thread->read_pipe->closed = 1;                
                }

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
    }

    thread->current = current_node;
}

void run_program(ASTNode *program)
{
    spawn_child_thread(0, program);

    int done = 0;
    while (!done)
    {
        int all_finished = 1;
        for (int i = 0; i < n_threads; i++)
        {
            InterpreterThread *thread = &thread_pool[i];

            if (!thread->finished)
            {
                all_finished = 0;
            }

            if (thread->n_pending_children > 0)
            {
                continue;
            }

            if (thread->finished)
            {
                if (thread->parent)
                {
                    thread->parent->n_pending_children -= 1;
                    thread->parent = 0; // temporary measure, since currently we don't drop threads from the loop when they're done
                }
                continue;
            }

            resume_execution(thread);
        }

        if (all_finished) done = 1;
    }
}

char input[1024 * 1024];

int main(int argc, char **argv)
{
    int do_interpret = 1;
    char* filename = "input.cha";
    for (int i = 1; i < argc; i++)
    {
        int r = streq(argv[i], "-t");
        if (streq(argv[i], "-t"))
        {
            do_interpret = 0;
        }
        else
        {
            filename = argv[i];
            break;
        }
    }

    FILE *file = fopen(filename, "r");
    if (!file)
    {
        printf("File not found: %s\n", filename);
        return 1;
    }

    int input_length = fread(input, 1, sizeof(input), file);
    
    ASTNode *program = parse(input, input_length);

    if (do_interpret)
        run_program(program);
    else
        print_ast(program);

    return 0;
}