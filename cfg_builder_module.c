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
            char* op_text = opTreeToString(node->statements[s]);
            fprintf(out, "\\n[%d] ", s);
            fprintEscaped(out, op_text);
            free(op_text);
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




void cfgNodesToDot(ControlFlowGraph* cfg, FILE* out)
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
            char* op_text = opTreeToString(node->statements[s]);
            fprintf(out, "\\n[%d] ", s);
            fprintEscaped(out, op_text);
            free(op_text);
        }

        fprintf(out, "\"];\n");
    }

    fprintf(out, "\n");

    // ---- print edges based on nextDefault / nextConditional ----
    for (int i = 0; i < cfg->node_count; i++)
    {
        CFGNode* node = cfg->nodes[i];

        if (node->nextDefault)
        {
            fprintf(out, "  n%d -> n%d [label=\"nextDefault\"];\n",
                    node->id, node->nextDefault->id);
        }

        if (node->nextConditional)
        {
            // Avoid duplicating the same arrow if both pointers coincide.
            if (node->nextConditional == node->nextDefault)
            {
                fprintf(out, "  n%d -> n%d [label=\"nextDefault/nextConditional\"];\n",
                        node->id, node->nextConditional->id);
            }
            else
            {
                fprintf(out, "  n%d -> n%d [label=\"nextConditional\" style=dashed];\n",
                        node->id, node->nextConditional->id);
            }
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

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} StringBuilder;

static void sbInit(StringBuilder* sb)
{
    sb->cap = 32;
    sb->len = 0;
    sb->data = malloc(sb->cap);
    sb->data[0] = '\0';
}

static void sbEnsure(StringBuilder* sb, size_t extra)
{
    size_t needed = sb->len + extra + 1;
    if (needed <= sb->cap) {
        return;
    }

    size_t new_cap = sb->cap * 2;
    while (new_cap < needed) {
        new_cap *= 2;
    }

    sb->data = realloc(sb->data, new_cap);
    sb->cap = new_cap;
}

static void sbAppend(StringBuilder* sb, const char* text)
{
    if (!text || text[0] == '\0') {
        return;
    }

    size_t add = strlen(text);
    sbEnsure(sb, add);
    memcpy(sb->data + sb->len, text, add);
    sb->len += add;
    sb->data[sb->len] = '\0';
}

static void sbAppendChar(StringBuilder* sb, char c)
{
    sbEnsure(sb, 1);
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
}

static void flattenTreeTextRec(StringBuilder* sb, pANTLR3_BASE_TREE node)
{
    if (!node) {
        return;
    }

    const char* text = get_ast_node_text(node);
    if (text[0] != '\0') {
        if (sb->len > 0) {
            sbAppendChar(sb, ' ');
        }
        sbAppend(sb, text);
    }

    ANTLR3_UINT32 count = node->getChildCount(node);
    for (ANTLR3_UINT32 i = 0; i < count; i++) {
        flattenTreeTextRec(sb, node->getChild(node, i));
    }
}

static char* flattenTreeText(pANTLR3_BASE_TREE node)
{
    StringBuilder sb;
    sbInit(&sb);
    flattenTreeTextRec(&sb, node);
    return sb.data;
}

static pANTLR3_BASE_TREE findChildByText(pANTLR3_BASE_TREE node, const char* text)
{
    ANTLR3_UINT32 count = node->getChildCount(node);

    for (ANTLR3_UINT32 i = 0; i < count; i++)
    {
        pANTLR3_BASE_TREE child = node->getChild(node, i);
        if (strcmp(get_ast_node_text(child), text) == 0)
        {
            return child;
        }
    }

    return NULL;
}

static char* extractTypeText(pANTLR3_BASE_TREE node)
{
    if (!node) {
        return NULL;
    }

    if (strcmp(get_ast_node_text(node), "VOID_VALUE") == 0) {
        return strdup("void");
    }

    return flattenTreeText(node);
}

static char* extractIdText(pANTLR3_BASE_TREE id_node)
{
    if (id_node->getChildCount(id_node) > 0) {
        return strdup(get_ast_node_text(id_node->getChild(id_node, 0)));
    }

    return strdup(get_ast_node_text(id_node));
}

static void initSubprogramInfo(SubprogramInfo* info)
{
    info->name = NULL;
    info->param_names = NULL;
    info->param_types = NULL;
    info->param_count = 0;
    info->return_type = NULL;
    info->local_names = NULL;
    info->local_types = NULL;
    info->local_count = 0;
    info->source_file = NULL;
    info->cfg = NULL;
}

static void fillSubprogramInfo(SubprogramInfo* info, pANTLR3_BASE_TREE method_node)
{
    pANTLR3_BASE_TREE name_node = findChildByText(method_node, "ID");
    if (name_node)
        info->name = extractIdText(name_node);

    pANTLR3_BASE_TREE params_node = findChildByText(method_node, "PARAMETERS");
    if (params_node)
    {
        int count = (int)params_node->getChildCount(params_node);
        if (count > 0)
        {
            info->param_count = count;
            info->param_names = calloc(count, sizeof(char*));
            info->param_types = calloc(count, sizeof(char*));

            for (int i = 0; i < count; i++)
            {
                pANTLR3_BASE_TREE param_node = params_node->getChild(params_node, i);
                pANTLR3_BASE_TREE param_id = findChildByText(param_node, "ID");
                info->param_names[i] = extractIdText(param_id);

                pANTLR3_BASE_TREE type_node = findChildByText(param_node, "TYPE");
                if (type_node && type_node->getChildCount(type_node) > 0)
                {
                    info->param_types[i] = extractTypeText(type_node->getChild(type_node, 0));
                }
            }
        }
    }

    pANTLR3_BASE_TREE return_node = findChildByText(method_node, "RETURN_TYPE");
    if (return_node && return_node->getChildCount(return_node) > 0) {
        info->return_type = extractTypeText(return_node->getChild(return_node, 0));
    }

    pANTLR3_BASE_TREE body_node = findChildByText(method_node, "BODY");
    if (body_node)
    {
        pANTLR3_BASE_TREE var_declarations_node = findChildByText(body_node, "VAR_DECLARATIONS");
        if (var_declarations_node && var_declarations_node->getChildCount(var_declarations_node) > 0)
        {
            ANTLR3_UINT32 var_declaration_count = var_declarations_node->getChildCount(var_declarations_node);

            for (int i = 0; i < var_declaration_count; i++)
            {
                pANTLR3_BASE_TREE var_decl_node = var_declarations_node->getChild(var_declarations_node, i);
                pANTLR3_BASE_TREE type_node = findChildByText(var_decl_node, "TYPE");
                char* decl_type = NULL;
                if (type_node && type_node->getChildCount(type_node) > 0)
                {
                    decl_type = extractTypeText(type_node->getChild(type_node, 0));
                }

                pANTLR3_BASE_TREE vars_node = findChildByText(var_decl_node, "VARIABLES");
                if (vars_node)
                {
                    ANTLR3_UINT32 vars_count = vars_node->getChildCount(vars_node);
                    for (ANTLR3_UINT32 j = 0; j < vars_count; j++)
                    {
                        pANTLR3_BASE_TREE var_id = vars_node->getChild(vars_node, j);
                        char* var_name = extractIdText(var_id);

                        info->local_names = realloc(info->local_names, sizeof(char*) * (info->local_count + 1));
                        info->local_types = realloc(info->local_types, sizeof(char*) * (info->local_count + 1));
                        info->local_names[info->local_count] = var_name;
                        info->local_types[info->local_count] = decl_type ? strdup(decl_type) : NULL;
                        info->local_count++;
                    }
                }

                if (decl_type)
                {
                    free(decl_type);
                }
            }
        }

        pANTLR3_BASE_TREE block_node = findChildByText(body_node, "BLOCK");
        if (block_node && block_node->getChildCount(block_node) > 0)
        {
            info->cfg = buildCFG(block_node);
        }
    }

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
    node->nextDefault = NULL;
    node->nextConditional = NULL;

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
                             FlowResult flow_result)
{
    const char* node_text = get_ast_node_text(node);

    // Обработка узла IF.
    if (strcmp(node_text, "IF") == 0)
    {
        FlowResult new_flow_result = processIfStatement(node, cfg, flow_result);

        return new_flow_result;
    }
    // Обработка узла while
    else if (strcmp(node_text, "WHILE") == 0)
    {
        FlowResult new_flow_result = processWhileStatement(node, cfg, flow_result);

        return new_flow_result;
    }
    else if (strcmp(node_text, "REPEAT") == 0)
    {
        // Обработка цикла repeat
        FlowResult new_flow_result = processRepeatStatement(node, cfg, flow_result);

        return new_flow_result;
    }
    else if (strcmp(node_text, "BREAK") == 0)
    {
        // Обработка break
        FlowResult new_flow_result = processBreakStatement(node, cfg, flow_result);

        return new_flow_result;
    }
    else if (strcmp(node_text, "BLOCK") == 0)
    {
        // Обработка блока statements
        ANTLR3_UINT32 child_count = node->getChildCount(node);

        FlowResult new_flow_result = flow_result;

        for (ANTLR3_UINT32 i = 0; i < child_count; i++)
        {
            pANTLR3_BASE_TREE child = node->getChild(node, i);
            new_flow_result = processStatement(child, cfg, new_flow_result);
        }

        return new_flow_result;
    }
    else if (strcmp(node_text, "ASSIGN") == 0 || strcmp(node_text, "EXPRESSION") == 0)
    {
        CFGNode* current_block;
        bool continue_current_block = true;
        // Обработка блока statements
        if (flow_result.exit_count == 1 && flow_result.exits[0]->type == NODE_BASIC_BLOCK)
        {
            current_block = flow_result.exits[0];
        }
        else
        {
            continue_current_block = false;
            current_block = createCFGNode(NODE_BASIC_BLOCK);
            cfg->nodes[cfg->node_count++] = current_block;

            for (int i = 0; i < flow_result.exit_count; i++)
            {
                CFGNode* exit_node = flow_result.exits[i];
                CFGEdge* exit_edge = flow_result.exitsEdges[i];

                exit_edge->to = current_block;
                addEdge(cfg, exit_edge);

                if (exit_edge->type == EDGE_TRUE)
                    exit_node->nextConditional = current_block;

                else
                    exit_node->nextDefault = current_block;
            }
        }

        current_block->statements = realloc(current_block->statements,
                                           (current_block->stmt_count + 1) *
                                           sizeof(OpNode*));
        current_block->statements[current_block->stmt_count++] = buildOpTree(node);

        if (continue_current_block)
            return flow_result;
        else
            {
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
        for (ANTLR3_UINT32 i = 0; i < node->getChildCount(node); i++)
        {
            pANTLR3_BASE_TREE child = node->getChild(node, i);
            new_flow_result = processStatement(child, cfg, new_flow_result);
        }

        return new_flow_result;
    }
}


// Обработка IF statement
static FlowResult processIfStatement(pANTLR3_BASE_TREE if_node,
                            ControlFlowGraph* cfg,
                            FlowResult flow_entries)
{
    CFGNode* if_block = createCFGNode(NODE_IF);
    // Добавляем узел в граф
    cfg->nodes[cfg->node_count++] = if_block;

    for (int i = 0; i < flow_entries.exit_count; i++)
    {
        CFGNode* entry_node = flow_entries.exits[i];
        CFGEdge* entry_edge = flow_entries.exitsEdges[i];

        entry_edge->to = if_block;
        addEdge(cfg, entry_edge);

        if (entry_edge->type == EDGE_TRUE)
            entry_node->nextConditional = if_block;

        else
            entry_node->nextDefault = if_block;
    }

    // Находим condition, then и else части
    ANTLR3_UINT32 child_count = if_node->getChildCount(if_node);

    FlowResult end_of_then_block;
    FlowResult end_of_else_block;
    bool else_block_present = false;

    for (ANTLR3_UINT32 i = 0; i < child_count; i++)
    {
        pANTLR3_BASE_TREE child_node = if_node->getChild(if_node, i);

        if (strcmp(get_ast_node_text(child_node), "CONDITION") == 0)
        {
            pANTLR3_BASE_TREE condition_node = child_node;
            // Обработка блока statements
            if_block->statements = realloc(if_block->statements,
                                               (if_block->stmt_count + 1) *
                                               sizeof(OpNode*));
            if_block->statements[if_block->stmt_count++] = buildOpTree(condition_node);
        }

        else if (strcmp(get_ast_node_text(child_node), "THEN") == 0)
        {
            pANTLR3_BASE_TREE then_node = child_node;

            FlowResult if_block_flow;
            if_block_flow.exits = malloc(sizeof(CFGNode*));
            if_block_flow.exits[0] = if_block;
            if_block_flow.exitsEdges = malloc(sizeof(CFGEdge*));
            if_block_flow.exitsEdges[0] = createCFGEdge(if_block, NULL, EDGE_TRUE);
            if_block_flow.exit_count = 1;

            end_of_then_block = processStatement(then_node, cfg, if_block_flow);
        }
        else if (strcmp(get_ast_node_text(child_node), "ELSE") == 0)
        {
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
    else
    {
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

    for (int i = 0; i < flow_entries.exit_count; i++)
    {
        CFGNode* exit_node = flow_entries.exits[i];
        CFGEdge* exit_edge = flow_entries.exitsEdges[i];

        exit_edge->to = while_block;
        addEdge(cfg, exit_edge);

        if (exit_edge->type == EDGE_TRUE)
            exit_node->nextConditional = while_block;

        else
            exit_node->nextDefault = while_block;
    }

    // Находим condition, then и else части
    ANTLR3_UINT32 child_count = while_node->getChildCount(while_node);

    FlowResult end_of_do_block;

    for (ANTLR3_UINT32 i = 0; i < child_count; i++)
    {
        pANTLR3_BASE_TREE child_node = while_node->getChild(while_node, i);

        if (strcmp(get_ast_node_text(child_node), "CONDITION") == 0)
        {
            pANTLR3_BASE_TREE condition_node = child_node;
            // Обработка блока statements
            while_block->statements = realloc(while_block->statements,
                                               (while_block->stmt_count + 1) *
                                               sizeof(OpNode*));
            while_block->statements[while_block->stmt_count++] = buildOpTree(condition_node);
        }

        else if (strcmp(get_ast_node_text(child_node), "DO") == 0)
        {
            pANTLR3_BASE_TREE do_node = child_node;
            // Обрабатываем then часть
            FlowResult while_block_flow;
            while_block_flow.exits = malloc(sizeof(CFGNode*));
            while_block_flow.exits[0] = while_block;
            while_block_flow.exitsEdges = malloc(sizeof(CFGEdge*));
            while_block_flow.exitsEdges[0] = createCFGEdge(while_block, NULL, EDGE_TRUE);
            while_block_flow.exit_count = 1;

            end_of_do_block = processStatement(do_node, cfg, while_block_flow);

            for (int k = 0; k < end_of_do_block.exit_count; k++)
            {
                CFGNode* exit_node = end_of_do_block.exits[k];
                CFGEdge* exit_edge = end_of_do_block.exitsEdges[k];

                exit_edge->to = while_block;
                addEdge(cfg, exit_edge);

                if (exit_edge->type == EDGE_TRUE)
                    exit_node->nextConditional = while_block;

                else
                    exit_node->nextDefault = while_block;
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

    for (int i = 0; i < flow_entries.exit_count; i++)
    {
        CFGNode* exit_node = flow_entries.exits[i];
        CFGEdge* exit_edge = flow_entries.exitsEdges[i];

        exit_edge->to = repeatable_part_block;
        addEdge(cfg, exit_edge);

        if (exit_edge->type == EDGE_TRUE)
            exit_node->nextConditional = repeatable_part_block;

        else
            exit_node->nextDefault = repeatable_part_block;
    }

    // Находим condition, then и else части
    ANTLR3_UINT32 child_count = repeat_node->getChildCount(repeat_node);

    FlowResult end_of_repeatable_part_flow;

    for (ANTLR3_UINT32 i = 0; i < child_count; i++)
    {
        pANTLR3_BASE_TREE child_node = repeat_node->getChild(repeat_node, i);

        if (strcmp(get_ast_node_text(child_node), "REPEATABLE_PART") == 0)
        {
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

    for (ANTLR3_UINT32 i = 0; i < child_count; i++)
    {
        pANTLR3_BASE_TREE child_node = repeat_node->getChild(repeat_node, i);

        if (strcmp(get_ast_node_text(child_node), "UNTIL") == 0)
        {
            pANTLR3_BASE_TREE condition_node = child_node;
            // Обработка блока statements

            CFGNode* until_block = createCFGNode(NODE_REPEAT_CONDITION);
            cfg->nodes[cfg->node_count++] = until_block;

            until_block->statements = realloc(until_block->statements,
                                               (until_block->stmt_count + 1) *
                                               sizeof(OpNode*));
            until_block->statements[until_block->stmt_count++] = buildOpTree(condition_node);

            for (int k = 0; k < end_of_repeatable_part_flow.exit_count; k++)
            {
                CFGNode* exit_node = end_of_repeatable_part_flow.exits[k];
                CFGEdge* exit_edge = end_of_repeatable_part_flow.exitsEdges[k];

                exit_edge->to = until_block;
                addEdge(cfg, exit_edge);

                if (exit_edge->type == EDGE_TRUE)
                    exit_node->nextConditional = until_block;
                else
                    exit_node->nextDefault = until_block;
            }

            CFGEdge* until_to_repeatable_part = createCFGEdge(until_block, repeatable_part_block, EDGE_TRUE);

            addEdge(cfg, until_to_repeatable_part);

            if (until_to_repeatable_part->type == EDGE_TRUE)
                until_block->nextConditional = repeatable_part_block;
            else
                until_block->nextDefault = repeatable_part_block;

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
    if (strcmp(get_ast_node_text(tree), "BLOCK") == 0)
    {
        return tree;
    }

    ANTLR3_UINT32 count = tree->getChildCount(tree);
    for (ANTLR3_UINT32 i = 0; i < count; i++)
    {
        pANTLR3_BASE_TREE result = skipUselessTokens(tree->getChild(tree, i));

        if (result != NULL)
        {
            return result;
        }
    }

    return NULL;
}

static pANTLR3_BASE_TREE findMethodDeclaration(pANTLR3_BASE_TREE tree)
{
    if (strcmp(get_ast_node_text(tree), "METHOD_DECL") == 0)
    {
        return tree;
    }

    for (ANTLR3_UINT32 i = 0; i < tree->getChildCount(tree); i++)
    {
        return  findMethodDeclaration(tree->getChild(tree, i));
    }

    return NULL;
}




ControlFlowGraph* buildCFG(pANTLR3_BASE_TREE blockNode)
{
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
    FlowResult result_flow = processStatement(blockNode, cfg, entry_block_flow);

    cfg->exit = createCFGNode(NODE_EXIT);
    cfg->nodes[cfg->node_count++] = cfg->exit;

    for (int i = 0; i < result_flow.exit_count; i++)
    {
        CFGNode* exit_node = result_flow.exits[i];
        CFGEdge* exit_edge = result_flow.exitsEdges[i];

        exit_edge->to = cfg->exit;
        addEdge(cfg, exit_edge);

        if (exit_edge->type == EDGE_TRUE)
            exit_node->nextConditional = cfg->exit;
        else
            exit_node->nextDefault = cfg->exit;
    }
    // else
    // {
    //     entryEdge->to = cfg->exit;
    //     addEdge(cfg, entryEdge);
    //
    //     cfg->entry->nextDefault = cfg->exit;
    // }

    return cfg;
}

SubprogramInfo* generateSubprogramInfo(const char* source_file, pANTLR3_BASE_TREE tree)
{
    SubprogramInfo* info = malloc(sizeof(SubprogramInfo));
    initSubprogramInfo(info);

    info->source_file = strdup(source_file);

    pANTLR3_BASE_TREE methodNode = findMethodDeclaration(tree);

    if (methodNode)
    {
        fillSubprogramInfo(info, methodNode);
    }

    else {
        return NULL;
    }

    return info;
}