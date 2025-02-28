#include <stdio.h>
#include <stdlib.h>

enum ASTNodeType
{
    PROGRAM_NODE,
    CODEBLOCK_NODE,
    PRINT_NODE,
    EXIT_NODE,
    SET_NODE,
    IF_NODE,
    WHILE_NODE,
    HOST_NODE,
    FUNCTION_CALL_NODE,
    ADD_NODE,
    MULTIPLY_NODE,
    LESSTHAN_NODE,
    EQUALS_NODE,
    OR_NODE,
    PIPE_NODE,
    NAME_NODE,
    NUMBER_NODE,
    STRING_NODE,
    RAW_TEXT_NODE
};

struct ASTNode
{
    enum ASTNodeType type;
    union
    {
        char *name;
        char *string;
        int number;
    };
    struct ASTNode *parent;
    struct ASTNode *first_child;
    struct ASTNode *next_sibling;
};

typedef struct ASTNode ASTNode;

ASTNode *alloc_ast_node(enum ASTNodeType type)
{
    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = type;
    node->first_child = 0;
    node->next_sibling = 0;
    return node;
}

void ast_attach_child(ASTNode *parent, ASTNode *child)
{
    parent->first_child = child;
    child->parent = parent;
}

void ast_attach_sibling(ASTNode *older, ASTNode *younger)
{
    older->next_sibling = younger;
    younger->parent = older->parent;
}

void print_ast_indented(ASTNode *tree, int indent)
{
    int n_indented = 0;
    while (n_indented < indent)
    {
        putc(' ', stdout);
        n_indented += 1;
    }

    switch (tree->type)
    {
        case PROGRAM_NODE:
            printf("PROGRAM");
        break;
        
        case CODEBLOCK_NODE:
            printf("CODEBLOCK");
        break;

        case PRINT_NODE:
            printf("PRINT");
        break;

        case EXIT_NODE:
            printf("EXIT");
        break;

        case SET_NODE:
            printf("SET");
        break;

        case IF_NODE:
            printf("IF");
        break;

        case WHILE_NODE:
            printf("WHILE");
        break;

        case HOST_NODE:
            printf("HOST");
        break;

        case FUNCTION_CALL_NODE:
            printf("FUNCCALL [%s]", tree->name);
        break;

        case ADD_NODE:
            printf("ADD");
        break;

        case MULTIPLY_NODE:
            printf("MULTIPLY");
        break;

        case LESSTHAN_NODE:
            printf("LESS THAN");
        break;

        case EQUALS_NODE:
            printf("EQUALS");
        break;

        case OR_NODE:
            printf("OR");
        break;

        case PIPE_NODE:
            printf("PIPE");
        break;

        case NAME_NODE:
            printf("NAME [%s]", tree->name);
        break;

        case NUMBER_NODE:
            printf("NUMBER [%d]", tree->number);
        break;

        case STRING_NODE:
            printf("STRING [%s]", tree->string);
        break;

        case RAW_TEXT_NODE:
            printf("RAW_TEXT [%s]", tree->string);
        break;
    }

    printf("\n");

    ASTNode *child = tree->first_child;
    while (child)
    {
        print_ast_indented(child, indent + 2);
        child = child->next_sibling;
    }
}

void print_ast(ASTNode *tree)
{
    print_ast_indented(tree, 0);
}