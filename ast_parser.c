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
    char *memory = (char*) malloc(length);
    strcpy(memory, string);
    return memory;
}

struct Parser
{
    Lexer *lexer;
};

typedef struct Parser Parser;

int OP_PRECEDENCE_NONE = 0;
int OP_PRECEDENCE_ADD = 1;
int OP_PRECEDENCE_MULTIPLY = 2;

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
                expression->type = NAME_NODE;
                expression->name = save_string_to_heap(token->text);

                expecting_op = 1;
                lexer_next_token(parser->lexer, 0);
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
            int token_is_operator = token_type == TOKEN_TYPE_OPADD || token_type == TOKEN_TYPE_OPMULTIPLY;
            
            int this_precedence;
            if (token_type <= TOKEN_TYPE_OPADD) this_precedence = OP_PRECEDENCE_ADD;
            else if (token_type <= TOKEN_TYPE_OPMULTIPLY) this_precedence = OP_PRECEDENCE_MULTIPLY;

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

ASTNode *parser_consume_statement(Parser *parser)
{
    Lexer *lexer = parser->lexer;

    ASTNode *statement = 0;

    int done = 0;
    while (!done)
    {
        if (lexer->token.type == TOKEN_TYPE_RAW_TEXT)
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
                name_node->next_sibling = rhs;

                statement->first_child = name_node;
            }
            else if (streq(text, "if"))
            {
                // if statement
                statement = alloc_ast_node(IF_NODE, 0);

                lexer_next_language_token(parser->lexer);

                ASTNode *condition = parser_consume_expression(parser, OP_PRECEDENCE_NONE);
                condition->parent = statement;

                statement->first_child = condition;
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
        else
        {
            done = 1;
        }
    }

    return statement;
}

ASTNode *parse(char *input, int input_length)
{
    Lexer lexer[1];
    lexer_init(lexer, input, input_length);
    
    Parser parser[1];
    parser->lexer = lexer;

    ASTNode *program = alloc_ast_node(PROGRAM_NODE, 0);
    ASTNode *previous = program;
    int is_first_child = 1;
    int only_one_child = 0;

    lexer_next_shell_token(lexer);
    while (1)
    {
        ASTNode *node = parser_consume_statement(parser);

        if (node)
        {
            if (is_first_child)
            {
                previous->first_child = node;
                node->parent = previous;
                is_first_child = 0;

                if (only_one_child)
                {
                    previous = previous->parent;
                }
            }
            else
            {
                previous->next_sibling = node;
                node->parent = previous->next_sibling->parent;
            }

            previous = node;

            if (node->type == IF_NODE)
            {                
                is_first_child = 1;
                only_one_child = 1;
            }
        }

        int t = parser->lexer->token.type;
        if (t == TOKEN_TYPE_NEWLINE)
        {
            lexer_next_shell_token(lexer);
        }
        else if (t == TOKEN_TYPE_CURLYOPEN)
        {
            ASTNode *block = alloc_ast_node(CODEBLOCK_NODE, 0);
            
            if (is_first_child)
            {
                previous->first_child = block;
                block->parent = previous;                
            }
            else
            {
                previous->next_sibling = block;
                block->parent = previous->next_sibling->parent;
            }

            is_first_child = 1;
            only_one_child = 0;

            lexer_next_shell_token(lexer);
        }
        else if (t == TOKEN_TYPE_CURLYCLOSE)
        {
            previous = previous->parent;
            is_first_child = 0;
            lexer_next_shell_token(lexer);
        }
        else if (t == TOKEN_TYPE_EOF)
        {
            return program;
        }
        else
        {
            lexer_backtrack_and_go_again(lexer, 1);
        }
    }
}

char input[1024 * 1024];

int main(int argc, char **argv)
{
    FILE *file = fopen("input.txt", "r");
    int input_length = fread(input, 1, sizeof(input), file);
    
    ASTNode *tree = parse(input, input_length);

    print_ast(tree);
}