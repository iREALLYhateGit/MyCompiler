#include "cfg_builder_module.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void fprintEscaped(FILE *out, const char *s);
const char* nodeTypeToString(NodeType type);
const char* edgeTypeToString(EdgeType type);

void cfgToDot(ControlFlowGraph* cfg, FILE* out)
{
    fprintf(out, "digraph CFG {\n");
    fprintf(out, "  node [shape=box];\n\n");

    // ---- print nodes ----
    for (int i = 0; i < cfg->node_count; i++)
    {
        CFGNode* node = cfg->nodes[i];

        fprintf(out, "  n%d [label=\"%s\\n(id=%d)",
            node->id,
            nodeTypeToString(node->type),
            node->id);

        for (int s = 0; s < node->stmt_count; ++s)
        {
            pANTLR3_STRING ast = node->statements[s]->toStringTree(node->statements[s]);
            fprintf(out, "\\n[%d] ", s);
            fprintEscaped(out, (const char *)ast->chars);
            ast->factory->destroy(ast->factory, ast);
        }

        fprintf(out, "\"];\n");
    }

    fprintf(out, "\n");

    // ---- print edges with labels ----
    for (int i = 0; i < cfg->edge_count; i++)
    {
        CFGEdge* edge = cfg->edges[i];

        // Получаем метку для ребра
        const char* edge_label = edgeTypeToString(edge->type);

        // Печатаем ребро с меткой или без
        if (strcmp(edge_label, "") != 0) {
            fprintf(out, "  n%d -> n%d [label=\"%s\"];\n",
                    edge->from->id, edge->to->id, edge_label);
        } else {
            fprintf(out, "  n%d -> n%d;\n",
                    edge->from->id, edge->to->id);
        }
    }

    fprintf(out, "}\n");
}



// helper to keep DOT label valid
static void fprintEscaped(FILE *out, const char *s) {
    for (; *s; ++s) {
        if (*s == '\\') fputs("\\\\", out);
        else if (*s == '"') fputs("\\\"", out);
        else if (*s == '\n') fputs("\\n", out);
        else if (*s != '\r') fputc(*s, out);
    }
}



// Создание нового ребра
static CFGEdge* createCFGEdge(CFGNode* from, CFGNode* to, EdgeType type)
{
    CFGEdge* edge = malloc(sizeof(CFGEdge));
    edge->from = from;
    edge->to = to;
    edge->type = type;

    return edge;
}

// Простая версия для обычных ребер
static void addEdge(ControlFlowGraph* cfg, CFGEdge* edge)
{
    cfg->edges[cfg->edge_count++] = edge;
}



static FlowResult processIfStatement(pANTLR3_BASE_TREE if_node,
                            ControlFlowGraph* cfg,
                            FlowResult);

static FlowResult processWhileStatement(pANTLR3_BASE_TREE while_node,
                               ControlFlowGraph* cfg,
                               FlowResult);

static FlowResult processRepeatStatement(pANTLR3_BASE_TREE repeat_node,
                                ControlFlowGraph* cfg,
                                FlowResult);

static FlowResult processBreakStatement(pANTLR3_BASE_TREE break_node,
                               ControlFlowGraph* cfg,
                               FlowResult);


// Вспомогательная функция для получения текста узла
static char* get_ast_node_text(pANTLR3_BASE_TREE node)
{
    if (!node) return strdup("");
    pANTLR3_STRING text = node->getText(node);
    return strdup((char*)text->chars);
}


const char* nodeTypeToString(NodeType type)
{
    switch (type)
    {
        case NODE_BASIC_BLOCK:      return "BASIC_BLOCK";
        case NODE_ENTRY:            return "ENTRY_POINT";
        case NODE_EXIT:             return "EXIT_POINT";
        case NODE_IF:               return "IF";
        case NODE_WHILE:            return "WHILE";
        case NODE_REPEAT_CONDITION: return "REPEAT_CONDITION";
        case NODE_BREAK:            return "BREAK";
        case NODE_RETURN:           return "RETURN";
        default:                    return "UNKNOWN";
    }
}


const char* edgeTypeToString(EdgeType type)
{
    switch (type)
    {
        case EDGE_CLASSIC:   return "";
        case EDGE_TRUE:      return "true";
        case EDGE_FALSE:     return "false";
        case EDGE_BREAK:     return "break";
        case EDGE_CONTINUE:  return "continue";
        default:             return "";
    }
}


// Создание нового узла CFG
static CFGNode* createCFGNode(NodeType type)
{
    CFGNode* node = malloc(sizeof(CFGNode));
    static int next_id = 0;

    node->id = next_id++;
    node->type = type;
    node->statements = NULL;
    node->stmt_count = 0;

    return node;
}


static void flowAppend(FlowResult* result_flow, FlowResult donor)
{
    if (donor.exit_count == 0)
        return;

    result_flow->exits = realloc(
        result_flow->exits,
        sizeof(CFGNode*) * (result_flow->exit_count + donor.exit_count)
    );

    result_flow->exitsEdges = realloc(
        result_flow->exitsEdges,
        sizeof(CFGEdge*) * (result_flow->exit_count + donor.exit_count)
    );

    for (int i = 0; i < donor.exit_count; i++) {
        result_flow->exits[result_flow->exit_count + i] = donor.exits[i];
        result_flow->exitsEdges[result_flow->exit_count + i] = donor.exitsEdges[i];
    }

    result_flow->exit_count += donor.exit_count;
}


// Рекурсивная функция обхода AST и построения CFG
static FlowResult processStatement(pANTLR3_BASE_TREE node,
                             ControlFlowGraph* cfg,
                             FlowResult flow_result) {

    const char* node_text = get_ast_node_text(node);

    // Обработка узла IF.
    if (strcmp(node_text, "IF") == 0) {
        FlowResult new_flow_result = processIfStatement(node, cfg, flow_result);

        return new_flow_result;
    }
    // Обработка узла while
    else if (strcmp(node_text, "WHILE") == 0) {
        FlowResult new_flow_result = processWhileStatement(node, cfg, flow_result);

        return new_flow_result;
    }
    else if (strcmp(node_text, "REPEAT") == 0) {
        // Обработка цикла repeat
        FlowResult new_flow_result = processRepeatStatement(node, cfg, flow_result);

        return new_flow_result;
    }
    else if (strcmp(node_text, "BREAK") == 0) {
        // Обработка break
        FlowResult new_flow_result = processBreakStatement(node, cfg, flow_result);

        return new_flow_result;
    }
    else if (strcmp(node_text, "BLOCK") == 0) {
        // Обработка блока statements
        ANTLR3_UINT32 child_count = node->getChildCount(node);

        FlowResult new_flow_result = flow_result;

        for (ANTLR3_UINT32 i = 0; i < child_count; i++) {
            pANTLR3_BASE_TREE child = node->getChild(node, i);
            new_flow_result = processStatement(child, cfg, new_flow_result);
        }

        return new_flow_result;
    }
    else if (strcmp(node_text, "ASSIGN") == 0 || strcmp(node_text, "EXPRESSION") == 0) {
        CFGNode* current_block;
        bool continue_current_block = true;
        // Обработка блока statements
        if (flow_result.exit_count == 1 && flow_result.exits[0]->type == NODE_BASIC_BLOCK) {
            current_block = flow_result.exits[0];
        }
        else {
            continue_current_block = false;
            current_block = createCFGNode(NODE_BASIC_BLOCK);
            cfg->nodes[cfg->node_count++] = current_block;

            for (int i = 0; i < flow_result.exit_count; i++){
                flow_result.exitsEdges[i]->to = current_block;
                addEdge(cfg, flow_result.exitsEdges[i]);
            }
        }

        current_block->statements = realloc(current_block->statements,
                                           (current_block->stmt_count + 1) *
                                           sizeof(pANTLR3_BASE_TREE));
        current_block->statements[current_block->stmt_count++] = node;

        if (continue_current_block)
            return flow_result;
        else {
            FlowResult flow_exits;

            flow_exits.exits = malloc(sizeof(CFGNode*));
            flow_exits.exits[0] = current_block;
            flow_exits.exitsEdges = malloc(sizeof(CFGEdge*));
            flow_exits.exitsEdges[0] = createCFGEdge(current_block, NULL, EDGE_CLASSIC);
            flow_exits.exit_count = 1;

            return flow_exits;
        }
    }
    else if (strcmp(node_text, "THEN") == 0 || strcmp(node_text, "ELSE") == 0
        || strcmp(node_text, "DO") == 0 || strcmp(node_text, "REPEATABLE_PART") == 0) {

        FlowResult new_flow_result = flow_result;
        for (ANTLR3_UINT32 i = 0; i < node->getChildCount(node); i++) {
            pANTLR3_BASE_TREE child = node->getChild(node, i);
            new_flow_result = processStatement(child, cfg, new_flow_result);
        }

        return new_flow_result;
    }
}


// Обработка IF statement
static FlowResult processIfStatement(pANTLR3_BASE_TREE if_node,
                            ControlFlowGraph* cfg,
                            FlowResult flow_result) {

    CFGNode* if_block = createCFGNode(NODE_IF);
    // Добавляем узел в граф
    cfg->nodes[cfg->node_count++] = if_block;

    for (int i = 0; i < flow_result.exit_count; i++){
        flow_result.exitsEdges[i]->to = if_block;
        addEdge(cfg, flow_result.exitsEdges[i]);
    }

    // Находим condition, then и else части
    ANTLR3_UINT32 child_count = if_node->getChildCount(if_node);

    FlowResult end_of_then_block;
    FlowResult end_of_else_block;
    bool else_block_present = false;

    for (ANTLR3_UINT32 i = 0; i < child_count; i++) {
        pANTLR3_BASE_TREE child_node = if_node->getChild(if_node, i);

        if (strcmp(get_ast_node_text(child_node), "CONDITION") == 0) {
            pANTLR3_BASE_TREE condition_node = child_node;
            // Обработка блока statements
            if_block->statements = realloc(if_block->statements,
                                               (if_block->stmt_count + 1) *
                                               sizeof(pANTLR3_BASE_TREE));
            if_block->statements[if_block->stmt_count++] = condition_node;
        }

        else if (strcmp(get_ast_node_text(child_node), "THEN") == 0) {
            pANTLR3_BASE_TREE then_node = child_node;

            FlowResult if_block_flow;
            if_block_flow.exits = malloc(sizeof(CFGNode*));
            if_block_flow.exits[0] = if_block;
            if_block_flow.exitsEdges = malloc(sizeof(CFGEdge*));
            if_block_flow.exitsEdges[0] = createCFGEdge(if_block, NULL, EDGE_TRUE);
            if_block_flow.exit_count = 1;

            end_of_then_block = processStatement(then_node, cfg, if_block_flow);
        }
        else if (strcmp(get_ast_node_text(child_node), "ELSE") == 0) {
            pANTLR3_BASE_TREE else_node = child_node;

            else_block_present = true;

            FlowResult flow_before_else;
            flow_before_else.exits = malloc(sizeof(CFGNode*));
            flow_before_else.exits[0] = if_block;
            flow_before_else.exitsEdges = malloc(sizeof(CFGEdge*));
            flow_before_else.exitsEdges[0] = createCFGEdge(if_block, NULL, EDGE_FALSE);
            flow_before_else.exit_count = 1;

            end_of_else_block = processStatement(else_node, cfg, flow_before_else);
        }
    }

    FlowResult exit_flow_result;

    exit_flow_result.exit_count = 0;
    exit_flow_result.exits = malloc(0);
    exit_flow_result.exitsEdges = malloc(0);
    flowAppend(&exit_flow_result, end_of_then_block);

    if (else_block_present)
        flowAppend(&exit_flow_result, end_of_else_block);
    else {
        FlowResult if_block_flow;
        if_block_flow.exits = malloc(sizeof(CFGNode*));
        if_block_flow.exits[0] = if_block;
        if_block_flow.exitsEdges = malloc(sizeof(CFGEdge*));
        if_block_flow.exitsEdges[0] = createCFGEdge(if_block, NULL, EDGE_FALSE);
        if_block_flow.exit_count = 1;

        flowAppend(&exit_flow_result, if_block_flow);
    }

    return exit_flow_result;
}

// Обработка WHILE statement
static FlowResult processWhileStatement(pANTLR3_BASE_TREE while_node,
                               ControlFlowGraph* cfg,
                               FlowResult flow_entries) {

    // Создаем узлы для if
    CFGNode* while_block = createCFGNode(NODE_WHILE);
    // Добавляем узел в граф
    cfg->nodes[cfg->node_count++] = while_block;

    for (int i = 0; i < flow_entries.exit_count; i++){
        flow_entries.exitsEdges[i]->to = while_block;
        addEdge(cfg, flow_entries.exitsEdges[i]);
    }

    // Находим condition, then и else части
    ANTLR3_UINT32 child_count = while_node->getChildCount(while_node);

    FlowResult end_of_do_block;

    for (ANTLR3_UINT32 i = 0; i < child_count; i++) {
        pANTLR3_BASE_TREE child_node = while_node->getChild(while_node, i);

        if (strcmp(get_ast_node_text(child_node), "CONDITION") == 0) {
            pANTLR3_BASE_TREE condition_node = child_node;
            // Обработка блока statements
            while_block->statements = realloc(while_block->statements,
                                               (while_block->stmt_count + 1) *
                                               sizeof(pANTLR3_BASE_TREE));
            while_block->statements[while_block->stmt_count++] = condition_node;
        }

        else if (strcmp(get_ast_node_text(child_node), "DO") == 0) {
            pANTLR3_BASE_TREE do_node = child_node;
            // Обрабатываем then часть
            FlowResult while_block_flow;
            while_block_flow.exits = malloc(sizeof(CFGNode*));
            while_block_flow.exits[0] = while_block;
            while_block_flow.exitsEdges = malloc(sizeof(CFGEdge*));
            while_block_flow.exitsEdges[0] = createCFGEdge(while_block, NULL, EDGE_TRUE);
            while_block_flow.exit_count = 1;

            end_of_do_block = processStatement(do_node, cfg, while_block_flow);

            for (int k = 0; k < end_of_do_block.exit_count; k++) {
                end_of_do_block.exitsEdges[k]->to = while_block;
                addEdge(cfg, end_of_do_block.exitsEdges[k]);
            }
        }
    }

    FlowResult flow_exits;

    flow_exits.exits = malloc(sizeof(CFGNode*));
    flow_exits.exits[0] = while_block;
    flow_exits.exitsEdges = malloc(sizeof(CFGEdge*));
    flow_exits.exitsEdges[0] = createCFGEdge(while_block, NULL, EDGE_FALSE);
    flow_exits.exit_count = 1;

    return flow_exits;
}

// Обработка REPEAT statement
static FlowResult processRepeatStatement(pANTLR3_BASE_TREE repeat_node,
                                ControlFlowGraph* cfg,
                                FlowResult flow_entries) {

    // Создаем узлы для if
    CFGNode* repeatable_part_block = createCFGNode(NODE_BASIC_BLOCK);
    // Добавляем узел в граф
    cfg->nodes[cfg->node_count++] = repeatable_part_block;

    for (int i = 0; i < flow_entries.exit_count; i++){
        flow_entries.exitsEdges[i]->to = repeatable_part_block;
        addEdge(cfg, flow_entries.exitsEdges[i]);
    }

    // Находим condition, then и else части
    ANTLR3_UINT32 child_count = repeat_node->getChildCount(repeat_node);

    FlowResult end_of_repeatable_part_flow;

    for (ANTLR3_UINT32 i = 0; i < child_count; i++) {
        pANTLR3_BASE_TREE child_node = repeat_node->getChild(repeat_node, i);

        if (strcmp(get_ast_node_text(child_node), "REPEATABLE_PART") == 0) {
            pANTLR3_BASE_TREE repeatable_part_node = child_node;

            FlowResult flow_repeatable_part;

            flow_repeatable_part.exits = malloc(sizeof(CFGNode*));
            flow_repeatable_part.exits[0] = repeatable_part_block;
            flow_repeatable_part.exitsEdges = malloc(sizeof(CFGEdge*));
            flow_repeatable_part.exitsEdges[0] = createCFGEdge(repeatable_part_block, NULL, EDGE_CLASSIC);
            flow_repeatable_part.exit_count = 1;

            end_of_repeatable_part_flow = processStatement(repeatable_part_node, cfg, flow_repeatable_part);
        }
    }

    FlowResult flow_exits;

    for (ANTLR3_UINT32 i = 0; i < child_count; i++) {
        pANTLR3_BASE_TREE child_node = repeat_node->getChild(repeat_node, i);

        if (strcmp(get_ast_node_text(child_node), "UNTIL") == 0) {
            pANTLR3_BASE_TREE condition_node = child_node;
            // Обработка блока statements

            CFGNode* until_block = createCFGNode(NODE_REPEAT_CONDITION);
            cfg->nodes[cfg->node_count++] = until_block;

            until_block->statements = realloc(until_block->statements,
                                               (until_block->stmt_count + 1) *
                                               sizeof(pANTLR3_BASE_TREE));
            until_block->statements[until_block->stmt_count++] = condition_node;

            for (int k = 0; k < end_of_repeatable_part_flow.exit_count; k++){
                CFGEdge* edge = end_of_repeatable_part_flow.exitsEdges[k];
                end_of_repeatable_part_flow.exitsEdges[k]->to = until_block;
                addEdge(cfg, end_of_repeatable_part_flow.exitsEdges[k]);
            }

            CFGEdge* until_to_repeatable_part = createCFGEdge(until_block, repeatable_part_block, EDGE_TRUE);

            addEdge(cfg, until_to_repeatable_part);

            flow_exits.exits = malloc(sizeof(CFGNode*));
            flow_exits.exits[0] = until_block;
            flow_exits.exitsEdges = malloc(sizeof(CFGEdge*));
            flow_exits.exitsEdges[0] = createCFGEdge(until_block, NULL, EDGE_FALSE);
            flow_exits.exit_count = 1;
        }
    }

    return flow_exits;
}

// Обработка BREAK statement
static FlowResult processBreakStatement(pANTLR3_BASE_TREE break_node,
                               ControlFlowGraph* cfg,
                               FlowResult flow_entries)
{
    for (int i = 0; i < flow_entries.exit_count; i++)
    {
        flow_entries.exitsEdges[i]->type = EDGE_BREAK;
    }

    return flow_entries;

    // CFGNode* break_block = createCFGNode(NODE_BREAK);
    // cfg->nodes[cfg->node_count++] = break_block;
    //
    // for (int i = 0; i < flow_entries.exit_count; i++){
    //     flow_entries.exitsEdges[i]->to = break_block;
    //     addEdge(cfg, flow_entries.exitsEdges[i]);
    // }
    //
    // FlowResult flow_exits;
    //
    // flow_exits.exits = malloc(sizeof(CFGNode*));
    // flow_exits.exits[0] = break_block;
    // flow_exits.exitsEdges = malloc(sizeof(CFGEdge*));
    // flow_exits.exitsEdges[0] = createCFGEdge(break_block, NULL, EDGE_BREAK, NULL);
    // flow_exits.exit_count = 1;
    //
    // return flow_exits;
}

static pANTLR3_BASE_TREE skipUselessTokens(pANTLR3_BASE_TREE tree)
{
    if (tree == NULL) {
        return NULL;
    }

    if (strcmp(get_ast_node_text(tree), "BLOCK") == 0) {
        return tree;
    }

    ANTLR3_UINT32 count = tree->getChildCount(tree);
    for (ANTLR3_UINT32 i = 0; i < count; i++) {
        pANTLR3_BASE_TREE result = skipUselessTokens(tree->getChild(tree, i));

        if (result != NULL) {
            return result;
        }
    }

    return NULL;
}




ControlFlowGraph* buildCFG(pANTLR3_BASE_TREE tree)
{
    if (!tree) return NULL;

    ControlFlowGraph* cfg = malloc(sizeof(ControlFlowGraph));

    cfg->max_nodes = 100;
    cfg->nodes = malloc(cfg->max_nodes * sizeof(CFGNode*));
    cfg->node_count = 0;

    cfg->max_edges = 100;
    cfg->edges = malloc(cfg->max_edges * sizeof(CFGEdge*));
    cfg->edge_count = 0;

    // Создаем entry узел
    cfg->entry = createCFGNode(NODE_ENTRY);
    cfg->nodes[cfg->node_count++] = cfg->entry;


    CFGEdge* entryEdge = createCFGEdge(cfg->entry, NULL, EDGE_CLASSIC);

    FlowResult entry_block_flow;
    entry_block_flow.exits = malloc(sizeof(CFGNode*));
    entry_block_flow.exits[0] = cfg->entry;
    entry_block_flow.exitsEdges = malloc(sizeof(CFGEdge*));
    entry_block_flow.exitsEdges[0] = entryEdge;
    entry_block_flow.exit_count = 1;

    // Рекурсивно строим CFG
    pANTLR3_BASE_TREE blockNode = skipUselessTokens(tree);

    FlowResult result_flow;
    if (blockNode != NULL) {
        result_flow = processStatement(blockNode, cfg, entry_block_flow);
    }

    cfg->exit = createCFGNode(NODE_EXIT);
    cfg->nodes[cfg->node_count++] = cfg->exit;

    if (blockNode != NULL) {
        for (int i = 0; i < result_flow.exit_count; i++) {
            result_flow.exitsEdges[i]->to = cfg->exit;
            addEdge(cfg, result_flow.exitsEdges[i]);
        }
    }
    else {
        entryEdge->to = cfg->exit;
        addEdge(cfg, entryEdge);
    }

    return cfg;
}