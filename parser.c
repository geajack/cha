#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.c"

int streq(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

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

Symbol symbol_table[64];
int n_symbols = 0;

struct Parser
{
    Lexer *lexer;
};

typedef struct Parser Parser;

Value *lookup_symbol(char *name)
{
    for (int i = 0; i < n_symbols; i++)
    {
        if (streq(symbol_table[i].name, name))
        {
            return symbol_table[i].value;
        }
    }
    return 0;
}

void push_symbol(char *name, Value *value)
{
    symbol_table[n_symbols].name = name;
    symbol_table[n_symbols].value = value;
    n_symbols += 1;
}

char *save_string_to_heap(char *string)
{
    int length = strlen(string);
    char *memory = (char*) malloc(length);
    strcpy(memory, string);
    return memory;
}

Value *interpreter_apply_operator(int op, Value *left, Value *right)
{
    Value *result = malloc(sizeof(Value));

    if (op == TOKEN_TYPE_OPADD)
    {
        if (left->type == VALUE_TYPE_NUMBER && right->type == VALUE_TYPE_NUMBER)
        {
            // add
            result->type = VALUE_TYPE_NUMBER;
            result->integer_value = left->integer_value + right->integer_value;
        }
        else
        {
            // concatenate
        }
    }
    else if (op == TOKEN_TYPE_OPMULTIPLY)
    {
        if (left->type == VALUE_TYPE_NUMBER && right->type == VALUE_TYPE_NUMBER)
        {
            // add
            result->type = VALUE_TYPE_NUMBER;
            result->integer_value = left->integer_value * right->integer_value;
        }
    }
    
    return result;
}

void parser_init(Parser *parser)
{}

Value dummy = { .type = VALUE_TYPE_STRING, .string_value = "dummy" };

Value *parser_consume_expression(Parser *parser, int parent_operator)
{
    Value *total = 0;
    int expecting_op = 0;

    while (1)
    {
        int token_type = parser->lexer->token.type;
        if (!expecting_op)
        {
            if (token_type == TOKEN_TYPE_NAME)
            {
                total = lookup_symbol(parser->lexer->token.text);
                if (total == 0)
                {
                    printf("PARSE ERROR: Undefined symbol (parser.c:%d)\n", __LINE__);
                    return 0;
                }

                expecting_op = 1;
                lexer_next_token(parser->lexer, 0);
            }
            else if (token_type == TOKEN_TYPE_STRING)
            {
                total = malloc(sizeof(Value));
                total->type = VALUE_TYPE_STRING;
                total->string_value = save_string_to_heap(parser->lexer->token.text);
                expecting_op = 1;
                lexer_next_token(parser->lexer, 0);
            }
            else if (token_type == TOKEN_TYPE_NUMBER)
            {
                total = malloc(sizeof(Value));
                total->type = VALUE_TYPE_NUMBER;

                int integer_value;
                {
                    char *text = parser->lexer->token.text;
                    int i = 0;
                    integer_value = 0;
                    while (text[i])               
                    {
                        int digit = text[i] - '0';
                        integer_value = integer_value * 10 + digit;
                        i += 1;
                    }
                }
                total->integer_value = integer_value;

                expecting_op = 1;
                lexer_next_token(parser->lexer, 0);
            }
            else if (token_type == TOKEN_TYPE_PARENOPEN)
            {
                lexer_next_token(parser->lexer, 0); // consume paren
                Value *inner_value = parser_consume_expression(parser, TOKEN_TYPE_OPADD);
                if (parser->lexer->token.type != TOKEN_TYPE_PARENCLOSE)
                {
                    printf("PARSE ERROR: Expected right parenthesis (parser.c:%d)\n", __LINE__);
                    return 0;
                }
                lexer_next_token(parser->lexer, 0); // consume close paren
                total = inner_value;
                expecting_op = 1;
            }
            else
            {
                printf("PARSE ERROR: Unexpected token (parser.c:%d)\n", __LINE__);
                return 0;
            }
        }
        else
        {
            if (token_type == TOKEN_TYPE_OPADD)
            {
                if (parent_operator == TOKEN_TYPE_OPMULTIPLY)
                {
                    // stop
                    // the peek token at this point will be the + operator, so the caller better be
                    // ready to handle an operator as the next token
                    return total;
                }
                else
                {
                    lexer_next_token(parser->lexer, 0); // consume +
                    Value *rhs = parser_consume_expression(parser, TOKEN_TYPE_OPADD);
                    total = interpreter_apply_operator(token_type, total, rhs);
                    if (total == 0)
                    {
                        printf("PARSE ERROR: Could not compute (parser.c:%d)\n", __LINE__);
                        return 0;
                    }

                    expecting_op = 1;
                }
            }
            else if (token_type == TOKEN_TYPE_OPMULTIPLY)
            {
                lexer_next_token(parser->lexer, 0); // consume +
                Value *rhs = parser_consume_expression(parser, TOKEN_TYPE_OPMULTIPLY);
                total = interpreter_apply_operator(token_type, total, rhs);
                if (total == 0)
                {
                    printf("PARSE ERROR: Could not compute (parser.c:%d)\n", __LINE__);
                    return 0;
                }

                expecting_op = 1;
            }
            else
            {
                // end of expression, I guess
                return total;
            }
        }
    }

    return 0;
}

int parser_consume_statement(Parser *parser, int do_execute)
{
    Lexer *lexer = parser->lexer;

    if (lexer->token.type == TOKEN_TYPE_RAW_TEXT)
    {
        if(streq(lexer->token.text, "print"))
        {
            lexer_next_token(parser->lexer, 0);

            // print statement
            Value *value = parser_consume_expression(parser, TOKEN_TYPE_OPADD);
            if (value == 0)
            {
                printf("PARSE ERROR: Could not evaluate expression (parser.c:%d)\n", __LINE__);
                return 0;
            }

            if (do_execute)
            {
                if (value->type == VALUE_TYPE_STRING)
                {
                    printf("%s\n", value->string_value);
                }
                else
                {
                    printf("%d\n", value->integer_value);
                }
            }
        }
        else if (streq(lexer->token.text, "set"))
        {
            lexer_next_token(parser->lexer, 0);

            if (parser->lexer->token.type != TOKEN_TYPE_NAME)
            {
                printf("PARSE ERROR: Expected name (parser.c:%d)\n", __LINE__);
                return 0;
            }

            char *name = save_string_to_heap(parser->lexer->token.text);

            lexer_next_token(parser->lexer, 0);

            if (parser->lexer->token.type != TOKEN_TYPE_OPASSIGN)
            {
                printf("PARSE ERROR: Expected '=' (parser.c:%d)\n", __LINE__);
                return 0;
            }
            
            lexer_next_token(parser->lexer, 0);

            Value *value = parser_consume_expression(parser, TOKEN_TYPE_OPADD);
            if (value == 0)
            {
                printf("PARSE ERROR: Could not evaluate expression (parser.c:%d)\n", __LINE__);
                return 0;
            }

            if (do_execute)
            {
                push_symbol(name, value);
            }
        }
        else
        {
            // host statement

            // notice that this works whether the token is a name or a string
            char *program_name = save_string_to_heap(parser->lexer->token.text);
            printf("Executing host program %s with arguments: ", program_name);

            // arguments
            lexer_next_token(parser->lexer, 1);
            int token_type = lexer->token.type;
            while (token_type == TOKEN_TYPE_RAW_TEXT)
            {
                // argument
                if (token_type == TOKEN_TYPE_RAW_TEXT)
                {
                    if (do_execute) printf("[%s] ", lexer->token.text);
                }
                lexer_next_token(lexer, 1);
                token_type = lexer->token.type;
            }
            if (do_execute) printf("\n");
        }
        
        return 1;
    }
    else
    {
        // error
        return 1;
    }
}

void parser_parse(Parser *parser, Lexer *lexer)
{
    int execute_flag = 1;
    int depth = 0;
    parser->lexer = lexer;
    lexer_next_token(lexer, 1); // prime lexer
    while (parser_consume_statement(parser, execute_flag))
    {
        int token_type = parser->lexer->token.type;
        if (token_type == TOKEN_TYPE_NEWLINE)
        {
            lexer_next_token(parser->lexer, 1);
        }
        else if (token_type == TOKEN_TYPE_CURLYOPEN)
        {
            depth += 1;
            lexer_next_token(parser->lexer, 1);
        }
        else if (token_type == TOKEN_TYPE_CURLYCLOSE)
        {
            depth -= 1;
            if (depth < 0)
            {
                printf("PARSE ERROR: Unbalanced curly braces (parser.c:%d)\n", __LINE__);
                return;
            }

            lexer_next_token(parser->lexer, 1);
        }
        else if (token_type == TOKEN_TYPE_EOF)
        {
            // done
            return;
        }
        else
        {
            // error
            printf("PARSE ERROR: Unexpected token after end of statement (parser.c:%d)\n", __LINE__);
            return;
        }
    }
}

void parser_print_tokens(Parser *parser, Lexer *lexer)
{
    parser->lexer = lexer;
    int shell_mode = 1;
    int is_first_token = 1;
    while (1)
    {
        int has_tokens = lexer_next_token(parser->lexer, shell_mode);
        if (!has_tokens) return;

        int token_type = parser->lexer->token.type;
        if (token_type == TOKEN_TYPE_RAW_TEXT)
        {
            if(streq(lexer->token.text, "print"))
            {
                shell_mode = 0;
                printf("<KEYW %s> ", lexer->token.text);
            }
            else if (streq(lexer->token.text, "set"))
            {
                shell_mode = 0;
                printf("<KEYW %s> ", lexer->token.text);
            }
            else
            {
                printf("<RAW %s> ", lexer->token.text);
            }
            is_first_token = 0;
        }
        else if (token_type == TOKEN_TYPE_NAME)
        {
            printf("<NAME %s> ", lexer->token.text);
        }
        else if (token_type == TOKEN_TYPE_NUMBER)
        {
            printf("<NUMBER %s> ", lexer->token.text);
            
        }
        else if (token_type == TOKEN_TYPE_STRING)
        {
            printf("<STRING %s> ", lexer->token.text);
        }
        else if (token_type == TOKEN_TYPE_OPASSIGN)
        {
            printf("<OP => ");
        }
        else if (token_type == TOKEN_TYPE_OPADD)
        {
            printf("<OP +> ");
        }
        else if (token_type == TOKEN_TYPE_OPMULTIPLY)
        {
            printf("<OP *> ");
        }
        else if (token_type == TOKEN_TYPE_PARENOPEN)
        {
            printf("<PARENOPEN> ");
        }
        else if (token_type == TOKEN_TYPE_PARENCLOSE)
        {
            printf("<PARENCLOSE> ");
        }
        else if (token_type == TOKEN_TYPE_NEWLINE)
        {
            printf("<NEWLINE>\n");
            shell_mode = 1;
            is_first_token = 1;  
        }
        else if (token_type == TOKEN_TYPE_CURLYOPEN)
        {
            printf("<CURLYOPEN>\n");
            shell_mode = 1;
            is_first_token = 1;  
        }
        else if (token_type == TOKEN_TYPE_CURLYCLOSE)
        {
            printf("<CURLYCLOSE>\n");
            shell_mode = 1;
            is_first_token = 1;  
        }
        else if (token_type == TOKEN_TYPE_EOF)
        {
            // done
            printf("<EOF>\n");
            return;
        }
        else
        {
            // error
            printf("PARSE ERROR: Unexpected token after end of statement (parser.c:%d)\n", __LINE__);
            return;
        }
    }
}

char input[1024 * 1024];

int main(int argc, char **argv)
{
    FILE *file = fopen("input.txt", "r");
    int input_length = fread(input, 1, sizeof(input), file);
        
    Lexer lexer;
    lexer_init(&lexer, input, input_length);

    int parse = 1;
    if (argc > 1 && argv[1][0] == 't') parse = 0;

    Parser parser;
    if (parse)
        parser_parse(&parser, &lexer);
    else
        parser_print_tokens(&parser, &lexer);
}