#include <stdio.h>
#include <string.h>


typedef int LexerChar;
const LexerChar LexerEOF = -1;

struct Token
{
    int type;
    char text[256];
    int i0, i1;
};

typedef struct Token Token;

struct Lexer
{
    const char *input;
    int index;
    int input_length;
    Token token;

    // state
    int state;
    int checkpoint;
};

typedef struct Lexer Lexer;

const int LEXER_STATE_INITIAL = 0;
const int LEXER_STATE_NONINITIAL  = 1;

enum TokenType
{
    TOKEN_TYPE_NAME,
    TOKEN_TYPE_STRING,
    TOKEN_TYPE_NUMBER,
    TOKEN_TYPE_NEWLINE,
    TOKEN_TYPE_RAW_TEXT,
    
    TOKEN_TYPESET_OP_FIRST,
    TOKEN_TYPE_OPASSIGN,
    TOKEN_TYPE_OPADD,
    TOKEN_TYPE_OPMULTIPLY,
    TOKEN_TYPE_OPLESSTHAN,
    TOKEN_TYPESET_OP_LAST,
    
    TOKEN_TYPE_PARENOPEN,
    TOKEN_TYPE_PARENCLOSE,
    TOKEN_TYPE_CURLYOPEN,
    TOKEN_TYPE_CURLYCLOSE,
    
    TOKEN_TYPE_EOF
};

void lexer_init(Lexer *lexer, char *file_contents, int length)
{
    lexer->input = file_contents;
    lexer->input_length = length;
    lexer->index = 0;
    lexer->state = LEXER_STATE_INITIAL;
    lexer->checkpoint = 0;
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

int lexer_next_token(Lexer *lexer, int shell_mode)
{
    lexer->checkpoint = lexer->index;
    Token *token = &lexer->token;
    while (1)
    {
        LexerChar c = lexer_peek(lexer);
        
        if (c == '\'' || c == '"')
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
        else if (c == '{')
        {
            token->type = TOKEN_TYPE_CURLYOPEN;
            token->text[0] = c;
            token->text[1] = 0;
            lexer_consume(lexer);
            return 1;
        }
        else if (c == '}')
        {
            token->type = TOKEN_TYPE_CURLYCLOSE;
            token->text[0] = c;
            token->text[1] = 0;
            lexer_consume(lexer);
            return 1;
        }
        else if (c == LexerEOF)
        {
            token->type = TOKEN_TYPE_EOF;
            return 1;
        }
        else
        {
            if (!shell_mode)
            {
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
                if (c >= '0' && c <= '9')
                {
                    // consume number
                    char *buffer = token->text;
                    token->type = TOKEN_TYPE_NUMBER;
                    token->i0 = lexer->index;
                    int index = 0;
                    while (1)
                    {
                        LexerChar c = lexer_peek(lexer);

                        if (c >= '0' && c <= '9')
                        {
                            buffer[index++] = c;
                            lexer_consume(lexer);
                        }
                        else
                        {
                            buffer[index++] = 0;
                            token->i1 = lexer->index;
                            return 1;
                        }
                    }
                }
                else if (c == '=')
                {
                    token->type = TOKEN_TYPE_OPASSIGN;
                    lexer_consume(lexer);
                    return 1;
                }
                else if (c == '+')
                {
                    token->type = TOKEN_TYPE_OPADD;
                    lexer_consume(lexer);
                    return 1;
                }
                else if (c == '*')
                {
                    token->type = TOKEN_TYPE_OPMULTIPLY;
                    lexer_consume(lexer);
                    return 1;
                }
                else if (c == '<')
                {
                    token->type = TOKEN_TYPE_OPLESSTHAN;
                    lexer_consume(lexer);
                    return 1;
                }
                else if (c == '(')
                {
                    token->type = TOKEN_TYPE_PARENOPEN;
                    lexer_consume(lexer);
                    return 1;
                }
                else if (c == ')')
                {
                    token->type = TOKEN_TYPE_PARENCLOSE;
                    lexer_consume(lexer);
                    return 1;
                }
                else
                {
                    shell_mode = 1;
                }
            }
            
            if (shell_mode)
            {
                // consume raw text
                char *buffer = token->text;
                token->type = TOKEN_TYPE_RAW_TEXT;
                token->i0 = lexer->index;
                int name_index = 0;
                while (1)
                {
                    LexerChar c = lexer_peek(lexer);
                    if (c == ' ' || c == '\n' || c == LexerEOF)
                    {
                        buffer[name_index] = 0;
                        token->i1 = lexer->index;
                        return 1;
                    }
                    else
                    {
                        buffer[name_index++] = c;
                        lexer_consume(lexer);
                    }
                }
            }
        }
    }
}

int lexer_next_language_token(Lexer *lexer)
{
    return lexer_next_token(lexer, 0);
}

int lexer_next_shell_token(Lexer *lexer)
{
    return lexer_next_token(lexer, 1);
}

int lexer_backtrack_and_go_again(Lexer *lexer, int shell_mode)
{
    lexer->index = lexer->checkpoint;
    return lexer_next_token(lexer, shell_mode);
}

// int c_main()
// {
//     char *input =
//         "print message\n"
//         "     ... print \"hel\\\"lo\"";

//     Lexer lexer;
//     lexer_init(&lexer, input, strlen(input));

//     Token token;
//     while (lexer_next_token(&lexer, &token))
//     {
//         if (token.type == TOKEN_TYPE_NAME)
//         {
//             printf("name: %s\n", token.text);
//         }
//         else if (token.type == TOKEN_TYPE_NEWLINE)
//         {
//             printf("newline\n");
//         }
//         else if (token.type == TOKEN_TYPE_STRING)
//         {
//             printf("string: %s\n", token.text);
//         }
//     }

//     return 0;
// }