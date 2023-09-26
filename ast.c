enum ASTNodeType
{
    PROGRAM_NODE,
    CODEBLOCK_NODE,
    PRINT_NODE,
    SET_NODE,
    IF_NODE,
    HOST_NODE,
    ADD_NODE,
    MULTIPLY_NODE,
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