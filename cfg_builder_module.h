#ifndef CFG_BUILDER_MODULE_H
#define CFG_BUILDER_MODULE_H

#include <antlr3.h>
#include <stdbool.h>
#include <stdio.h>

#include "op_tree.h"

// New structures for CFG
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
    // For basic block
    OpNode** statements;
    int stmt_count;
    struct CFGNode* nextDefault;
    struct CFGNode* nextConditional;
} CFGNode;

// CFG edge types
typedef enum {
    EDGE_CLASSIC,
    EDGE_TRUE,
    EDGE_FALSE,
    EDGE_BREAK,
    EDGE_CONTINUE
} EdgeType;

// Labeled CFG edge
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

typedef struct ControlFlowGraph {
    CFGNode* entry;
    CFGNode* exit;
    CFGNode** nodes;
    int node_count;
    int max_nodes;
    CFGEdge** edges;
    int edge_count;
    int max_edges;
} ControlFlowGraph;

typedef struct {
    char* name;
    char** param_names;
    char** param_types;
    int param_count;
    char* return_type;
    char** local_names;
    char** local_types;
    int local_count;
    char* source_file;
    ControlFlowGraph* cfg;
    bool has_body;
} SubprogramInfo;

typedef struct {
    SubprogramInfo* items;
    int count;
} SubprogramCollection;

typedef struct {
    char* caller_name;
    char* callee_name;
} CallGraphEdge;

typedef struct {
    char** node_names;
    int node_count;
    CallGraphEdge* edges;
    int edge_count;
} CallGraph;

// CFG helpers
ControlFlowGraph* buildCFG(pANTLR3_BASE_TREE block_node);
SubprogramInfo* generateSubprogramInfo(const char* source_file, pANTLR3_BASE_TREE tree);
SubprogramCollection generateSubprogramInfoCollection(const char* source_file, pANTLR3_BASE_TREE tree);
void cfgToDot(ControlFlowGraph* cfg, FILE* out);
void cfgNodesToDot(ControlFlowGraph* cfg, FILE* out);
void freeCFG(ControlFlowGraph* cfg);

// Subprogram helpers
void freeSubprogramCollection(SubprogramCollection* collection);

// Call graph helpers
CallGraph* buildCallGraph(const SubprogramCollection* collection);
void callGraphToDot(const CallGraph* graph, FILE* out);
void freeCallGraph(CallGraph* graph);

#endif
