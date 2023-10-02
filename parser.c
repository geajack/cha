#include <stdio.h>

#include "lexer.c"
#include "ast.c"

int streq(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

char *save_string_to_heap(char *string)
{
    int length = strlen(string);
    char *memory = (char*) malloc(length + 1);
    strcpy(memory, string);
    return memory;
}

struct ASTAttachmentPoint
{
    ASTNode **target;
    ASTNode *parent;
};

typedef struct ASTAttachmentPoint ASTAttachmentPoint;

struct Parser
{
    Lexer *lexer;
    
    ASTAttachmentPoint stack[64];
    int just_opened_if_statement;
    int stack_size;
};

typedef struct Parser Parser;

int OP_PRECEDENCE_NONE = 0;
int OP_PRECEDENCE_COMPARISON = 1;
int OP_PRECEDENCE_ADD = 2;
int OP_PRECEDENCE_MULTIPLY = 3;

ASTNode *parser_consume_expression(Parser *parser, int precedence)
{
    Lexer *lexer = parser->lexer;

    ASTNode *expression = alloc_ast_node(PROGRAM_NODE);

    int done = 0;
    int expecting_op = 0;
    while (!done)
    {
        Token *token = &parser->lexer->token;
        int token_type = parser->lexer->token.type;
        if (!expecting_op)
        {
            if (token_type == TOKEN_TYPE_NAME)
            {
                char *name = save_string_to_heap(token->text);
                lexer_next_language_token(parser->lexer);

                if (parser->lexer->token.type == TOKEN_TYPE_PARENOPEN)
                {
                    // must be a function call                    
                    lexer_next_language_token(parser->lexer); // consume open paren
                    if (parser->lexer->token.type != TOKEN_TYPE_PARENCLOSE)
                    {
                        // we don't support arguments right now
                        printf("PARSE ERROR: Function arguments are not supported (parser.c:%d)\n", __LINE__);
                        return 0;
                    }

                    lexer_next_language_token(parser->lexer); // consume close paren
                    expression->type = FUNCTION_CALL_NODE;
                    expression->name = name;
                }
                else
                {
                    expression->type = NAME_NODE;
                    expression->name = name;
                }

                expecting_op = 1;
            }
            else if (token_type == TOKEN_TYPE_STRING)
            {
                expression->type = STRING_NODE;
                expression->string = save_string_to_heap(parser->lexer->token.text);
                expecting_op = 1;
                lexer_next_token(parser->lexer, 0);
            }
            else if (token_type == TOKEN_TYPE_NUMBER)
            {
                expression->type = NUMBER_NODE;

                int integer_value;
                {
                    char *text = token->text;
                    int i = 0;
                    integer_value = 0;
                    while (text[i])               
                    {
                        int digit = text[i] - '0';
                        integer_value = integer_value * 10 + digit;
                        i += 1;
                    }
                }
                expression->number = integer_value;

                expecting_op = 1;
                lexer_next_token(parser->lexer, 0);
            }
            else if (token_type == TOKEN_TYPE_PARENOPEN)
            {
                lexer_next_token(parser->lexer, 0); // consume paren
                expression = parser_consume_expression(parser, OP_PRECEDENCE_NONE);
                if (parser->lexer->token.type != TOKEN_TYPE_PARENCLOSE)
                {
                    printf("PARSE ERROR: Expected right parenthesis (parser.c:%d)\n", __LINE__);
                }
                lexer_next_token(parser->lexer, 0); // consume close paren                
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
            int token_is_operator = TOKEN_TYPESET_OP_FIRST < token_type && token_type < TOKEN_TYPESET_OP_LAST;
            
            int this_precedence;
            if (token_type == TOKEN_TYPE_OPADD) this_precedence = OP_PRECEDENCE_ADD;
            else if (token_type == TOKEN_TYPE_OPMULTIPLY) this_precedence = OP_PRECEDENCE_MULTIPLY;
            else if (token_type == TOKEN_TYPE_OPLESSTHAN) this_precedence = OP_PRECEDENCE_COMPARISON;
            else if (token_type == TOKEN_TYPE_OPEQUALS) this_precedence = OP_PRECEDENCE_COMPARISON;

            if (token_is_operator && precedence <= this_precedence)
            {
                lexer_next_token(parser->lexer, 0); // consume operator
                ASTNode *rhs = parser_consume_expression(parser, this_precedence);
                
                enum ASTNodeType operator_node_type;
                if (token_type == TOKEN_TYPE_OPADD)
                {
                    operator_node_type = ADD_NODE;
                }
                else if (token_type == TOKEN_TYPE_OPMULTIPLY)
                {
                    operator_node_type = MULTIPLY_NODE;
                }
                else if (token_type == TOKEN_TYPE_OPLESSTHAN)
                {
                    operator_node_type = LESSTHAN_NODE;
                }
                else if (token_type == TOKEN_TYPE_OPEQUALS)
                {
                    operator_node_type = EQUALS_NODE;
                }
                else
                {
                    printf("PARSE ERROR: Unexpected operator (parser.c:%d)\n", __LINE__);
                }

                ASTNode *operation = alloc_ast_node(operator_node_type);                
                ast_attach_child(operation, expression);
                ast_attach_sibling(expression, rhs);

                expression = operation;                
                expecting_op = 1;
            }
            else
            {
                return expression;
            }
        }
    }

    return expression;
}

void parse_statements(Parser *parser, ASTNode *root);

ASTNode *parser_consume_statement(Parser *parser)
{
    Lexer *lexer = parser->lexer;

    ASTNode *statement = 0;

    if (lexer->token.type == TOKEN_TYPE_CURLYOPEN)
    {
        lexer_next_shell_token(lexer);
        statement = alloc_ast_node(CODEBLOCK_NODE);        
        parse_statements(parser, statement);
        if (parser->lexer->token.type != TOKEN_TYPE_CURLYCLOSE)
        {
            printf("PARSE ERROR: Expected '}' (parser.c:%d)\n", __LINE__);
            return 0;
        }
        lexer_next_shell_token(lexer);
    }
    else if (lexer->token.type == TOKEN_TYPE_RAW_TEXT)
    {
        char *text = lexer->token.text;
        if(streq(text, "print"))
        {
            // print statement
            statement = alloc_ast_node(PRINT_NODE);

            lexer_next_token(parser->lexer, 0);
            ASTNode *argument = parser_consume_expression(parser, OP_PRECEDENCE_NONE);
            
            ast_attach_child(statement, argument);
        }
        else if (streq(text, "set"))
        {
            // set statement
            statement = alloc_ast_node(SET_NODE);

            lexer_next_language_token(parser->lexer);

            if (parser->lexer->token.type != TOKEN_TYPE_NAME)
            {
                printf("PARSE ERROR: Expected name (parser.c:%d)\n", __LINE__);
            }

            char *name = save_string_to_heap(parser->lexer->token.text);
            ASTNode *name_node = alloc_ast_node(NAME_NODE);
            name_node->name = name;

            lexer_next_token(parser->lexer, 0);

            if (parser->lexer->token.type != TOKEN_TYPE_OPASSIGN)
            {
                printf("PARSE ERROR: Expected '=' (parser.c:%d)\n", __LINE__);
            }
            
            lexer_next_token(parser->lexer, 0);

            ASTNode *rhs = parser_consume_expression(parser, OP_PRECEDENCE_NONE);
            ast_attach_child(statement, rhs); // deliberate - we always pack expressions "to the left".
                                              // The interpreter depends on this convention.
            ast_attach_sibling(rhs, name_node);
        }
        else if (streq(text, "if"))
        {
            // if statement
            statement = alloc_ast_node(IF_NODE);

            lexer_next_language_token(parser->lexer);

            ASTNode *condition = parser_consume_expression(parser, OP_PRECEDENCE_NONE);
            
            ast_attach_child(statement, condition);

            lexer_backtrack_and_go_again(lexer, 1);
            ASTNode *body = 0;
            while (!body)
            {
                body = parser_consume_statement(parser);
                if (!body)
                {
                    lexer_next_shell_token(lexer);
                }
            }

            ast_attach_sibling(condition, body);
        }
        else if (streq(text, "while"))
        {
            // while statement
            statement = alloc_ast_node(WHILE_NODE);

            lexer_next_language_token(parser->lexer);

            ASTNode *condition = parser_consume_expression(parser, OP_PRECEDENCE_NONE);
            
            ast_attach_child(statement, condition);

            lexer_backtrack_and_go_again(lexer, 1);
            ASTNode *body = 0;
            while (!body)
            {
                body = parser_consume_statement(parser);
                if (!body)
                {
                    lexer_next_shell_token(lexer);
                }
            }
            
            ast_attach_sibling(condition, body);
        }
        else
        {
            // host statement
            statement = alloc_ast_node(HOST_NODE);

            char *program = save_string_to_heap(text);
            ASTNode *program_node = alloc_ast_node(RAW_TEXT_NODE);
            program_node->string = program;
            
            ast_attach_child(statement, program_node);

            // arguments
            lexer_next_shell_token(parser->lexer);
            int t = lexer->token.type;
            ASTNode *previous = program_node;
            while (t == TOKEN_TYPE_RAW_TEXT || t == TOKEN_TYPE_STRING)
            {
                // argument
                ASTNode *argument;
                if (t == TOKEN_TYPE_RAW_TEXT)
                {
                    argument = alloc_ast_node(RAW_TEXT_NODE);
                    argument->string = save_string_to_heap(lexer->token.text);
                }
                else
                {
                    argument = alloc_ast_node(STRING_NODE);
                    argument->string = save_string_to_heap(lexer->token.text);
                }

                ast_attach_sibling(previous, argument);

                previous = argument;

                lexer_next_shell_token(parser->lexer);
                t = lexer->token.type;
            }
        }
    }

    return statement;
}

ASTNode *parser_consume_statement_chain(Parser *parser)
{
    ASTNode *chain = 0;
    ASTNode *previous = 0;

    int expecting_op = 0;
    int done = 0;
    while (!done)
    {
        ASTNode *statement = parser_consume_statement(parser);
        ASTNode *statement_or_pipe = statement;

        switch (parser->lexer->token.type)
        {
            case TOKEN_TYPE_PIPE:
            lexer_next_shell_token(parser->lexer);
            ASTNode *pipe = alloc_ast_node(PIPE_NODE);
            ast_attach_child(pipe, statement);
            statement_or_pipe = pipe;
            break;

            case TOKEN_TYPE_NEWLINE:
            case TOKEN_TYPE_CURLYOPEN:
            case TOKEN_TYPE_CURLYCLOSE:
            case TOKEN_TYPE_EOF:
            done = 1;
            break;

            default:
            printf("PARSE ERROR: Unexpected token (%d) (parser.c:%d)\n", parser->lexer->token.type, __LINE__);
            done = 1;
            break;
        }

        if (chain == 0) chain = statement_or_pipe;

        if (previous)
        {
            ast_attach_sibling(previous, statement_or_pipe);
        }
        
        previous = statement;
    }

    return chain;
}

void parse_statements(Parser *parser, ASTNode *root)
{
    int have_first = 0;
    ASTNode *previous = root;

    int done = 0;
    while (!done)
    {
        ASTNode *statement_chain = parser_consume_statement_chain(parser);

        if (statement_chain)
        {
            if (!have_first)
            {
                ast_attach_child(previous, statement_chain);
            }
            else
            {
                ast_attach_sibling(previous, statement_chain);
            }

            previous = statement_chain;
            have_first = 1;
        }

        switch (parser->lexer->token.type)
        {
            case TOKEN_TYPE_CURLYOPEN:
            lexer_next_shell_token(parser->lexer);

            ASTNode *code_block = alloc_ast_node(CODEBLOCK_NODE);            
            if (!have_first)
            {
                ast_attach_child(previous, code_block);
            }
            else
            {
                ast_attach_sibling(previous, code_block);
            }

            parse_statements(parser, code_block);

            if (parser->lexer->token.type != TOKEN_TYPE_CURLYCLOSE)
            {
                printf("PARSE ERROR: Expected '}' (parser.c:%d)\n", __LINE__);
            }

            lexer_next_language_token(parser->lexer);
            break;

            case TOKEN_TYPE_NEWLINE:
            lexer_next_shell_token(parser->lexer);
            break;

            case TOKEN_TYPE_CURLYCLOSE:
            done = 1;
            break;

            case TOKEN_TYPE_EOF:
            done = 1;
            break;
        }
    }
}

ASTNode *parse(char *input, int input_length)
{
    Lexer lexer[1];
    lexer_init(lexer, input, input_length);
    
    Parser parser[1];
    parser->lexer = lexer;

    ASTNode *program = alloc_ast_node(PROGRAM_NODE);
    lexer_next_shell_token(parser->lexer);
    parse_statements(parser, program);

    return program;
}