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

    ASTNode *expression = alloc_ast_node(PROGRAM_NODE, 0);

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

            if (token_is_operator && precedence <= this_precedence)
            {
                lexer_next_token(parser->lexer, 0); // consume +
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
                else
                {
                    printf("PARSE ERROR: Unexpected operator (parser.c:%d)\n", __LINE__);
                }

                ASTNode *operation = alloc_ast_node(operator_node_type, 0);
                operation->first_child = expression;
                expression->next_sibling = rhs;

                expression->parent = operation;
                rhs->parent = operation;

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
        statement = alloc_ast_node(CODEBLOCK_NODE, 0);        
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
            statement = alloc_ast_node(PRINT_NODE, 0);

            lexer_next_token(parser->lexer, 0);
            ASTNode *argument = parser_consume_expression(parser, OP_PRECEDENCE_NONE);
            
            statement->first_child = argument;
        }
        else if (streq(text, "set"))
        {
            // set statement
            statement = alloc_ast_node(SET_NODE, 0);

            lexer_next_language_token(parser->lexer);

            if (parser->lexer->token.type != TOKEN_TYPE_NAME)
            {
                printf("PARSE ERROR: Expected name (parser.c:%d)\n", __LINE__);
            }

            char *name = save_string_to_heap(parser->lexer->token.text);
            ASTNode *name_node = alloc_ast_node(NAME_NODE, statement);
            name_node->name = name;

            lexer_next_token(parser->lexer, 0);

            if (parser->lexer->token.type != TOKEN_TYPE_OPASSIGN)
            {
                printf("PARSE ERROR: Expected '=' (parser.c:%d)\n", __LINE__);
            }
            
            lexer_next_token(parser->lexer, 0);

            ASTNode *rhs = parser_consume_expression(parser, OP_PRECEDENCE_NONE);
            rhs->parent = statement;
            name_node->parent = statement;
            statement->first_child = rhs; // deliberate - we always pack expressions "to the left".
                                          // The interpreter depends on this convention.
            statement->first_child->next_sibling = name_node;
        }
        else if (streq(text, "if"))
        {
            // if statement
            statement = alloc_ast_node(IF_NODE, 0);

            lexer_next_language_token(parser->lexer);

            ASTNode *condition = parser_consume_expression(parser, OP_PRECEDENCE_NONE);
            condition->parent = statement;

            statement->first_child = condition;

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
            condition->next_sibling = body;
            body->parent = statement;            
        }
        else if (streq(text, "while"))
        {
            // while statement
            statement = alloc_ast_node(WHILE_NODE, 0);

            lexer_next_language_token(parser->lexer);

            ASTNode *condition = parser_consume_expression(parser, OP_PRECEDENCE_NONE);
            condition->parent = statement;

            statement->first_child = condition;

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
            condition->next_sibling = body;
            body->parent = statement;
        }
        else
        {
            // host statement
            statement = alloc_ast_node(HOST_NODE, 0);

            char *program = save_string_to_heap(text);
            ASTNode *program_node = alloc_ast_node(RAW_TEXT_NODE, statement);
            program_node->string = program;
            statement->first_child = program_node;

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
                    argument = alloc_ast_node(RAW_TEXT_NODE, statement);
                    argument->string = save_string_to_heap(lexer->token.text);
                }
                else
                {
                    argument = alloc_ast_node(STRING_NODE, statement);
                    argument->string = save_string_to_heap(lexer->token.text);
                }

                previous->next_sibling = argument;
                previous = argument;

                lexer_next_shell_token(parser->lexer);
                t = lexer->token.type;
            }
        }
    }

    return statement;
}

ASTAttachmentPoint *parser_pop(Parser *parser)
{
    ASTAttachmentPoint *top = &parser->stack[parser->stack_size - 1];
    parser->stack_size -= 1;
    return top;
}

void parser_push(Parser *parser, ASTNode **node, ASTNode *parent)
{
    parser->stack[parser->stack_size].target = node;
    parser->stack[parser->stack_size].parent = parent;
    parser->stack_size += 1;
}

void parser_attach(Parser *parser, ASTNode *node)
{
    ASTAttachmentPoint *attach = parser_pop(parser);
    *(attach->target) = node;
    node->parent = attach->parent;

    if (!parser->just_opened_if_statement)
    {
        parser_push(parser, &node->next_sibling, node->parent);
    }
    parser->just_opened_if_statement = 0;
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
            ASTNode *pipe = alloc_ast_node(PIPE_NODE, 0);
            pipe->first_child = statement;
            statement->parent = pipe;
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
            previous->next_sibling = statement_or_pipe;
            statement_or_pipe->parent = previous->parent;
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
                previous->first_child = statement_chain;
            }
            else
            {
                previous->next_sibling = statement_chain;
            }
            statement_chain->parent = root;

            previous = statement_chain;
            have_first = 1;
        }

        switch (parser->lexer->token.type)
        {
            case TOKEN_TYPE_CURLYOPEN:
            lexer_next_shell_token(parser->lexer);

            ASTNode *code_block = alloc_ast_node(CODEBLOCK_NODE, 0);            
            if (!have_first)
            {
                code_block->parent = previous;
                previous->first_child = code_block;
            }
            else
            {
                code_block->parent = previous->parent;
                previous->next_sibling = code_block;
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

    ASTNode *program = alloc_ast_node(PROGRAM_NODE, 0);
    lexer_next_shell_token(parser->lexer);
    parse_statements(parser, program);

    return program;
}