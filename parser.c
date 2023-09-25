#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.c"

int streq(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

struct Value
{
    int type;
    char *string_value;
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

void parser_init(Parser *parser)
{}

Value *parser_consume_expression(Parser *parser)
{
    return 0;
}

int parser_consume_statement(Parser *parser)
{
    Lexer *lexer = parser->lexer;

    if (lexer->token.type == TOKEN_TYPE_RAW_TEXT)
    {
        if(streq(lexer->token.text, "print"))
        {
            lexer_next_token(parser->lexer, 0);

            // print statement
            Value *value = parser_consume_expression(parser);
            if (value == 0)
            {
                printf("PARSE ERROR: Could not evaluate expression (parser.c:%d)\n", __LINE__);
                return 0;
            }

            printf("%s\n", value->string_value);
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

            Value *value = parser_consume_expression(parser);
            if (value == 0)
            {
                printf("PARSE ERROR: Could not evaluate expression (parser.c:%d)\n", __LINE__);
                return 0;
            }

            push_symbol(name, value);
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
                    printf("[%s] ", lexer->token.text);
                }
                lexer_next_token(lexer, 1);
                token_type = lexer->token.type;
            }
            printf("\n");
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
    parser->lexer = lexer;
    lexer_next_token(lexer, 1); // prime lexer
    while (parser_consume_statement(parser))
    {
        int token_type = parser->lexer->token.type;
        if (token_type == TOKEN_TYPE_NEWLINE)
        {
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
            }
            else if (streq(lexer->token.text, "set"))
            {
                shell_mode = 0;
            }
            printf("<RAW %s> ", lexer->token.text);
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

int main(int argc, char **argv)
{
    char *input =
        "set value = 5 + 5 * 10\n"
        "set message = \"hello world\"\n"
        "print \"hello\"\n"
        "print message\n"
        "./ffmpeg\n"
        "    ... -i audio.mp3\n"
        "    ... converted.ogg";

    Lexer lexer;
    lexer_init(&lexer, input, strlen(input));

    int parse = 1;
    if (argc > 1 && argv[1][0] == 't') parse = 0;

    Parser parser;
    if (parse)
        parser_parse(&parser, &lexer);
    else
        parser_print_tokens(&parser, &lexer);
}