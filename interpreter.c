#include <stdio.h>
#include <stdlib.h>
#include "ast_parser.c"

enum ValueType
{
    VALUE_TYPE_STRING,
    VALUE_TYPE_NUMBER
};

struct Value
{
    enum ValueType type;
    union
    {
        char *string_value;
        int integer_value;
    };
};

typedef struct Value Value;

struct Symbol
{
    char *name;
    Value *value;
};

typedef struct Symbol Symbol;

Value *alloc_value(int type)
{
    Value *value = malloc(sizeof(Value));
    value->type = type;
    return value;
}

void set_symbol(char *name, Value *value)
{

}

Value *lookup_symbol(char *name)
{
    Value *value = alloc_value(VALUE_TYPE_STRING);
    char *string = malloc(128);
    sprintf(string, "dummy value for <%s>", name);
    value->string_value = string;
    return value;
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
            result->integer_value = expression->number;
        }
    }
    else if (type == MULTIPLY_NODE)
    {
        Value *left = evaluate(expression->first_child);
        Value *right = evaluate(expression->first_child->next_sibling);

        if (left->type == VALUE_TYPE_NUMBER && right->type == VALUE_TYPE_NUMBER)
        {
            int sum = left->integer_value * right->integer_value;
            result = alloc_value(VALUE_TYPE_NUMBER);
            result->integer_value = expression->number;
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
    return 1;
}

void interpret(ASTNode *root)
{
    if (root->type != PROGRAM_NODE)
    {
        printf("Error (interpreter.c:%d)\n", __LINE__);
        return;
    }

    int done = 0;
    ASTNode *statement = root->first_child;
    while (!done)
    {
        ASTNode *next_statement = statement->next_sibling;

        if (statement->type == PRINT_NODE)
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
            next_statement = statement->first_child;
        }
        else if (statement->type == IF_NODE)
        {
            ASTNode *condition = statement->first_child;
            ASTNode *body = condition->next_sibling;
            Value *value = evaluate(condition);
            if (is_truthy(value))
            {
                next_statement = body;
            }
        }
                
        while (!next_statement)
        {
            statement = statement->parent;
            if (statement == root)
            {
                done = 1;
                break;
            }

            next_statement = statement->next_sibling;
        }
        statement = next_statement;
    }
}

char input[1024 * 1024];

int main()
{
    FILE *file = fopen("input.txt", "r");
    int input_length = fread(input, 1, sizeof(input), file);
    
    ASTNode *program = parse(input, input_length);

    interpret(program);
    // print_ast(program);

    return 0;
}