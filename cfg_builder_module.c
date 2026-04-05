#include "cfg_builder_module.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

static void fprintEscaped(FILE *out, const char *s);
const char* nodeTypeToString(NodeType type);
const char* edgeTypeToString(EdgeType type);
static ControlFlowGraph* buildEmptyCFG(void);

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
    if (cfg->edge_count + 1 > cfg->max_edges) {
        int new_capacity = cfg->max_edges == 0 ? 16 : cfg->max_edges * 2;
        CFGEdge** new_edges = realloc(cfg->edges, sizeof(CFGEdge*) * new_capacity);
        if (!new_edges) {
            return;
        }
        cfg->edges = new_edges;
        cfg->max_edges = new_capacity;
    }

    cfg->edges[cfg->edge_count++] = edge;
}

static void addNode(ControlFlowGraph* cfg, CFGNode* node)
{
    if (!cfg || !node) {
        return;
    }

    if (cfg->node_count + 1 > cfg->max_nodes) {
        int new_capacity = cfg->max_nodes == 0 ? 16 : cfg->max_nodes * 2;
        CFGNode** new_nodes = realloc(cfg->nodes, sizeof(CFGNode*) * new_capacity);
        if (!new_nodes) {
            return;
        }
        cfg->nodes = new_nodes;
        cfg->max_nodes = new_capacity;
    }

    cfg->nodes[cfg->node_count++] = node;
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
static const char* get_ast_node_text(pANTLR3_BASE_TREE node)
{
    if (!node) {
        return "";
    }

    pANTLR3_STRING text = node->getText(node);
    if (!text || !text->chars) {
        return "";
    }

    return (const char*)text->chars;
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

static char* duplicateSanitizedText(const char* text)
{
    if (!text) {
        return strdup("");
    }

    size_t len = strlen(text);
    if (len >= 2 && text[0] == '"' && text[len - 1] == '"') {
        char* copy = malloc(len - 1);
        if (!copy) {
            return strdup("");
        }
        memcpy(copy, text + 1, len - 2);
        copy[len - 2] = '\0';
        return copy;
    }

    return strdup(text);
}

static void appendOwnedString(char*** items, int* count, const char* value)
{
    if (!items || !count) {
        return;
    }

    char** new_items = realloc(*items, sizeof(char*) * (*count + 1));
    if (!new_items) {
        return;
    }

    *items = new_items;
    (*items)[*count] = value ? strdup(value) : NULL;
    (*count)++;
}

static void appendCollectionError(SubprogramCollection* collection, const char* format, ...)
{
    if (!collection || !format) {
        return;
    }

    va_list args;
    va_start(args, format);
    int needed = vsnprintf(NULL, 0, format, args);
    va_end(args);
    if (needed < 0) {
        return;
    }

    char* message = malloc((size_t)needed + 1);
    if (!message) {
        return;
    }

    va_start(args, format);
    vsnprintf(message, (size_t)needed + 1, format, args);
    va_end(args);

    char** new_errors = realloc(collection->errors, sizeof(char*) * (collection->error_count + 1));
    if (!new_errors) {
        free(message);
        return;
    }

    collection->errors = new_errors;
    collection->errors[collection->error_count++] = message;
}

static void appendField(FieldInfo** fields,
                        int* count,
                        const char* name,
                        const char* type_name,
                        const char* declaring_type_name,
                        int offset_bytes)
{
    if (!fields || !count) {
        return;
    }

    FieldInfo* new_fields = realloc(*fields, sizeof(FieldInfo) * (*count + 1));
    if (!new_fields) {
        return;
    }

    *fields = new_fields;
    FieldInfo* field = &(*fields)[*count];
    field->name = name ? strdup(name) : NULL;
    field->type_name = type_name ? strdup(type_name) : NULL;
    field->declaring_type_name = declaring_type_name ? strdup(declaring_type_name) : NULL;
    field->offset_bytes = offset_bytes;
    (*count)++;
}

static void initMethodSignature(MethodSignatureInfo* signature)
{
    if (!signature) {
        return;
    }

    signature->name = NULL;
    signature->param_types = NULL;
    signature->param_count = 0;
    signature->return_type = NULL;
}

static void freeMethodSignature(MethodSignatureInfo* signature)
{
    if (!signature) {
        return;
    }

    free(signature->name);
    for (int i = 0; i < signature->param_count; i++) {
        free(signature->param_types ? signature->param_types[i] : NULL);
    }
    free(signature->param_types);
    free(signature->return_type);
    initMethodSignature(signature);
}

static void appendMethodSignature(MethodSignatureInfo** methods,
                                  int* count,
                                  const char* name,
                                  char** param_types,
                                  int param_count,
                                  const char* return_type)
{
    if (!methods || !count) {
        return;
    }

    MethodSignatureInfo* new_methods = realloc(*methods, sizeof(MethodSignatureInfo) * (*count + 1));
    if (!new_methods) {
        return;
    }

    *methods = new_methods;
    MethodSignatureInfo* signature = &(*methods)[*count];
    initMethodSignature(signature);
    signature->name = name ? strdup(name) : NULL;
    signature->param_count = param_count;
    if (param_count > 0) {
        signature->param_types = calloc(param_count, sizeof(char*));
        if (signature->param_types) {
            for (int i = 0; i < param_count; i++) {
                signature->param_types[i] = param_types && param_types[i] ? strdup(param_types[i]) : NULL;
            }
        }
    }
    signature->return_type = return_type ? strdup(return_type) : strdup("void");
    (*count)++;
}

static void appendVarDeclarationsToArrays(pANTLR3_BASE_TREE var_declarations_node,
                                          char*** names,
                                          char*** types,
                                          int* count,
                                          const char* declaring_type_name,
                                          FieldInfo** fields,
                                          int* field_count)
{
    if (!var_declarations_node) {
        return;
    }

    ANTLR3_UINT32 var_declaration_count = var_declarations_node->getChildCount(var_declarations_node);
    for (ANTLR3_UINT32 i = 0; i < var_declaration_count; i++) {
        pANTLR3_BASE_TREE var_decl_node = var_declarations_node->getChild(var_declarations_node, i);
        pANTLR3_BASE_TREE type_node = findChildByText(var_decl_node, "TYPE");
        char* decl_type = NULL;
        if (type_node && type_node->getChildCount(type_node) > 0) {
            decl_type = extractTypeText(type_node->getChild(type_node, 0));
        }

        pANTLR3_BASE_TREE vars_node = findChildByText(var_decl_node, "VARIABLES");
        if (vars_node) {
            ANTLR3_UINT32 vars_count = vars_node->getChildCount(vars_node);
            for (ANTLR3_UINT32 j = 0; j < vars_count; j++) {
                pANTLR3_BASE_TREE var_id = vars_node->getChild(vars_node, j);
                char* var_name = extractIdText(var_id);

                if (names && types && count) {
                    char** new_names = realloc(*names, sizeof(char*) * (*count + 1));
                    char** new_types = realloc(*types, sizeof(char*) * (*count + 1));
                    if (new_names && new_types) {
                        *names = new_names;
                        *types = new_types;
                        (*names)[*count] = var_name;
                        (*types)[*count] = decl_type ? strdup(decl_type) : NULL;
                        (*count)++;
                        var_name = NULL;
                    }
                }

                if (fields && field_count) {
                    appendField(fields, field_count, var_name, decl_type, declaring_type_name, 0);
                }

                free(var_name);
            }
        }

        free(decl_type);
    }
}

static void initSubprogramInfo(SubprogramInfo* info)
{
    if (!info) {
        return;
    }

    info->name = NULL;
    info->owner_type_name = NULL;
    info->asm_name = NULL;
    info->param_names = NULL;
    info->param_types = NULL;
    info->param_count = 0;
    info->return_type = NULL;
    info->local_names = NULL;
    info->local_types = NULL;
    info->local_count = 0;
    info->source_file = NULL;
    info->cfg = NULL;
    info->has_body = false;
    info->is_method = false;
    info->visibility = MEMBER_VISIBILITY_DEFAULT;
    info->import_info.is_imported = false;
    info->import_info.dll_name = NULL;
    info->import_info.entry_name = NULL;
}

static void fillSubprogramInfo(SubprogramInfo* info,
                               pANTLR3_BASE_TREE method_node,
                               const char* source_file,
                               const char* owner_type_name,
                               MemberVisibility visibility)
{
    if (!info || !method_node) {
        return;
    }

    initSubprogramInfo(info);
    info->source_file = source_file ? strdup(source_file) : NULL;
    info->owner_type_name = owner_type_name ? strdup(owner_type_name) : NULL;
    info->is_method = owner_type_name != NULL;
    info->visibility = visibility;

    pANTLR3_BASE_TREE name_node = findChildByText(method_node, "ID");
    if (name_node) {
        info->name = extractIdText(name_node);
    }

    pANTLR3_BASE_TREE params_node = findChildByText(method_node, "PARAMETERS");
    if (params_node) {
        int count_local = (int)params_node->getChildCount(params_node);
        if (count_local > 0) {
            info->param_count = count_local;
            info->param_names = calloc(count_local, sizeof(char*));
            info->param_types = calloc(count_local, sizeof(char*));

            for (int i = 0; i < count_local; i++) {
                pANTLR3_BASE_TREE param_node = params_node->getChild(params_node, i);
                pANTLR3_BASE_TREE param_id = findChildByText(param_node, "ID");
                info->param_names[i] = extractIdText(param_id);

                pANTLR3_BASE_TREE type_node = findChildByText(param_node, "TYPE");
                if (type_node && type_node->getChildCount(type_node) > 0) {
                    info->param_types[i] = extractTypeText(type_node->getChild(type_node, 0));
                }
            }
        }
    }

    pANTLR3_BASE_TREE return_node = findChildByText(method_node, "RETURN_TYPE");
    if (return_node && return_node->getChildCount(return_node) > 0) {
        info->return_type = extractTypeText(return_node->getChild(return_node, 0));
    } else {
        info->return_type = strdup("void");
    }

    pANTLR3_BASE_TREE import_node = findChildByText(method_node, "IMPORT_SPEC");
    if (import_node) {
        info->import_info.is_imported = true;
        pANTLR3_BASE_TREE dll_name_node = findChildByText(import_node, "DLL_NAME");
        if (dll_name_node && dll_name_node->getChildCount(dll_name_node) > 0) {
            info->import_info.dll_name = duplicateSanitizedText(
                get_ast_node_text(dll_name_node->getChild(dll_name_node, 0))
            );
        }

        pANTLR3_BASE_TREE entry_node = findChildByText(import_node, "DLL_ENTRY");
        if (entry_node && entry_node->getChildCount(entry_node) > 0) {
            info->import_info.entry_name = duplicateSanitizedText(
                get_ast_node_text(entry_node->getChild(entry_node, 0))
            );
        }
    }

    pANTLR3_BASE_TREE body_node = findChildByText(method_node, "BODY");
    if (body_node) {
        info->has_body = true;
        pANTLR3_BASE_TREE var_declarations_node = findChildByText(body_node, "VAR_DECLARATIONS");
        appendVarDeclarationsToArrays(var_declarations_node,
                                      &info->local_names,
                                      &info->local_types,
                                      &info->local_count,
                                      NULL,
                                      NULL,
                                      NULL);

        pANTLR3_BASE_TREE block_node = findChildByText(body_node, "BLOCK");
        info->cfg = block_node ? buildCFG(block_node) : buildEmptyCFG();
    } else {
        info->cfg = buildEmptyCFG();
    }
}

static MemberVisibility parseVisibilityFromMember(pANTLR3_BASE_TREE member_node)
{
    if (!member_node) {
        return MEMBER_VISIBILITY_DEFAULT;
    }

    for (ANTLR3_UINT32 i = 0; i < member_node->getChildCount(member_node); i++) {
        const char* text = get_ast_node_text(member_node->getChild(member_node, i));
        if (strcmp(text, "PUBLIC") == 0) {
            return MEMBER_VISIBILITY_PUBLIC;
        }
        if (strcmp(text, "PRIVATE") == 0) {
            return MEMBER_VISIBILITY_PRIVATE;
        }
    }

    return MEMBER_VISIBILITY_DEFAULT;
}

static pANTLR3_BASE_TREE findMethodChildFromMember(pANTLR3_BASE_TREE member_node)
{
    if (!member_node) {
        return NULL;
    }

    for (ANTLR3_UINT32 i = 0; i < member_node->getChildCount(member_node); i++) {
        pANTLR3_BASE_TREE child = member_node->getChild(member_node, i);
        if (strcmp(get_ast_node_text(child), "METHOD_DECL") == 0) {
            return child;
        }
    }

    return NULL;
}

static void appendSubprogram(SubprogramCollection* collection, const SubprogramInfo* source)
{
    if (!collection || !source) {
        return;
    }

    SubprogramInfo* new_items = realloc(collection->items, sizeof(SubprogramInfo) * (collection->count + 1));
    if (!new_items) {
        return;
    }

    collection->items = new_items;
    collection->items[collection->count] = *source;
    collection->count++;
}

static void appendUserType(SubprogramCollection* collection, const UserTypeInfo* source)
{
    if (!collection || !source) {
        return;
    }

    UserTypeInfo* new_items = realloc(collection->user_types, sizeof(UserTypeInfo) * (collection->user_type_count + 1));
    if (!new_items) {
        return;
    }

    collection->user_types = new_items;
    collection->user_types[collection->user_type_count] = *source;
    collection->user_type_count++;
}

static void initUserTypeInfo(UserTypeInfo* type_info)
{
    if (!type_info) {
        return;
    }

    type_info->kind = USER_TYPE_CLASS;
    type_info->name = NULL;
    type_info->base_type_name = NULL;
    type_info->interface_names = NULL;
    type_info->interface_count = 0;
    type_info->declared_fields = NULL;
    type_info->declared_field_count = 0;
    type_info->resolved_fields = NULL;
    type_info->resolved_field_count = 0;
    type_info->declared_methods = NULL;
    type_info->declared_method_count = 0;
    type_info->total_size_bytes = 0;
}

static void collectInterfaceMethodSignature(UserTypeInfo* type_info, pANTLR3_BASE_TREE method_node)
{
    if (!type_info || !method_node) {
        return;
    }

    SubprogramInfo temp;
    fillSubprogramInfo(&temp, method_node, NULL, type_info->name, MEMBER_VISIBILITY_PUBLIC);
    appendMethodSignature(&type_info->declared_methods,
                          &type_info->declared_method_count,
                          temp.name,
                          temp.param_types,
                          temp.param_count,
                          temp.return_type);

    free(temp.name);
    free(temp.owner_type_name);
    free(temp.asm_name);
    for (int i = 0; i < temp.param_count; i++) {
        free(temp.param_names ? temp.param_names[i] : NULL);
        free(temp.param_types ? temp.param_types[i] : NULL);
    }
    free(temp.param_names);
    free(temp.param_types);
    free(temp.return_type);
    free(temp.source_file);
    free(temp.import_info.dll_name);
    free(temp.import_info.entry_name);
    freeCFG(temp.cfg);
}

static char* sanitizeIdentifierPart(const char* text)
{
    if (!text) {
        return strdup("");
    }

    char* result = strdup(text);
    if (!result) {
        return strdup("");
    }

    for (char* p = result; *p; ++p) {
        if (!isalnum((unsigned char)*p) && *p != '_') {
            *p = '_';
        }
    }

    return result;
}

static char* buildMangledMethodName(const char* owner_type_name, const char* method_name, char** param_types, int param_count)
{
    StringBuilder sb;
    sbInit(&sb);
    sbAppend(&sb, owner_type_name ? owner_type_name : "");
    if (owner_type_name && owner_type_name[0] != '\0') {
        sbAppendChar(&sb, '_');
    }
    sbAppend(&sb, method_name ? method_name : "method");
    for (int i = 0; i < param_count; i++) {
        char* clean = sanitizeIdentifierPart(param_types && param_types[i] ? param_types[i] : "void");
        sbAppendChar(&sb, '_');
        sbAppend(&sb, clean);
        free(clean);
    }
    return sb.data;
}

static bool sameMethodShape(const MethodSignatureInfo* left, const MethodSignatureInfo* right)
{
    if (!left || !right || !left->name || !right->name) {
        return false;
    }

    if (strcmp(left->name, right->name) != 0 || left->param_count != right->param_count) {
        return false;
    }

    for (int i = 0; i < left->param_count; i++) {
        const char* left_type = left->param_types ? left->param_types[i] : NULL;
        const char* right_type = right->param_types ? right->param_types[i] : NULL;
        if ((!left_type && right_type) || (left_type && !right_type)) {
            return false;
        }
        if (left_type && right_type && strcmp(left_type, right_type) != 0) {
            return false;
        }
    }

    return true;
}

static bool sameMethodContract(const MethodSignatureInfo* left, const MethodSignatureInfo* right)
{
    if (!sameMethodShape(left, right)) {
        return false;
    }

    return strcmp(left->return_type ? left->return_type : "void",
                  right->return_type ? right->return_type : "void") == 0;
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
            addNode(cfg, current_block);

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
    addNode(cfg, if_block);

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
    addNode(cfg, while_block);

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
    addNode(cfg, repeatable_part_block);

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
            addNode(cfg, until_block);

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


static void collectMethodDeclarations(pANTLR3_BASE_TREE tree,
                                      pANTLR3_BASE_TREE** methods,
                                      int* method_count,
                                      int* method_capacity)
{
    if (!tree || !methods || !method_count || !method_capacity) {
        return;
    }

    if (strcmp(get_ast_node_text(tree), "METHOD_DECL") == 0) {
        if (*method_count + 1 > *method_capacity) {
            int new_capacity = *method_capacity == 0 ? 8 : (*method_capacity * 2);
            pANTLR3_BASE_TREE* new_items = realloc(*methods, sizeof(pANTLR3_BASE_TREE) * new_capacity);
            if (!new_items) {
                return;
            }

            *methods = new_items;
            *method_capacity = new_capacity;
        }

        (*methods)[(*method_count)++] = tree;
    }

    ANTLR3_UINT32 count = tree->getChildCount(tree);
    for (ANTLR3_UINT32 i = 0; i < count; i++) {
        collectMethodDeclarations(tree->getChild(tree, i), methods, method_count, method_capacity);
    }
}

static ControlFlowGraph* buildEmptyCFG(void)
{
    ControlFlowGraph* cfg = malloc(sizeof(ControlFlowGraph));
    if (!cfg) {
        return NULL;
    }

    cfg->max_nodes = 4;
    cfg->nodes = malloc(cfg->max_nodes * sizeof(CFGNode*));
    cfg->node_count = 0;

    cfg->max_edges = 4;
    cfg->edges = malloc(cfg->max_edges * sizeof(CFGEdge*));
    cfg->edge_count = 0;

    cfg->entry = createCFGNode(NODE_ENTRY);
    cfg->exit = createCFGNode(NODE_EXIT);

    addNode(cfg, cfg->entry);
    addNode(cfg, cfg->exit);

    CFGEdge* edge = createCFGEdge(cfg->entry, cfg->exit, EDGE_CLASSIC);
    addEdge(cfg, edge);
    cfg->entry->nextDefault = cfg->exit;

    return cfg;
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
    addNode(cfg, cfg->entry);


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
    addNode(cfg, cfg->exit);

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

    free(result_flow.exits);
    free(result_flow.exitsEdges);

    return cfg;
}

const UserTypeInfo* findUserTypeInfo(const SubprogramCollection* collection, const char* name)
{
    if (!collection || !name) {
        return NULL;
    }

    for (int i = 0; i < collection->user_type_count; i++) {
        if (collection->user_types[i].name && strcmp(collection->user_types[i].name, name) == 0) {
            return &collection->user_types[i];
        }
    }

    return NULL;
}

static UserTypeInfo* findMutableUserTypeInfo(SubprogramCollection* collection, const char* name)
{
    return (UserTypeInfo*)findUserTypeInfo(collection, name);
}

static bool isBuiltinTypeName(const char* type_name)
{
    if (!type_name) {
        return false;
    }

    return strcmp(type_name, "bool") == 0
        || strcmp(type_name, "byte") == 0
        || strcmp(type_name, "int") == 0
        || strcmp(type_name, "uint") == 0
        || strcmp(type_name, "long") == 0
        || strcmp(type_name, "ulong") == 0
        || strcmp(type_name, "char") == 0
        || strcmp(type_name, "string") == 0;
}

static int getBuiltinTypeSizeBytes(const char* type_name)
{
    if (!type_name) {
        return 4;
    }
    if (strcmp(type_name, "bool") == 0 || strcmp(type_name, "byte") == 0 || strcmp(type_name, "char") == 0) {
        return 1;
    }
    if (strcmp(type_name, "long") == 0 || strcmp(type_name, "ulong") == 0) {
        return 8;
    }
    return 4;
}

static const FieldInfo* findFieldInArray(const FieldInfo* fields, int count, const char* field_name)
{
    if (!fields || !field_name) {
        return NULL;
    }

    for (int i = 0; i < count; i++) {
        if (fields[i].name && strcmp(fields[i].name, field_name) == 0) {
            return &fields[i];
        }
    }

    return NULL;
}

const FieldInfo* findResolvedFieldInfo(const UserTypeInfo* type_info, const char* field_name)
{
    if (!type_info) {
        return NULL;
    }

    return findFieldInArray(type_info->resolved_fields, type_info->resolved_field_count, field_name);
}

static int resolveTypeSizeBytes(SubprogramCollection* collection, const char* type_name, char** visiting, int visiting_count);

static int ensureResolvedUserTypeLayout(SubprogramCollection* collection,
                                        UserTypeInfo* type_info,
                                        char** visiting,
                                        int visiting_count)
{
    if (!collection || !type_info) {
        return 0;
    }

    if (type_info->kind == USER_TYPE_INTERFACE) {
        type_info->total_size_bytes = 0;
        return 0;
    }

    if (type_info->resolved_fields) {
        return type_info->total_size_bytes;
    }

    for (int i = 0; i < visiting_count; i++) {
        if (strcmp(visiting[i], type_info->name) == 0) {
            appendCollectionError(collection, "Recursive user type layout is not supported for '%s'.", type_info->name);
            return 0;
        }
    }

    char** next_visiting = malloc(sizeof(char*) * (visiting_count + 1));
    if (!next_visiting) {
        return 0;
    }
    for (int i = 0; i < visiting_count; i++) {
        next_visiting[i] = visiting[i];
    }
    next_visiting[visiting_count] = type_info->name;

    int offset = 0;
    if (type_info->base_type_name) {
        UserTypeInfo* base_type = findMutableUserTypeInfo(collection, type_info->base_type_name);
        if (!base_type) {
            appendCollectionError(collection, "Unknown base type '%s' for class '%s'.",
                                  type_info->base_type_name, type_info->name);
        } else if (base_type->kind != USER_TYPE_CLASS) {
            appendCollectionError(collection, "Base type '%s' for class '%s' must be a class.",
                                  type_info->base_type_name, type_info->name);
        } else {
            offset = ensureResolvedUserTypeLayout(collection, base_type, next_visiting, visiting_count + 1);
            for (int i = 0; i < base_type->resolved_field_count; i++) {
                appendField(&type_info->resolved_fields,
                            &type_info->resolved_field_count,
                            base_type->resolved_fields[i].name,
                            base_type->resolved_fields[i].type_name,
                            base_type->resolved_fields[i].declaring_type_name,
                            base_type->resolved_fields[i].offset_bytes);
            }
        }
    }

    for (int i = 0; i < type_info->declared_field_count; i++) {
        FieldInfo* field = &type_info->declared_fields[i];
        if (findResolvedFieldInfo(type_info, field->name)) {
            appendCollectionError(collection,
                                  "Field '%s' is declared more than once in class '%s' or its base chain.",
                                  field->name,
                                  type_info->name);
            continue;
        }

        int field_size = resolveTypeSizeBytes(collection, field->type_name, next_visiting, visiting_count + 1);
        appendField(&type_info->resolved_fields,
                    &type_info->resolved_field_count,
                    field->name,
                    field->type_name,
                    type_info->name,
                    offset);
        offset += field_size;
    }

    free(next_visiting);
    type_info->total_size_bytes = offset;
    return offset;
}

static int resolveTypeSizeBytes(SubprogramCollection* collection, const char* type_name, char** visiting, int visiting_count)
{
    if (isBuiltinTypeName(type_name)) {
        return getBuiltinTypeSizeBytes(type_name);
    }

    UserTypeInfo* type_info = findMutableUserTypeInfo(collection, type_name);
    if (!type_info) {
        appendCollectionError(collection, "Unknown type '%s'.", type_name ? type_name : "<null>");
        return 4;
    }

    if (type_info->kind != USER_TYPE_CLASS) {
        appendCollectionError(collection, "Type '%s' cannot be used as a value type because it is not a class.", type_name);
        return 4;
    }

    return ensureResolvedUserTypeLayout(collection, type_info, visiting, visiting_count);
}

int getTypeSizeBytes(const SubprogramCollection* collection, const char* type_name)
{
    if (isBuiltinTypeName(type_name)) {
        return getBuiltinTypeSizeBytes(type_name);
    }

    return resolveTypeSizeBytes((SubprogramCollection*)collection, type_name, NULL, 0);
}

static void collectProgramItems(SubprogramCollection* collection,
                                const char* source_file,
                                pANTLR3_BASE_TREE tree)
{
    if (!collection || !tree || strcmp(get_ast_node_text(tree), "SOURCE") != 0) {
        return;
    }

    for (ANTLR3_UINT32 i = 0; i < tree->getChildCount(tree); i++) {
        pANTLR3_BASE_TREE child = tree->getChild(tree, i);
        const char* child_text = get_ast_node_text(child);

        if (strcmp(child_text, "METHOD_DECL") == 0) {
            SubprogramInfo info;
            fillSubprogramInfo(&info, child, source_file, NULL, MEMBER_VISIBILITY_DEFAULT);
            appendSubprogram(collection, &info);
            continue;
        }

        if (strcmp(child_text, "INTERFACE_DECL") == 0) {
            UserTypeInfo type_info;
            initUserTypeInfo(&type_info);
            type_info.kind = USER_TYPE_INTERFACE;

            pANTLR3_BASE_TREE name_node = findChildByText(child, "ID");
            type_info.name = name_node ? extractIdText(name_node) : NULL;

            for (ANTLR3_UINT32 j = 0; j < child->getChildCount(child); j++) {
                pANTLR3_BASE_TREE member_node = child->getChild(child, j);
                if (strcmp(get_ast_node_text(member_node), "METHOD_DECL") == 0) {
                    collectInterfaceMethodSignature(&type_info, member_node);
                }
            }

            appendUserType(collection, &type_info);
            continue;
        }

        if (strcmp(child_text, "CLASS_DECL") == 0) {
            UserTypeInfo type_info;
            initUserTypeInfo(&type_info);
            type_info.kind = USER_TYPE_CLASS;

            pANTLR3_BASE_TREE name_node = findChildByText(child, "ID");
            type_info.name = name_node ? extractIdText(name_node) : NULL;

            pANTLR3_BASE_TREE base_node = findChildByText(child, "BASE_TYPE");
            if (base_node && base_node->getChildCount(base_node) > 0) {
                type_info.base_type_name = extractIdText(base_node->getChild(base_node, 0));
            }

            pANTLR3_BASE_TREE implements_node = findChildByText(child, "IMPLEMENTS");
            if (implements_node) {
                for (ANTLR3_UINT32 j = 0; j < implements_node->getChildCount(implements_node); j++) {
                    char* interface_name = extractIdText(implements_node->getChild(implements_node, j));
                    appendOwnedString(&type_info.interface_names, &type_info.interface_count, interface_name);
                    free(interface_name);
                }
            }

            pANTLR3_BASE_TREE var_node = findChildByText(child, "VAR_DECLARATIONS");
            appendVarDeclarationsToArrays(var_node,
                                          NULL,
                                          NULL,
                                          NULL,
                                          type_info.name,
                                          &type_info.declared_fields,
                                          &type_info.declared_field_count);

            for (ANTLR3_UINT32 j = 0; j < child->getChildCount(child); j++) {
                pANTLR3_BASE_TREE member_node = child->getChild(child, j);
                if (strcmp(get_ast_node_text(member_node), "MEMBER") != 0) {
                    continue;
                }

                pANTLR3_BASE_TREE method_node = findMethodChildFromMember(member_node);
                if (!method_node) {
                    continue;
                }

                SubprogramInfo info;
                fillSubprogramInfo(&info,
                                   method_node,
                                   source_file,
                                   type_info.name,
                                   parseVisibilityFromMember(member_node));
                appendSubprogram(collection, &info);
                appendMethodSignature(&type_info.declared_methods,
                                      &type_info.declared_method_count,
                                      info.name,
                                      info.param_types,
                                      info.param_count,
                                      info.return_type);
            }

            appendUserType(collection, &type_info);
        }
    }
}

static char* buildMangledLabel(const char* owner_type_name, const char* method_name, char** param_types, int param_count)
{
    return buildMangledMethodName(owner_type_name, method_name, param_types, param_count);
}

static void assignAsmNames(SubprogramCollection* collection)
{
    if (!collection) {
        return;
    }

    for (int i = 0; i < collection->count; i++) {
        SubprogramInfo* info = &collection->items[i];
        free(info->asm_name);
        if (info->owner_type_name) {
            info->asm_name = buildMangledLabel(info->owner_type_name,
                                               info->name,
                                               info->param_types,
                                               info->param_count);
        } else {
            info->asm_name = info->name ? strdup(info->name) : strdup("main");
        }
    }
}

static void buildMethodSignatureFromSubprogram(MethodSignatureInfo* signature, const SubprogramInfo* info)
{
    if (!signature || !info) {
        return;
    }

    initMethodSignature(signature);
    signature->name = info->name ? strdup(info->name) : NULL;
    signature->param_count = info->param_count;
    if (info->param_count > 0) {
        signature->param_types = calloc(info->param_count, sizeof(char*));
        if (signature->param_types) {
            for (int i = 0; i < info->param_count; i++) {
                signature->param_types[i] = info->param_types[i] ? strdup(info->param_types[i]) : NULL;
            }
        }
    }
    signature->return_type = info->return_type ? strdup(info->return_type) : strdup("void");
}

static const SubprogramInfo* findMethodInTypeHierarchy(const SubprogramCollection* collection,
                                                       const UserTypeInfo* type_info,
                                                       const MethodSignatureInfo* required)
{
    if (!collection || !type_info || !required) {
        return NULL;
    }

    for (int i = 0; i < collection->count; i++) {
        const SubprogramInfo* info = &collection->items[i];
        if (!info->owner_type_name || strcmp(info->owner_type_name, type_info->name) != 0) {
            continue;
        }

        MethodSignatureInfo candidate;
        buildMethodSignatureFromSubprogram(&candidate, info);
        bool matches = sameMethodContract(&candidate, required);
        freeMethodSignature(&candidate);
        if (matches) {
            return info;
        }
    }

    if (type_info->base_type_name) {
        const UserTypeInfo* base_type = findUserTypeInfo(collection, type_info->base_type_name);
        if (base_type) {
            return findMethodInTypeHierarchy(collection, base_type, required);
        }
    }

    return NULL;
}

static void validateProgramItems(SubprogramCollection* collection)
{
    if (!collection) {
        return;
    }

    for (int i = 0; i < collection->user_type_count; i++) {
        UserTypeInfo* type_info = &collection->user_types[i];
        if (type_info->kind != USER_TYPE_CLASS) {
            continue;
        }

        for (int j = 0; j < type_info->interface_count; j++) {
            const UserTypeInfo* interface_type = findUserTypeInfo(collection, type_info->interface_names[j]);
            if (!interface_type) {
                appendCollectionError(collection, "Unknown interface '%s' for class '%s'.",
                                      type_info->interface_names[j], type_info->name);
            } else if (interface_type->kind != USER_TYPE_INTERFACE) {
                appendCollectionError(collection, "Type '%s' referenced by class '%s' must be an interface.",
                                      type_info->interface_names[j], type_info->name);
            }
        }

        ensureResolvedUserTypeLayout(collection, type_info, NULL, 0);
    }

    for (int i = 0; i < collection->count; i++) {
        SubprogramInfo* info = &collection->items[i];

        if (!info->owner_type_name) {
            for (int j = i + 1; j < collection->count; j++) {
                SubprogramInfo* other = &collection->items[j];
                if (!other->owner_type_name && info->name && other->name && strcmp(info->name, other->name) == 0) {
                    appendCollectionError(collection, "Global method '%s' is declared more than once.", info->name);
                }
            }
            continue;
        }

        MethodSignatureInfo current;
        buildMethodSignatureFromSubprogram(&current, info);

        for (int j = i + 1; j < collection->count; j++) {
            SubprogramInfo* other = &collection->items[j];
            if (!other->owner_type_name || strcmp(info->owner_type_name, other->owner_type_name) != 0) {
                continue;
            }

            MethodSignatureInfo other_sig;
            buildMethodSignatureFromSubprogram(&other_sig, other);
            if (sameMethodShape(&current, &other_sig)) {
                appendCollectionError(collection,
                                      "Method '%s' in class '%s' is declared multiple times with the same signature.",
                                      info->name,
                                      info->owner_type_name);
            }
            freeMethodSignature(&other_sig);
        }

        const UserTypeInfo* owner_type = findUserTypeInfo(collection, info->owner_type_name);
        if (owner_type && owner_type->base_type_name) {
            const UserTypeInfo* base_type = findUserTypeInfo(collection, owner_type->base_type_name);
            if (base_type) {
                const SubprogramInfo* base_method = findMethodInTypeHierarchy(collection, base_type, &current);
                if (base_method) {
                    appendCollectionError(collection,
                                          "Method override is forbidden: '%s' in class '%s' matches a base method signature.",
                                          info->name,
                                          info->owner_type_name);
                }
            }
        }

        freeMethodSignature(&current);
    }

    for (int i = 0; i < collection->user_type_count; i++) {
        const UserTypeInfo* type_info = &collection->user_types[i];
        if (type_info->kind != USER_TYPE_CLASS) {
            continue;
        }

        for (int iface_index = 0; iface_index < type_info->interface_count; iface_index++) {
            const UserTypeInfo* interface_type = findUserTypeInfo(collection, type_info->interface_names[iface_index]);
            if (!interface_type || interface_type->kind != USER_TYPE_INTERFACE) {
                continue;
            }

            for (int method_index = 0; method_index < interface_type->declared_method_count; method_index++) {
                const MethodSignatureInfo* required = &interface_type->declared_methods[method_index];
                const SubprogramInfo* found = findMethodInTypeHierarchy(collection, type_info, required);
                if (!found || (!found->has_body && !found->import_info.is_imported)) {
                    appendCollectionError(collection,
                                          "Class '%s' does not implement interface method '%s'.",
                                          type_info->name,
                                          required->name ? required->name : "<unknown>");
                }
            }
        }
    }

    assignAsmNames(collection);
}

SubprogramCollection generateSubprogramInfoCollection(const char* source_file, pANTLR3_BASE_TREE tree)
{
    SubprogramCollection collection;
    collection.items = NULL;
    collection.count = 0;
    collection.user_types = NULL;
    collection.user_type_count = 0;
    collection.errors = NULL;
    collection.error_count = 0;

    if (!source_file || !tree) {
        return collection;
    }

    collectProgramItems(&collection, source_file, tree);
    validateProgramItems(&collection);

    return collection;
}

SubprogramInfo* generateSubprogramInfo(const char* source_file, pANTLR3_BASE_TREE tree)
{
    SubprogramCollection collection = generateSubprogramInfoCollection(source_file, tree);
    if (collection.count <= 0 || !collection.items) {
        return NULL;
    }

    SubprogramInfo* info = malloc(sizeof(SubprogramInfo));
    if (!info) {
        freeSubprogramCollection(&collection);
        return NULL;
    }

    *info = collection.items[0];
    for (int i = 1; i < collection.count; i++) {
        SubprogramInfo* dropped = &collection.items[i];

        free(dropped->name);
        free(dropped->owner_type_name);
        free(dropped->asm_name);
        for (int p = 0; p < dropped->param_count; p++) {
            free(dropped->param_names ? dropped->param_names[p] : NULL);
            free(dropped->param_types ? dropped->param_types[p] : NULL);
        }
        free(dropped->param_names);
        free(dropped->param_types);

        free(dropped->return_type);

        for (int l = 0; l < dropped->local_count; l++) {
            free(dropped->local_names ? dropped->local_names[l] : NULL);
            free(dropped->local_types ? dropped->local_types[l] : NULL);
        }
        free(dropped->local_names);
        free(dropped->local_types);

        free(dropped->source_file);
        free(dropped->import_info.dll_name);
        free(dropped->import_info.entry_name);
        freeCFG(dropped->cfg);
    }

    for (int i = 0; i < collection.user_type_count; i++) {
        UserTypeInfo* type_info = &collection.user_types[i];
        free(type_info->name);
        free(type_info->base_type_name);
        for (int j = 0; j < type_info->interface_count; j++) {
            free(type_info->interface_names ? type_info->interface_names[j] : NULL);
        }
        free(type_info->interface_names);
        for (int j = 0; j < type_info->declared_field_count; j++) {
            free(type_info->declared_fields ? type_info->declared_fields[j].name : NULL);
            free(type_info->declared_fields ? type_info->declared_fields[j].type_name : NULL);
            free(type_info->declared_fields ? type_info->declared_fields[j].declaring_type_name : NULL);
        }
        free(type_info->declared_fields);
        for (int j = 0; j < type_info->resolved_field_count; j++) {
            free(type_info->resolved_fields ? type_info->resolved_fields[j].name : NULL);
            free(type_info->resolved_fields ? type_info->resolved_fields[j].type_name : NULL);
            free(type_info->resolved_fields ? type_info->resolved_fields[j].declaring_type_name : NULL);
        }
        free(type_info->resolved_fields);
        for (int j = 0; j < type_info->declared_method_count; j++) {
            freeMethodSignature(&type_info->declared_methods[j]);
        }
        free(type_info->declared_methods);
    }
    free(collection.user_types);
    for (int i = 0; i < collection.error_count; i++) {
        free(collection.errors ? collection.errors[i] : NULL);
    }
    free(collection.errors);
    free(collection.items);

    return info;
}

void freeCFG(ControlFlowGraph* cfg)
{
    if (!cfg) {
        return;
    }

    for (int i = 0; i < cfg->node_count; i++) {
        CFGNode* node = cfg->nodes ? cfg->nodes[i] : NULL;
        if (!node) {
            continue;
        }

        for (int s = 0; s < node->stmt_count; s++) {
            freeOpTree(node->statements ? node->statements[s] : NULL);
        }
        free(node->statements);
        free(node);
    }

    for (int i = 0; i < cfg->edge_count; i++) {
        free(cfg->edges ? cfg->edges[i] : NULL);
    }

    free(cfg->nodes);
    free(cfg->edges);
    free(cfg);
}

static void freeSubprogramInfo(SubprogramInfo* info)
{
    if (!info) {
        return;
    }

    free(info->name);
    free(info->owner_type_name);
    free(info->asm_name);

    for (int i = 0; i < info->param_count; i++) {
        free(info->param_names ? info->param_names[i] : NULL);
        free(info->param_types ? info->param_types[i] : NULL);
    }
    free(info->param_names);
    free(info->param_types);

    free(info->return_type);

    for (int i = 0; i < info->local_count; i++) {
        free(info->local_names ? info->local_names[i] : NULL);
        free(info->local_types ? info->local_types[i] : NULL);
    }
    free(info->local_names);
    free(info->local_types);

    free(info->source_file);
    free(info->import_info.dll_name);
    free(info->import_info.entry_name);
    freeCFG(info->cfg);

    initSubprogramInfo(info);
}

static void freeUserTypeInfo(UserTypeInfo* type_info)
{
    if (!type_info) {
        return;
    }

    free(type_info->name);
    free(type_info->base_type_name);
    for (int i = 0; i < type_info->interface_count; i++) {
        free(type_info->interface_names ? type_info->interface_names[i] : NULL);
    }
    free(type_info->interface_names);

    for (int i = 0; i < type_info->declared_field_count; i++) {
        free(type_info->declared_fields ? type_info->declared_fields[i].name : NULL);
        free(type_info->declared_fields ? type_info->declared_fields[i].type_name : NULL);
        free(type_info->declared_fields ? type_info->declared_fields[i].declaring_type_name : NULL);
    }
    free(type_info->declared_fields);

    for (int i = 0; i < type_info->resolved_field_count; i++) {
        free(type_info->resolved_fields ? type_info->resolved_fields[i].name : NULL);
        free(type_info->resolved_fields ? type_info->resolved_fields[i].type_name : NULL);
        free(type_info->resolved_fields ? type_info->resolved_fields[i].declaring_type_name : NULL);
    }
    free(type_info->resolved_fields);

    for (int i = 0; i < type_info->declared_method_count; i++) {
        freeMethodSignature(&type_info->declared_methods[i]);
    }
    free(type_info->declared_methods);

    initUserTypeInfo(type_info);
}

void freeSubprogramCollection(SubprogramCollection* collection)
{
    if (!collection) {
        return;
    }

    for (int i = 0; i < collection->count; i++) {
        freeSubprogramInfo(&collection->items[i]);
    }

    free(collection->items);
    collection->items = NULL;
    collection->count = 0;

    for (int i = 0; i < collection->user_type_count; i++) {
        freeUserTypeInfo(&collection->user_types[i]);
    }
    free(collection->user_types);
    collection->user_types = NULL;
    collection->user_type_count = 0;

    for (int i = 0; i < collection->error_count; i++) {
        free(collection->errors ? collection->errors[i] : NULL);
    }
    free(collection->errors);
    collection->errors = NULL;
    collection->error_count = 0;
}

static void call_graph_add_node(CallGraph* graph, const char* node_name)
{
    if (!graph || !node_name || node_name[0] == '\0') {
        return;
    }

    for (int i = 0; i < graph->node_count; i++) {
        if (strcmp(graph->node_names[i], node_name) == 0) {
            return;
        }
    }

    char** new_nodes = realloc(graph->node_names, sizeof(char*) * (graph->node_count + 1));
    if (!new_nodes) {
        return;
    }

    graph->node_names = new_nodes;
    graph->node_names[graph->node_count++] = strdup(node_name);
}

static void call_graph_add_edge(CallGraph* graph, const char* caller, const char* callee)
{
    if (!graph || !caller || !callee || caller[0] == '\0' || callee[0] == '\0') {
        return;
    }

    for (int i = 0; i < graph->edge_count; i++) {
        if (strcmp(graph->edges[i].caller_name, caller) == 0
            && strcmp(graph->edges[i].callee_name, callee) == 0) {
            return;
        }
    }

    CallGraphEdge* new_edges = realloc(graph->edges, sizeof(CallGraphEdge) * (graph->edge_count + 1));
    if (!new_edges) {
        return;
    }

    graph->edges = new_edges;
    graph->edges[graph->edge_count].caller_name = strdup(caller);
    graph->edges[graph->edge_count].callee_name = strdup(callee);
    graph->edge_count++;

    call_graph_add_node(graph, caller);
    call_graph_add_node(graph, callee);
}

static void collect_calls_from_op_tree(CallGraph* graph, const char* caller_name, const OpNode* node)
{
    if (!graph || !caller_name || !node) {
        return;
    }

    if ((node->type == OP_FUNCTION_CALL || node->type == OP_MEMBER_CALL) && node->text) {
        call_graph_add_edge(graph, caller_name, node->text);
    }

    for (int i = 0; i < node->operand_count; i++) {
        collect_calls_from_op_tree(graph, caller_name, node->operands[i]);
    }
}

CallGraph* buildCallGraph(const SubprogramCollection* collection)
{
    if (!collection) {
        return NULL;
    }

    CallGraph* graph = calloc(1, sizeof(CallGraph));
    if (!graph) {
        return NULL;
    }

    for (int i = 0; i < collection->count; i++) {
        const SubprogramInfo* info = &collection->items[i];
        const char* node_name = info ? (info->asm_name ? info->asm_name : info->name) : NULL;
        if (!info || !node_name) {
            continue;
        }

        call_graph_add_node(graph, node_name);
        if (!info->cfg) {
            continue;
        }

        for (int node_index = 0; node_index < info->cfg->node_count; node_index++) {
            CFGNode* cfg_node = info->cfg->nodes[node_index];
            if (!cfg_node) {
                continue;
            }

            for (int stmt_index = 0; stmt_index < cfg_node->stmt_count; stmt_index++) {
                collect_calls_from_op_tree(graph, node_name, cfg_node->statements[stmt_index]);
            }
        }
    }

    return graph;
}

void callGraphToDot(const CallGraph* graph, FILE* out)
{
    if (!graph || !out) {
        return;
    }

    fprintf(out, "digraph CallGraph {\n");
    fprintf(out, "  node [shape=box];\n\n");

    for (int i = 0; i < graph->node_count; i++) {
        fprintf(out, "  \"");
        fprintEscaped(out, graph->node_names[i]);
        fprintf(out, "\";\n");
    }

    if (graph->node_count > 0) {
        fprintf(out, "\n");
    }

    for (int i = 0; i < graph->edge_count; i++) {
        fprintf(out, "  \"");
        fprintEscaped(out, graph->edges[i].caller_name);
        fprintf(out, "\" -> \"");
        fprintEscaped(out, graph->edges[i].callee_name);
        fprintf(out, "\";\n");
    }

    fprintf(out, "}\n");
}

void freeCallGraph(CallGraph* graph)
{
    if (!graph) {
        return;
    }

    for (int i = 0; i < graph->node_count; i++) {
        free(graph->node_names ? graph->node_names[i] : NULL);
    }
    free(graph->node_names);

    for (int i = 0; i < graph->edge_count; i++) {
        free(graph->edges ? graph->edges[i].caller_name : NULL);
        free(graph->edges ? graph->edges[i].callee_name : NULL);
    }
    free(graph->edges);

    free(graph);
}
