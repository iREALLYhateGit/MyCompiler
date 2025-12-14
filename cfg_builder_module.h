#ifndef CFG_BUILDER_MODULE_H
#define CFG_BUILDER_MODULE_H

#include <antlr3.h>

// Новые структуры для CFG
typedef enum {
    NODE_BASIC_BLOCK,
    NODE_ENTRY,
    NODE_EXIT,
    NODE_IF,
    NODE_WHILE,
    NODE_REPEAT_CONDITION,
    NODE_BREAK,
    NODE_RETURN
} NodeType;


typedef struct CFGNode {
    int id;
    NodeType type;
    const char* label;  // Для отображения
    struct CFGNode** successors;
    int succ_count;
    struct CFGNode** predecessors;
    int pred_count;
    // Для basic block
    pANTLR3_BASE_TREE* statements;
    int stmt_count;
} CFGNode;

typedef struct {
    CFGNode** exits;
    int exit_count;
} FlowResult;

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
void cfgToDot(ControlFlowGraph* cfg, FILE* out);

#endif
