#ifndef OP_TREE_H
#define OP_TREE_H

#include <antlr3.h>
#include <stdio.h>

typedef enum {
    OP_ASSIGNMENT,
    OP_ADDITION,
    OP_SUBTRACTION,
    OP_MULTIPLICATION,
    OP_DIVISION,
    OP_MODULO,
    OP_LOGICAL_AND,
    OP_LOGICAL_OR,
    OP_EQUAL,
    OP_NOT_EQUAL,
    OP_LESS_THAN,
    OP_LESS_THAN_OR_EQUAL,
    OP_GREATER_THAN,
    OP_GREATER_THAN_OR_EQUAL,
    OP_UNARY_PLUS,
    OP_UNARY_MINUS,
    OP_LOGICAL_NOT,
    OP_FUNCTION_CALL,
    OP_ARRAY_INDEX,
    OP_IDENTIFIER,
    OP_LITERAL,
    OP_UNKNOWN
} OpType;

typedef struct OpNode {
    OpType type;
    struct OpNode** operands;
    int operand_count;
    char* text;
} OpNode;

OpNode* buildOpTree(pANTLR3_BASE_TREE node);
void freeOpTree(OpNode* node);

void printOpTree(const OpNode* node, int indent);
void opTreeToDot(const OpNode* node, FILE* out);
char* opTreeToString(const OpNode* node);

const char* opTypeToString(OpType type);

#endif
