#include "cfg_builder_module.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void cfgToDot(ControlFlowGraph* cfg, FILE* out)
{
    if (!cfg || !out)
        return;

    fprintf(out, "digraph CFG {\n");
    fprintf(out, "  node [shape=box];\n\n");

    // ---- print nodes ----
    for (int i = 0; i < cfg->node_count; i++) {
        CFGNode* node = cfg->nodes[i];

        fprintf(out,
            "  n%d [label=\"%s\\n(id=%d)\"];\n",
            node->id,
            node->label,
            node->id
        );
    }

    fprintf(out, "\n");

    // ---- print edges ----
    for (int i = 0; i < cfg->node_count; i++) {
        CFGNode* node = cfg->nodes[i];

        for (int j = 0; j < node->succ_count; j++) {
            CFGNode* succ = node->successors[j];

            fprintf(out,
                "  n%d -> n%d;\n",
                node->id,
                succ->id
            );
        }
    }

    fprintf(out, "}\n");
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
static char* getNodeText(pANTLR3_BASE_TREE node) {
    if (!node) return strdup("");
    pANTLR3_STRING text = node->getText(node);
    return strdup((char*)text->chars);
}


const char* nodeTypeToString(NodeType type)
{
    switch (type)
    {
        case NODE_BASIC_BLOCK:      return "BASIC_BLOCK";
        case NODE_ENTRY:            return "ENTRY";
        case NODE_EXIT:             return "EXIT";
        case NODE_IF:               return "IF";
        case NODE_WHILE:            return "WHILE";
        case NODE_REPEAT_CONDITION: return "REPEAT_CONDITION";
        case NODE_BREAK:            return "BREAK";
        case NODE_RETURN:           return "RETURN";
        default:                    return "UNKNOWN";
    }
}



// Создание нового узла CFG
static CFGNode* createCFGNode(NodeType type) {
    CFGNode* node = malloc(sizeof(CFGNode));
    static int next_id = 0;

    node->id = next_id++;
    node->type = type;
    node->label = nodeTypeToString(type);
    node->successors = NULL;
    node->succ_count = 0;
    node->predecessors = NULL;
    node->pred_count = 0;
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

    for (int i = 0; i < donor.exit_count; i++) {
        result_flow->exits[result_flow->exit_count + i] = donor.exits[i];
    }

    result_flow->exit_count += donor.exit_count;
}


// Добавление связи между узлами
static void addEdge(CFGNode* from, CFGNode* to) {
    // Добавляем to в successors of from
    from->successors = realloc(from->successors,
                              (from->succ_count + 1) * sizeof(CFGNode*));
    from->successors[from->succ_count++] = to;

    // Добавляем from в predecessors of to
    to->predecessors = realloc(to->predecessors,
                              (to->pred_count + 1) * sizeof(CFGNode*));
    to->predecessors[to->pred_count++] = from;
}


// Рекурсивная функция обхода AST и построения CFG
static FlowResult processStatement(pANTLR3_BASE_TREE node,
                             ControlFlowGraph* cfg,
                             FlowResult flow_result) {

    const char* node_text = getNodeText(node);

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
                addEdge(flow_result.exits[i], current_block);
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
        addEdge(flow_result.exits[i], if_block);
    }

    // Находим condition, then и else части
    ANTLR3_UINT32 child_count = if_node->getChildCount(if_node);

    FlowResult end_of_then_block;
    FlowResult end_of_else_block;
    bool else_block_present = false;

    for (ANTLR3_UINT32 i = 0; i < child_count; i++) {
        pANTLR3_BASE_TREE child_node = if_node->getChild(if_node, i);

        if (strcmp(getNodeText(child_node), "CONDITION") == 0) {
            pANTLR3_BASE_TREE condition_node = child_node;
            // Обработка блока statements
            if_block->statements = realloc(if_block->statements,
                                               (if_block->stmt_count + 1) *
                                               sizeof(pANTLR3_BASE_TREE));
            if_block->statements[if_block->stmt_count++] = condition_node;
        }

        else if (strcmp(getNodeText(child_node), "THEN") == 0) {
            pANTLR3_BASE_TREE then_node = child_node;

            FlowResult if_block_flow;
            if_block_flow.exits = malloc(sizeof(CFGNode*));
            if_block_flow.exits[0] = if_block;
            if_block_flow.exit_count = 1;

            end_of_then_block = processStatement(then_node, cfg, if_block_flow);
        }
        else if (strcmp(getNodeText(child_node), "ELSE") == 0) {
            pANTLR3_BASE_TREE else_node = child_node;

            else_block_present = true;

            FlowResult flow_before_else;
            flow_before_else.exits = malloc(sizeof(CFGNode*));
            flow_before_else.exits[0] = if_block;
            flow_before_else.exit_count = 1;

            end_of_else_block = processStatement(else_node, cfg, flow_before_else);
        }
    }

    FlowResult exit_flow_result;

    exit_flow_result.exit_count = 0;
    exit_flow_result.exits = malloc(0);
    flowAppend(&exit_flow_result, end_of_then_block);

    if (else_block_present)
        flowAppend(&exit_flow_result, end_of_else_block);
    else {
        FlowResult if_block_flow;
        if_block_flow.exits = malloc(sizeof(CFGNode*));
        if_block_flow.exits[0] = if_block;
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
        addEdge(flow_entries.exits[i], while_block);
    }

    // Находим condition, then и else части
    ANTLR3_UINT32 child_count = while_node->getChildCount(while_node);

    FlowResult end_of_do_block;

    for (ANTLR3_UINT32 i = 0; i < child_count; i++) {
        pANTLR3_BASE_TREE child_node = while_node->getChild(while_node, i);

        if (strcmp(getNodeText(child_node), "CONDITION") == 0) {
            pANTLR3_BASE_TREE condition_node = child_node;
            // Обработка блока statements
            while_block->statements = realloc(while_block->statements,
                                               (while_block->stmt_count + 1) *
                                               sizeof(pANTLR3_BASE_TREE));
            while_block->statements[while_block->stmt_count++] = condition_node;
        }

        else if (strcmp(getNodeText(child_node), "DO") == 0) {
            pANTLR3_BASE_TREE do_node = child_node;
            // Обрабатываем then часть
            FlowResult while_block_flow;
            while_block_flow.exits = malloc(sizeof(CFGNode*));
            while_block_flow.exits[0] = while_block;
            while_block_flow.exit_count = 1;

            end_of_do_block = processStatement(do_node, cfg, while_block_flow);

            for (int k = 0; k < end_of_do_block.exit_count; k++) {
                addEdge(end_of_do_block.exits[k], while_block);
            }
        }
    }

    FlowResult flow_exits;

    flow_exits.exits = malloc(sizeof(CFGNode*));
    flow_exits.exits[0] = while_block;
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
        addEdge(flow_entries.exits[i], repeatable_part_block);
    }

    // Находим condition, then и else части
    ANTLR3_UINT32 child_count = repeat_node->getChildCount(repeat_node);

    FlowResult end_of_repeatable_part_flow;

    for (ANTLR3_UINT32 i = 0; i < child_count; i++) {
        pANTLR3_BASE_TREE child_node = repeat_node->getChild(repeat_node, i);

        if (strcmp(getNodeText(child_node), "REPEATABLE_PART") == 0) {
            pANTLR3_BASE_TREE repeatable_part_node = child_node;

            FlowResult flow_repeatable_part;

            flow_repeatable_part.exits = malloc(sizeof(CFGNode*));
            flow_repeatable_part.exits[0] = repeatable_part_block;
            flow_repeatable_part.exit_count = 1;

            end_of_repeatable_part_flow = processStatement(repeatable_part_node, cfg, flow_repeatable_part);
        }
    }

    FlowResult flow_exits;

    for (ANTLR3_UINT32 i = 0; i < child_count; i++) {
        pANTLR3_BASE_TREE child_node = repeat_node->getChild(repeat_node, i);

        if (strcmp(getNodeText(child_node), "UNTIL") == 0) {
            pANTLR3_BASE_TREE condition_node = child_node;
            // Обработка блока statements

            CFGNode* until_block = createCFGNode(NODE_REPEAT_CONDITION);
            cfg->nodes[cfg->node_count++] = until_block;

            until_block->statements = realloc(until_block->statements,
                                               (until_block->stmt_count + 1) *
                                               sizeof(pANTLR3_BASE_TREE));
            until_block->statements[until_block->stmt_count++] = condition_node;

            for (int k = 0; k < end_of_repeatable_part_flow.exit_count; k++){
                addEdge(end_of_repeatable_part_flow.exits[k], until_block);
            }

            addEdge(until_block, repeatable_part_block);

            flow_exits.exits = malloc(sizeof(CFGNode*));
            flow_exits.exits[0] = until_block;
            flow_exits.exit_count = 1;
        }
    }

    return flow_exits;
}

// Обработка BREAK statement
static FlowResult processBreakStatement(pANTLR3_BASE_TREE break_node,
                               ControlFlowGraph* cfg,
                               FlowResult flow_entries) {

    CFGNode* break_block = createCFGNode(NODE_BREAK);
    cfg->nodes[cfg->node_count++] = break_block;

    for (int k = 0; k < flow_entries.exit_count; k++){
        addEdge(flow_entries.exits[k], break_block);
    }

    FlowResult flow_exits;

    flow_exits.exits = malloc(sizeof(CFGNode*));
    flow_exits.exits[0] = break_block;
    flow_exits.exit_count = 1;

    return flow_exits;
}

static pANTLR3_BASE_TREE skipUselessTokens(pANTLR3_BASE_TREE tree)
{
    if (tree == NULL) {
        return NULL;
    }

    if (strcmp(getNodeText(tree), "BLOCK") == 0) {
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




ControlFlowGraph* buildCFG(pANTLR3_BASE_TREE tree) {
    if (!tree) return NULL;

    ControlFlowGraph* cfg = malloc(sizeof(ControlFlowGraph));
    cfg->max_nodes = 100;
    cfg->nodes = malloc(cfg->max_nodes * sizeof(CFGNode*));
    cfg->node_count = 0;

    // Создаем entry узел
    cfg->entry = createCFGNode(NODE_ENTRY);
    cfg->entry->label = "ENTRYPOINT";
    cfg->nodes[cfg->node_count++] = cfg->entry;

    FlowResult entry_block_flow;

    entry_block_flow.exits = malloc(sizeof(CFGNode*));
    entry_block_flow.exits[0] = cfg->entry;
    entry_block_flow.exit_count = 1;

    // Рекурсивно строим CFG
    pANTLR3_BASE_TREE blockNode = skipUselessTokens(tree);

    FlowResult result;
    if (blockNode != NULL) {
        result = processStatement(blockNode, cfg, entry_block_flow);
    }

    cfg->exit = createCFGNode(NODE_EXIT);
    cfg->exit->label = "EXIT";
    cfg->nodes[cfg->node_count++] = cfg->exit;

    if (blockNode != NULL) {
        for (int i = 0; i < result.exit_count; i++) {
            // Связываем последний блок с exit
            addEdge(result.exits[i], cfg->exit);
        }
    }
    else {
        // Связываем последний блок с exit
        addEdge(cfg->entry, cfg->exit);
    }

    return cfg;
}