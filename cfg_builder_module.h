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

typedef enum {
    MEMBER_VISIBILITY_DEFAULT,
    MEMBER_VISIBILITY_PUBLIC,
    MEMBER_VISIBILITY_PRIVATE
} MemberVisibility;

typedef enum {
    USER_TYPE_CLASS,
    USER_TYPE_INTERFACE
} UserTypeKind;

typedef struct {
    char* name;
    char* type_name;
    char* declaring_type_name;
    int offset_bytes;
} FieldInfo;

typedef struct {
    char* name;
    char** param_types;
    int param_count;
    char* return_type;
} MethodSignatureInfo;

typedef struct {
    bool is_imported;
    char* dll_name;
    char* entry_name;
} ImportInfo;

typedef struct {
    char* name;
    char* owner_type_name;
    char* asm_name;
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
    bool is_method;
    MemberVisibility visibility;
    ImportInfo import_info;
} SubprogramInfo;

typedef struct {
    UserTypeKind kind;
    char* name;
    char* base_type_name;
    char** interface_names;
    int interface_count;
    FieldInfo* declared_fields;
    int declared_field_count;
    FieldInfo* resolved_fields;
    int resolved_field_count;
    MethodSignatureInfo* declared_methods;
    int declared_method_count;
    int total_size_bytes;
} UserTypeInfo;

typedef struct {
    SubprogramInfo* items;
    int count;
    UserTypeInfo* user_types;
    int user_type_count;
    char** errors;
    int error_count;
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
const UserTypeInfo* findUserTypeInfo(const SubprogramCollection* collection, const char* name);
const FieldInfo* findResolvedFieldInfo(const UserTypeInfo* type_info, const char* field_name);
int getTypeSizeBytes(const SubprogramCollection* collection, const char* type_name);

// Call graph helpers
CallGraph* buildCallGraph(const SubprogramCollection* collection);
void callGraphToDot(const CallGraph* graph, FILE* out);
void freeCallGraph(CallGraph* graph);

#endif
