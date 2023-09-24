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

struct Parser
{
    Lexer *lexer;
};

typedef struct Parser Parser;

Value *lookup_symbol(char *name)
{
    return 0;
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
    if (parser->lexer->token.type == TOKEN_TYPE_NAME)
    {
        Value *value = lookup_symbol(parser->lexer->token.text);
        return value;
    }
    else if (parser->lexer->token.type == TOKEN_TYPE_STRING)
    {
        char *string_value = save_string_to_heap(parser->lexer->token.text);
        Value *value = malloc(sizeof(Value));
        value->string_value = string_value;
        return value;
    }
    else
    {
        // error
        return 0;
    }
}

int parser_consume_statement(Parser *parser)
{
    Lexer *lexer = parser->lexer;

    if (lexer->token.type == TOKEN_TYPE_NAME)
    {
        if(streq(lexer->token.text, "print"))
        {
            lexer_next_token(parser->lexer);

            // print statement
            Value *value = parser_consume_expression(parser);
            printf("%s\n", value->string_value);
        }
        else
        {
            // host statement

            // notice that this works whether the token is a name or a string
            char *program_name = save_string_to_heap(parser->lexer->token.text);
            printf("Executing host program %s with arguments: ", program_name);

            // arguments
            lexer_next_token(parser->lexer);
            int token_type = lexer->token.type;
            while (token_type == TOKEN_TYPE_NAME || token_type == TOKEN_TYPE_STRING)
            {
                // argument
                if (token_type == TOKEN_TYPE_NAME)
                {
                    printf("%s ", lexer->token.text);
                }
                else if (token_type == TOKEN_TYPE_STRING)
                {
                    printf("%s ", lexer->token.text);
                }
                lexer_next_token(lexer);
                token_type = lexer->token.type;
            }
            printf("\n");
        }
        
        return 1;
    }
    else if (lexer->token.type == TOKEN_TYPE_EOF)
    {
        return 0;
    }
    else
    {
        // error
        return 0;
    }
}

void parser_parse(Parser *parser, Lexer *lexer)
{
    parser->lexer = lexer;
    lexer_next_token(lexer); // prime lexer
    while (parser_consume_statement(parser))
    {
        lexer_next_token(parser->lexer);
        int token_type = parser->lexer->token.type;
        if (token_type == TOKEN_TYPE_NEWLINE)
        {
            lexer_next_token(parser->lexer);
        }
        else if (token_type == TOKEN_TYPE_EOF)
        {
            // done
            return;
        }
        else
        {
            // error
            return;
        }
    }
}

int main()
{
    char *input =
        "print \"hello\"\n"
        "print \"message\"\n"
        "ffmpeg -i audio.mp3 converted.ogg";

    Lexer lexer;
    lexer_init(&lexer, input, strlen(input));

    Parser parser;
    parser_parse(&parser, &lexer);
}