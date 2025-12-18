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
    // Для basic block
    pANTLR3_BASE_TREE* statements;
    int stmt_count;
    struct CFGNode* nextDefault;
    struct CFGNode* nextConditional;
} CFGNode;


// Типы ребер CFG
typedef enum {
    EDGE_CLASSIC,   // Обычное ребро потока управления
    EDGE_TRUE,      // Ребро для true ветви условия
    EDGE_FALSE,     // Ребро для false ветви условия
    EDGE_BREAK,     // Ребро для break
    EDGE_CONTINUE   // Ребро для continue (если будет)
} EdgeType;

// Структура для хранения ребра с меткой
typedef struct CFGEdge {
    CFGNode* from;
    CFGNode* to;
    EdgeType type;
} CFGEdge;

typedef struct {
    CFGNode** exits;
    CFGEdge** exitsEdges;
    int exit_count;
} FlowResult;

typedef struct {
    CFGNode* entry;
    CFGNode* exit;
    CFGNode** nodes;
    int node_count;
    int max_nodes;
    CFGEdge** edges;
    int edge_count;
    int max_edges;
} ControlFlowGraph;

// Функции для работы с CFG
ControlFlowGraph* buildCFG(pANTLR3_BASE_TREE tree);
void cfgToDot(ControlFlowGraph* cfg, FILE* out);
void freeCFG(ControlFlowGraph* cfg);

#endif
