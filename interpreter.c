#include <stdio.h>
#include <stdlib.h>

#include "parser.c"

struct InterpreterThread
{
    ASTNode *root;
    ASTNode *current;
    int n_pending_children;
    int finished;
    struct InterpreterThread *parent;
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

Value *evaluate(ASTNode *expression)
{
    enum ASTNodeType type = expression->type;

    Value *result = 0;

    if (type == NAME_NODE)
    {
        return lookup_symbol(expression->name);
    }
    else if (type == NUMBER_NODE)
    {
        result = alloc_value(VALUE_TYPE_NUMBER);
        result->integer_value = expression->number;
    }
    else if (type == STRING_NODE)
    {
        result = alloc_value(VALUE_TYPE_STRING);
        result->string_value = expression->string;
    }
    else if (type == ADD_NODE)
    {
        Value *left = evaluate(expression->first_child);
        Value *right = evaluate(expression->first_child->next_sibling);
        
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
    }
    else if (type == MULTIPLY_NODE)
    {
        Value *left = evaluate(expression->first_child);
        Value *right = evaluate(expression->first_child->next_sibling);

        if (left->type == VALUE_TYPE_NUMBER && right->type == VALUE_TYPE_NUMBER)
        {
            int product = left->integer_value * right->integer_value;
            result = alloc_value(VALUE_TYPE_NUMBER);
            result->integer_value = product;
        }
    }
    else if (type == LESSTHAN_NODE)
    {
        Value *left = evaluate(expression->first_child);
        Value *right = evaluate(expression->first_child->next_sibling);

        if (left->type == VALUE_TYPE_NUMBER && right->type == VALUE_TYPE_NUMBER)
        {
            int comparison = left->integer_value < right->integer_value;
            result = alloc_value(VALUE_TYPE_BOOLEAN);
            result->boolean_value = comparison;
        }
    }

    return result;
}

void print(Value *value)
{
    if (value->type == VALUE_TYPE_STRING)
    {
        printf("%s\n", value->string_value);
    }
    else if (value->type == VALUE_TYPE_NUMBER)
    {
        printf("%d\n", value->integer_value);
    }
    else if (value->type == VALUE_TYPE_BOOLEAN)
    {
        if (value->boolean_value)
        {
            printf("true\n");
        }
        else
        {
            printf("false\n");
        }
    }
}

void execute_host_program(char *program, char **arguments, int n_arguments)
{
    printf("Executing host program \"%s\"", program);
    if (n_arguments > 0) printf(" with arguments ");
    for (int i = 0; i < n_arguments; i++)
    {
        printf("[%s] ", arguments[i]);
    }
    printf("\n");
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

void spawn_child_thread(InterpreterThread *thread, ASTNode *root)
{
    thread_pool[n_threads].root = root;
    thread_pool[n_threads].current = root;
    thread_pool[n_threads].n_pending_children = 0;
    thread_pool[n_threads].finished = 0;
    thread_pool[n_threads].parent = thread;

    thread->n_pending_children += 1;
    n_threads += 1;
}

void resume_execution(InterpreterThread *thread)
{
    int done = 0;
    ASTNode *statement = thread->current;
    while (!done)
    {
        ASTNode *down = 0;

        if (statement->type == PROGRAM_NODE)
        {
            down = statement->first_child;
        }
        else if (statement->type == PRINT_NODE)
        {
            Value *value = evaluate(statement->first_child);
            print(value);
        }
        else if (statement->type == SET_NODE)
        {
            char *name = statement->first_child->name;
            ASTNode *rhs = statement->first_child->next_sibling;
            Value *value = evaluate(rhs);
            set_symbol(name, value);
        }
        else if (statement->type == HOST_NODE)
        {
            char *program = statement->first_child->string;
            char *arguments[64];
            int n_arguments = 0;
            ASTNode *argument = statement->first_child->next_sibling;
            while (argument)
            {
                arguments[n_arguments] = argument->string;
                n_arguments += 1;
                argument = argument->next_sibling;
            }
            execute_host_program(program, arguments, n_arguments);
        }
        else if (statement->type == CODEBLOCK_NODE)
        {
            down = statement->first_child;
        }
        else if (statement->type == IF_NODE)
        {
            ASTNode *condition = statement->first_child;
            ASTNode *body = condition->next_sibling;
            Value *value = evaluate(condition);
            if (is_truthy(value))
            {
                down = body;
            }
        }
        else if (statement->type == WHILE_NODE)
        {
            ASTNode *condition = statement->first_child;
            ASTNode *body = condition->next_sibling;
            Value *value = evaluate(condition);
            if (is_truthy(value))
            {
                down = body;
            }
        }
        else if (statement->type == PIPE_NODE)
        {
            ASTNode *left = statement->first_child;
            ASTNode *right = statement->first_child->next_sibling;
            spawn_child_thread(thread, left);
            spawn_child_thread(thread, right);
            done = 1;
        }
                
        ASTNode *next = 0;
        if (down)
        {
            next = down;
        }

        ASTNode *node = statement;
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

        statement = next;

        done = 1;
    }

    thread->current = statement;
}

void run_program(ASTNode *program)
{
    thread_pool[0].root = program;
    thread_pool[0].current = program;
    thread_pool[0].n_pending_children = 0;
    thread_pool[0].finished = 0;
    n_threads = 1;

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
                thread->parent->n_pending_children -= 1;
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