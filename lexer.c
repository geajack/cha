#include <stdio.h>
#include <string.h>


typedef int LexerChar;
const LexerChar LexerEOF = -1;

struct Lexer
{
    const char *input;
    int index;
    int input_length;

    // state
    int state;
};

typedef struct Lexer Lexer;

const int LEXER_STATE_INITIAL = 0;
const int LEXER_STATE_NONINITIAL  = 1;

struct Token
{
    int type;
    char text[256];
    int i0, i1;
};

typedef struct Token Token;

const int TOKEN_TYPE_NAME = 0;
const int TOKEN_TYPE_STRING = 1;
const int TOKEN_TYPE_NEWLINE = 2;

void lexer_init(Lexer *lexer, char *file_contents, int length)
{
    lexer->input = file_contents;
    lexer->input_length = length;
    lexer->index = 0;
    lexer->state = LEXER_STATE_INITIAL;
}

LexerChar lexer_peek(Lexer *lexer)
{
    if (lexer->index >= lexer->input_length) return LexerEOF;
    else return lexer->input[lexer->index];
}

LexerChar lexer_consume(Lexer *lexer)
{
    if (lexer->index >= lexer->input_length) return LexerEOF;
    else return lexer->input[lexer->index++];
}

int lexer_match(Lexer *lexer, const char *string)
{
    int i = 0;
    while (string[i] != 0)
    {
        LexerChar c = lexer_consume(lexer);
        if (c != string[i])
        {
            return 0;
        }
        i += 1;
    }
    return 1;
}

int lexer_next_token(Lexer *lexer, Token *token)
{
    while (1)
    {
        LexerChar c = lexer_peek(lexer);

        if (c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
        {
            // consume name
            char *name_buffer = token->text;
            token->type = TOKEN_TYPE_NAME;
            token->i0 = lexer->index;
            int name_index = 0;
            while (1)
            {
                LexerChar c = lexer_peek(lexer);

                if (c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
                {
                    name_buffer[name_index++] = c;
                    lexer_consume(lexer);
                }
                else
                {
                    name_buffer[name_index] = 0;
                    token->i1 = lexer->index;
                    return 1;
                }
            }
        }
        else if (c == '\'' || c == '"')
        {
            // consume strings
            char quotemark = c;
            int escaped = 0;
            int string_index = 0;

            token->type = TOKEN_TYPE_STRING;
            token->i0 = lexer->index;

            lexer_consume(lexer);

            while (1)
            {
                LexerChar c = lexer_consume(lexer);

                if (c == '\n' || c == LexerEOF)
                {
                    // illegal
                    printf("Lexer error %d (lexer.c:%d)\n", c, __LINE__);
                    return 0;
                }

                if (escaped)
                {
                    if (c == 'n')
                    {
                        // append newline
                        token->text[string_index++] = '\n';
                    }
                    else if (c == '\'' || c == '"')
                    {
                        // append c
                        token->text[string_index++] = c;
                    }
                    else if (c == '\\')
                    {
                        // append backslash
                        token->text[string_index++] = '\\';
                    }
                    else
                    {
                        // illegal
                        printf("Lexer error %d (lexer.c:%d)\n", c, __LINE__);
                        return 0;   
                    }
                    escaped = 0;
                }
                else
                {
                    if (c == '\\')
                    {
                        escaped = 1;
                    }
                    else if (c == quotemark)
                    {
                        // end
                        token->text[string_index]  = 0;
                        token->i1 = lexer->index - 1;
                        return 1;
                    }
                    else
                    {
                        // append
                        token->text[string_index++] = c;
                    }
                }
            }
        }
        else if (c == '\n')
        {
            // consume whitespace until either "..." or anything else
            lexer_consume(lexer);
            int checkpoint = lexer->index;
            int n_dots = 0;
            int backtrack = 0;
            while (1)
            {
                LexerChar c = lexer_peek(lexer);
                if (c == ' ' || c == '\t')
                {
                    lexer_consume(lexer);
                    checkpoint = lexer->index; // unnecessary, but technically faster
                }
                else if (c == '.')
                {
                    if (lexer_match(lexer, "..."))
                    {
                        break;
                    }
                    else
                    {
                        backtrack = 1;
                    }
                }
                else
                {
                    backtrack = 1;
                    
                }

                if (backtrack)
                {
                    lexer->index = checkpoint;
                    token->type = TOKEN_TYPE_NEWLINE;
                    token->text[0] = '\n';
                    token->text[1] = 0;
                    token->i0 = checkpoint - 1;
                    token->i1 = checkpoint;
                    return 1;
                }
            }
        }
        else if (c == ' ' || c == '\t')
        {
            lexer_consume(lexer);
        }
        else if (c == LexerEOF)
        {
            return 0;
        }
        else
        {
            // illegal
            printf("Lexer error %d (lexer.c:%d)\n", c, __LINE__);
            return 0;
        }
    }
}

int c_main()
{
    char *input =
        "print message\n"
        "     ... print \"hel\\\"lo\"";

    Lexer lexer;
    lexer_init(&lexer, input, strlen(input));

    Token token;
    while (lexer_next_token(&lexer, &token))
    {
        if (token.type == TOKEN_TYPE_NAME)
        {
            printf("name: %s\n", token.text);
        }
        else if (token.type == TOKEN_TYPE_NEWLINE)
        {
            printf("newline\n");
        }
        else if (token.type == TOKEN_TYPE_STRING)
        {
            printf("string: %s\n", token.text);
        }
    }

    return 0;
}