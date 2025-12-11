#ifndef PARSER_MODULE_H
#define PARSER_MODULE_H

#include <antlr3.h>
#include "GrammarLexer.h"
#include "GrammarParser.h"

typedef struct {
    pANTLR3_BASE_TREE tree;           // AST root
    char** errors;                    // array of error messages
    int errorCount;

    // ANTLR objects needed for proper cleanup
    pGrammarParser parser;
    pANTLR3_COMMON_TOKEN_STREAM tokens;
    pGrammarLexer lexer;
    pANTLR3_INPUT_STREAM input;
} ParseResult;

ParseResult parseFile(const char* filename);
void freeParseResult(ParseResult* result);
void printTree(pANTLR3_BASE_TREE tree, int indent);
void treeToDot(pANTLR3_BASE_TREE tree, FILE* out, int* nodeId);


// Новые структуры для CFG
typedef enum {
    NODE_BASIC_BLOCK,
    NODE_ENTRY,
    NODE_EXIT,
    NODE_IF,
    NODE_IF_ELSE,
    NODE_CONDITION,
    NODE_THEN,
    NODE_ELSE,
    NODE_MERGE,
    NODE_BLOCK,
    NODE_WHILE_ENTRY,
    NODE_WHILE_BODY,
    NODE_WHILE_EXIT,
    NODE_REPEAT_ENTRY,
    NODE_REPEAT_BODY,
    NODE_REPEAT_EXIT,
    NODE_BREAK,
    NODE_RETURN
} NodeType;

typedef struct CFGNode {
    int id;
    NodeType type;
    char* label;  // Для отображения
    struct CFGNode** successors;
    int succ_count;
    struct CFGNode** predecessors;
    int pred_count;
    // Для привязки к AST
    pANTLR3_BASE_TREE ast_node;
    // Для basic block
    pANTLR3_BASE_TREE* statements;
    int stmt_count;
} CFGNode;

typedef struct {
    CFGNode* entry;
    CFGNode* exit;
    CFGNode** nodes;
    int node_count;
    int max_nodes;
} ControlFlowGraph;

// Функции для работы с CFG
ControlFlowGraph* buildCFG(pANTLR3_BASE_TREE tree);
void printCFG(ControlFlowGraph* cfg, const char* filename);
void freeCFG(ControlFlowGraph* cfg);

#endif
