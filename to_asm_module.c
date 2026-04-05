#include "to_asm_module.h"

#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RUNTIME_RETVAL_SLOT 7160
#define RUNTIME_DISPATCH_LABEL "M_sys_ret_dispatch"
#define PSEUDO_LABEL_MNEMONIC ".label"

typedef struct {
    Instruction* items;
    int count;
    int capacity;
} InstructionList;

typedef struct {
    DataItem* items;
    int count;
    int capacity;
} DataItemList;

typedef struct {
    int instr_index;
    int operand_index;
    CFGNode* target;
} JumpPatch;

typedef struct {
    JumpPatch* items;
    int count;
    int capacity;
} JumpPatchList;

typedef struct {
    CFGNode* node;
    int start_index;
} NodeEntry;

typedef struct {
    int id;
    char* continue_label;
} ReturnSite;

typedef struct {
    ReturnSite* items;
    int count;
    int capacity;
    int next_id;
} ReturnSiteList;

typedef struct {
    const SubprogramInfo* info;
    const SubprogramCollection* subprograms;
    ReturnSiteList* return_sites;
    bool is_main_method;
    bool method_returns_value;
    bool halt_if_true_branch;
    bool has_error;
    char error_message[256];
    InstructionList instructions;
    DataItemList data_items;
    const char** var_names;
    const char** var_types;
    int var_count;
} CodegenContext;

static int emit_instruction(CodegenContext* ctx, const char* mnemonic, int operand_count, const char** operands);
static int emit_instruction1(CodegenContext* ctx, const char* mnemonic, const char* operand);
static void emit_indexed_instruction(CodegenContext* ctx, const char* mnemonic, int index);
static bool emit_expression(CodegenContext* ctx, const OpNode* node);

static void instruction_list_init(InstructionList* list)
{
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void instruction_list_reserve(InstructionList* list, int extra)
{
    if (list->count + extra <= list->capacity) {
        return;
    }

    int new_capacity = list->capacity == 0 ? 16 : list->capacity * 2;
    while (new_capacity < list->count + extra) {
        new_capacity *= 2;
    }

    list->items = realloc(list->items, sizeof(Instruction) * new_capacity);
    list->capacity = new_capacity;
}

static int instruction_list_add(InstructionList* list, const char* mnemonic, int operand_count, const char** operands)
{
    instruction_list_reserve(list, 1);

    Instruction* instr = &list->items[list->count];
    instr->mnemonic = mnemonic ? strdup(mnemonic) : strdup("");
    instr->operand_count = operand_count;
    instr->operands = NULL;

    if (operand_count > 0) {
        instr->operands = malloc(sizeof(char*) * operand_count);
        for (int i = 0; i < operand_count; i++) {
            instr->operands[i] = operands && operands[i] ? strdup(operands[i]) : strdup("");
        }
    }

    return list->count++;
}

static void data_item_list_init(DataItemList* list)
{
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void data_item_list_reserve(DataItemList* list, int extra)
{
    if (list->count + extra <= list->capacity) {
        return;
    }

    int new_capacity = list->capacity == 0 ? 16 : list->capacity * 2;
    while (new_capacity < list->count + extra) {
        new_capacity *= 2;
    }

    list->items = realloc(list->items, sizeof(DataItem) * new_capacity);
    list->capacity = new_capacity;
}

static void data_item_list_add_size(DataItemList* list, int size_bytes)
{
    data_item_list_reserve(list, 1);
    DataItem* item = &list->items[list->count++];
    item->kind = DATA_ITEM_TYPE_SIZE;
    item->value.size_bytes = size_bytes;
}

static void jump_patch_list_init(JumpPatchList* list)
{
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void jump_patch_list_add(JumpPatchList* list, int instr_index, int operand_index, CFGNode* target)
{
    if (list->count + 1 > list->capacity) {
        int new_capacity = list->capacity == 0 ? 16 : list->capacity * 2;
        list->items = realloc(list->items, sizeof(JumpPatch) * new_capacity);
        list->capacity = new_capacity;
    }

    JumpPatch* patch = &list->items[list->count++];
    patch->instr_index = instr_index;
    patch->operand_index = operand_index;
    patch->target = target;
}

static void return_site_list_init(ReturnSiteList* list)
{
    if (!list) {
        return;
    }
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
    list->next_id = 1;
}

static void return_site_list_free(ReturnSiteList* list)
{
    if (!list) {
        return;
    }

    for (int i = 0; i < list->count; i++) {
        free(list->items[i].continue_label);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
    list->next_id = 1;
}

static const ReturnSite* return_site_list_add(ReturnSiteList* list)
{
    if (!list) {
        return NULL;
    }

    if (list->count + 1 > list->capacity) {
        int new_capacity = list->capacity == 0 ? 16 : list->capacity * 2;
        ReturnSite* new_items = realloc(list->items, sizeof(ReturnSite) * new_capacity);
        if (!new_items) {
            return NULL;
        }
        list->items = new_items;
        list->capacity = new_capacity;
    }

    ReturnSite* site = &list->items[list->count++];
    site->id = list->next_id++;

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "M_sys_ret_%d", site->id);
    site->continue_label = strdup(buffer);
    if (!site->continue_label) {
        list->count--;
        return NULL;
    }

    return site;
}

static char* format_int(int value)
{
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d", value);
    return strdup(buffer);
}

static bool equals_ignore_case(const char* a, const char* b)
{
    if (!a || !b) {
        return false;
    }

    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}

static bool parse_index_operand(const char* text, int* out_value)
{
    if (!text || !out_value) {
        return false;
    }

    char* endptr = NULL;
    long value = strtol(text, &endptr, 10);
    if (!endptr || *endptr != '\0') {
        return false;
    }

    if (value < 0 || value > INT_MAX) {
        return false;
    }

    *out_value = (int)value;
    return true;
}

static bool is_jump_mnemonic(const char* mnemonic)
{
    return equals_ignore_case(mnemonic, "jmp")
        || equals_ignore_case(mnemonic, "jz")
        || equals_ignore_case(mnemonic, "jnz");
}

static char* sanitize_label(const char* name)
{
    const char* raw = (name && name[0]) ? name : "entry";
    size_t len = strlen(raw);
    size_t extra = 0;

    if (!isalpha((unsigned char)raw[0])) {
        extra = 2;
    }

    char* label = malloc(len + extra + 1);
    if (!label) {
        return strdup("entry");
    }

    size_t pos = 0;
    if (extra) {
        label[pos++] = 'M';
        label[pos++] = '_';
    }

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)raw[i];
        if (isalnum(c) || c == '_') {
            label[pos++] = (char)c;
        } else {
            label[pos++] = '_';
        }
    }

    label[pos] = '\0';
    return label;
}

static void set_codegen_error(CodegenContext* ctx, const char* message)
{
    if (!ctx || ctx->has_error) {
        return;
    }

    ctx->has_error = true;
    if (!message) {
        ctx->error_message[0] = '\0';
        return;
    }

    snprintf(ctx->error_message, sizeof(ctx->error_message), "%s", message);
}

static const SubprogramInfo* find_global_subprogram_by_name(const SubprogramCollection* subprograms, const char* name)
{
    if (!subprograms || !name) {
        return NULL;
    }

    for (int i = 0; i < subprograms->count; i++) {
        const SubprogramInfo* info = &subprograms->items[i];
        if (info->owner_type_name) {
            continue;
        }
        if (info->name && strcmp(info->name, name) == 0) {
            return info;
        }
    }

    return NULL;
}

static bool subprogram_returns_value(const SubprogramInfo* info)
{
    if (!info || !info->return_type) {
        return false;
    }

    return !equals_ignore_case(info->return_type, "void");
}

static bool is_empty_builtin(const SubprogramInfo* info, const char* expected_name)
{
    if (!info || !expected_name || !info->name) {
        return false;
    }

    if (info->owner_type_name) {
        return false;
    }

    if (!equals_ignore_case(info->name, expected_name)) {
        return false;
    }

    if (!info->cfg) {
        return true;
    }

    // Treat declaration-only and truly empty bodies (begin end;) as built-ins.
    for (int i = 0; i < info->cfg->node_count; i++) {
        CFGNode* node = info->cfg->nodes[i];
        if (!node) {
            continue;
        }

        if (node->stmt_count > 0) {
            return false;
        }

        if (node->type != NODE_ENTRY && node->type != NODE_EXIT) {
            return false;
        }
    }

    return true;
}

static bool is_read_builtin(const SubprogramInfo* info)
{
    return is_empty_builtin(info, "read") && info->param_count == 0;
}

static bool is_write_builtin(const SubprogramInfo* info)
{
    if (!is_empty_builtin(info, "write")) {
        return false;
    }

    if (info->param_count != 1 || !info->param_types || !info->param_types[0]) {
        return false;
    }

    return equals_ignore_case(info->param_types[0], "int");
}

static bool is_builtin_type_name(const char* type_name)
{
    if (!type_name) {
        return false;
    }

    return equals_ignore_case(type_name, "bool")
        || equals_ignore_case(type_name, "byte")
        || equals_ignore_case(type_name, "int")
        || equals_ignore_case(type_name, "uint")
        || equals_ignore_case(type_name, "long")
        || equals_ignore_case(type_name, "ulong")
        || equals_ignore_case(type_name, "char")
        || equals_ignore_case(type_name, "string")
        || equals_ignore_case(type_name, "void");
}

static void append_codegen_slot(CodegenContext* ctx, const char* name, const char* type_name)
{
    if (!ctx || !name || !type_name) {
        return;
    }

    const char** new_names = realloc((void*)ctx->var_names, sizeof(char*) * (ctx->var_count + 1));
    const char** new_types = realloc((void*)ctx->var_types, sizeof(char*) * (ctx->var_count + 1));
    if (!new_names || !new_types) {
        return;
    }

    ctx->var_names = new_names;
    ctx->var_types = new_types;
    ctx->var_names[ctx->var_count] = strdup(name);
    ctx->var_types[ctx->var_count] = strdup(type_name);
    data_item_list_add_size(&ctx->data_items, getTypeSizeBytes(ctx->subprograms, type_name));
    ctx->var_count++;
}

static void append_flattened_slots(CodegenContext* ctx, const char* prefix, const char* type_name)
{
    if (!ctx || !prefix || !type_name) {
        return;
    }

    const UserTypeInfo* type_info = findUserTypeInfo(ctx->subprograms, type_name);
    if (!type_info || type_info->kind != USER_TYPE_CLASS) {
        append_codegen_slot(ctx, prefix, type_name);
        return;
    }

    if (type_info->resolved_field_count == 0) {
        append_codegen_slot(ctx, prefix, "int");
        return;
    }

    for (int i = 0; i < type_info->resolved_field_count; i++) {
        const FieldInfo* field = &type_info->resolved_fields[i];
        char buffer[512];
        snprintf(buffer, sizeof(buffer), "%s.%s", prefix, field->name ? field->name : "field");
        if (is_builtin_type_name(field->type_name)) {
            append_codegen_slot(ctx, buffer, field->type_name);
        } else {
            append_flattened_slots(ctx, buffer, field->type_name);
        }
    }
}

static int find_var_index_exact(const CodegenContext* ctx, const char* name)
{
    if (!ctx || !name) {
        return -1;
    }

    for (int i = 0; i < ctx->var_count; i++) {
        if (ctx->var_names[i] && strcmp(ctx->var_names[i], name) == 0) {
            return i;
        }
    }

    return -1;
}

static int find_var_index(const CodegenContext* ctx, const char* name)
{
    int exact_index = find_var_index_exact(ctx, name);
    if (exact_index >= 0) {
        return exact_index;
    }

    if (ctx && ctx->info && ctx->info->owner_type_name && name && strncmp(name, "this.", 5) != 0) {
        char buffer[512];
        snprintf(buffer, sizeof(buffer), "this.%s", name);
        return find_var_index_exact(ctx, buffer);
    }

    return -1;
}

static const char* find_var_type(const CodegenContext* ctx, const char* name)
{
    int index = find_var_index(ctx, name);
    if (index < 0 || !ctx || !ctx->var_types) {
        return NULL;
    }
    return ctx->var_types[index];
}

static const char* find_declared_identifier_type(const CodegenContext* ctx, const char* name)
{
    if (!ctx || !ctx->info || !name) {
        return NULL;
    }

    if (ctx->info->owner_type_name && strcmp(name, "this") == 0) {
        return ctx->info->owner_type_name;
    }

    for (int i = 0; i < ctx->info->param_count; i++) {
        const char* param_name = ctx->info->param_names ? ctx->info->param_names[i] : NULL;
        const char* param_type = ctx->info->param_types ? ctx->info->param_types[i] : NULL;
        if (param_name && param_type && strcmp(param_name, name) == 0) {
            return param_type;
        }
    }

    for (int i = 0; i < ctx->info->local_count; i++) {
        const char* local_name = ctx->info->local_names ? ctx->info->local_names[i] : NULL;
        const char* local_type = ctx->info->local_types ? ctx->info->local_types[i] : NULL;
        if (local_name && local_type && strcmp(local_name, name) == 0) {
            return local_type;
        }
    }

    if (ctx->info->owner_type_name) {
        const UserTypeInfo* owner_type = findUserTypeInfo(ctx->subprograms, ctx->info->owner_type_name);
        const FieldInfo* field = findResolvedFieldInfo(owner_type, name);
        if (field && field->type_name) {
            return field->type_name;
        }
    }

    return NULL;
}

static const char* find_member_type(const CodegenContext* ctx, const char* owner_type_name, const char* member_name)
{
    if (!ctx || !owner_type_name || !member_name) {
        return NULL;
    }

    const UserTypeInfo* owner_type = findUserTypeInfo(ctx->subprograms, owner_type_name);
    const FieldInfo* field = findResolvedFieldInfo(owner_type, member_name);
    return field ? field->type_name : NULL;
}

static char* build_access_path(const OpNode* node)
{
    if (!node) {
        return NULL;
    }

    if (node->type == OP_IDENTIFIER) {
        return node->text ? strdup(node->text) : NULL;
    }

    if (node->type == OP_MEMBER_ACCESS && node->operand_count > 0) {
        char* base = build_access_path(node->operands[0]);
        if (!base) {
            return NULL;
        }

        size_t needed = strlen(base) + strlen(node->text ? node->text : "") + 2;
        char* path = malloc(needed);
        if (!path) {
            free(base);
            return NULL;
        }

        snprintf(path, needed, "%s.%s", base, node->text ? node->text : "");
        free(base);
        return path;
    }

    return NULL;
}

static bool parse_binary_literal(const char* text, int* out_value)
{
    int value = 0;
    for (const char* p = text; *p; ++p) {
        if (*p == '0' || *p == '1') {
            value = (value << 1) | (*p - '0');
        } else {
            return false;
        }
    }

    *out_value = value;
    return true;
}

static bool parse_char_literal(const char* text, int* out_value)
{
    if (!text || !out_value) {
        return false;
    }

    size_t len = strlen(text);
    if (len < 3 || text[0] != '\'' || text[len - 1] != '\'') {
        return false;
    }

    if (text[1] == '\\') {
        if (len != 4) {
            return false;
        }

        switch (text[2]) {
            case 'n': *out_value = '\n'; return true;
            case 'r': *out_value = '\r'; return true;
            case 't': *out_value = '\t'; return true;
            case '0': *out_value = '\0'; return true;
            case '\'': *out_value = '\''; return true;
            case '"': *out_value = '"'; return true;
            case '\\': *out_value = '\\'; return true;
            default:
                *out_value = (unsigned char)text[2];
                return true;
        }
    }

    if (len != 3) {
        return false;
    }

    *out_value = (unsigned char)text[1];
    return true;
}

static bool parse_int_literal(const char* text, int* out_value)
{
    if (!text || !out_value) {
        return false;
    }

    if (strcmp(text, "true") == 0) {
        *out_value = 1;
        return true;
    }

    if (strcmp(text, "false") == 0) {
        *out_value = 0;
        return true;
    }

    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        char* endptr = NULL;
        long value = strtol(text + 2, &endptr, 16);
        if (endptr && *endptr == '\0') {
            *out_value = (int)value;
            return true;
        }
        return false;
    }

    if (text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
        return parse_binary_literal(text + 2, out_value);
    }

    if (parse_char_literal(text, out_value)) {
        return true;
    }

    if (text[0] == '"' || text[0] == '\'' || text[0] == '\0') {
        return false;
    }

    char* endptr = NULL;
    long value = strtol(text, &endptr, 10);
    if (endptr && *endptr == '\0') {
        *out_value = (int)value;
        return true;
    }

    return false;
}

static const char* infer_expression_type(CodegenContext* ctx, const OpNode* node);

static const SubprogramInfo* find_method_on_type(const SubprogramCollection* subprograms,
                                                 const char* owner_type_name,
                                                 const char* method_name,
                                                 const OpNode* call_node,
                                                 CodegenContext* ctx)
{
    if (!subprograms || !owner_type_name || !method_name || !call_node) {
        return NULL;
    }

    const UserTypeInfo* type_info = findUserTypeInfo(subprograms, owner_type_name);
    if (!type_info) {
        return NULL;
    }

    for (int i = 0; i < subprograms->count; i++) {
        const SubprogramInfo* info = &subprograms->items[i];
        if (!info->owner_type_name || strcmp(info->owner_type_name, type_info->name) != 0) {
            continue;
        }
        if (!info->name || strcmp(info->name, method_name) != 0) {
            continue;
        }
        if (info->param_count != call_node->operand_count - 1) {
            continue;
        }

        bool matches = true;
        for (int arg_index = 1; arg_index < call_node->operand_count; arg_index++) {
            const char* arg_type = infer_expression_type(ctx, call_node->operands[arg_index]);
            const char* param_type = info->param_types ? info->param_types[arg_index - 1] : NULL;
            if (!arg_type || !param_type || strcmp(arg_type, param_type) != 0) {
                matches = false;
                break;
            }
        }

        if (matches) {
            return info;
        }
    }

    if (type_info->base_type_name) {
        return find_method_on_type(subprograms, type_info->base_type_name, method_name, call_node, ctx);
    }

    return NULL;
}

static const char* infer_expression_type(CodegenContext* ctx, const OpNode* node)
{
    if (!ctx || !node) {
        return NULL;
    }

    switch (node->type) {
        case OP_LITERAL:
            if (!node->text) {
                return "int";
            }
            if (strcmp(node->text, "true") == 0 || strcmp(node->text, "false") == 0) {
                return "bool";
            }
            if (node->text[0] == '"') {
                return "string";
            }
            if (node->text[0] == '\'') {
                return "char";
            }
            return "int";
        case OP_IDENTIFIER:
        {
            const char* type_name = find_var_type(ctx, node->text);
            return type_name ? type_name : find_declared_identifier_type(ctx, node->text);
        }
        case OP_MEMBER_ACCESS: {
            char* path = build_access_path(node);
            const char* type_name = path ? find_var_type(ctx, path) : NULL;
            free(path);
            if (type_name) {
                return type_name;
            }

            if (node->operand_count > 0 && node->text) {
                const char* owner_type = infer_expression_type(ctx, node->operands[0]);
                return find_member_type(ctx, owner_type, node->text);
            }

            return NULL;
        }
        case OP_ASSIGNMENT:
            if (node->operand_count > 0) {
                return infer_expression_type(ctx, node->operands[0]);
            }
            return NULL;
        case OP_ADDITION:
        case OP_SUBTRACTION:
        case OP_MULTIPLICATION:
        case OP_DIVISION:
        case OP_MODULO:
        case OP_UNARY_PLUS:
        case OP_UNARY_MINUS:
            return "int";
        case OP_LOGICAL_AND:
        case OP_LOGICAL_OR:
        case OP_LOGICAL_NOT:
        case OP_EQUAL:
        case OP_NOT_EQUAL:
        case OP_LESS_THAN:
        case OP_LESS_THAN_OR_EQUAL:
        case OP_GREATER_THAN:
        case OP_GREATER_THAN_OR_EQUAL:
            return "bool";
        case OP_FUNCTION_CALL: {
            const SubprogramInfo* callee = find_global_subprogram_by_name(ctx->subprograms, node->text);
            return callee ? callee->return_type : NULL;
        }
        case OP_MEMBER_CALL: {
            if (node->operand_count <= 0) {
                return NULL;
            }
            const char* owner_type = infer_expression_type(ctx, node->operands[0]);
            const SubprogramInfo* callee = find_method_on_type(ctx->subprograms, owner_type, node->text, node, ctx);
            return callee ? callee->return_type : NULL;
        }
        default:
            return NULL;
    }
}

static void emit_load_from_path(CodegenContext* ctx, const char* path)
{
    int index = find_var_index(ctx, path);
    if (index >= 0) {
        emit_indexed_instruction(ctx, "ldg", index);
    } else {
        emit_instruction1(ctx, "pushi", "0");
    }
}

static void emit_store_to_path(CodegenContext* ctx, const char* path)
{
    int index = find_var_index(ctx, path);
    if (index >= 0) {
        emit_indexed_instruction(ctx, "stg", index);
    } else {
        emit_instruction(ctx, "pop", 0, NULL);
    }
}

static void emit_receiver_slots(CodegenContext* ctx, const char* prefix, const char* type_name)
{
    if (!ctx || !prefix || !type_name) {
        return;
    }

    const UserTypeInfo* type_info = findUserTypeInfo(ctx->subprograms, type_name);
    if (!type_info || type_info->kind != USER_TYPE_CLASS) {
        emit_load_from_path(ctx, prefix);
        return;
    }

    for (int i = 0; i < type_info->resolved_field_count; i++) {
        const FieldInfo* field = &type_info->resolved_fields[i];
        char buffer[512];
        snprintf(buffer, sizeof(buffer), "%s.%s", prefix, field->name ? field->name : "field");
        if (is_builtin_type_name(field->type_name)) {
            emit_load_from_path(ctx, buffer);
        } else {
            emit_receiver_slots(ctx, buffer, field->type_name);
        }
    }
}

static int emit_instruction(CodegenContext* ctx, const char* mnemonic, int operand_count, const char** operands)
{
    return instruction_list_add(&ctx->instructions, mnemonic, operand_count, operands);
}

static int emit_instruction1(CodegenContext* ctx, const char* mnemonic, const char* operand)
{
    const char* ops[1] = { operand };
    return emit_instruction(ctx, mnemonic, 1, ops);
}

static int emit_label(CodegenContext* ctx, const char* label)
{
    return emit_instruction1(ctx, PSEUDO_LABEL_MNEMONIC, label);
}

static void emit_indexed_instruction(CodegenContext* ctx, const char* mnemonic, int index)
{
    char* operand = format_int(index);
    emit_instruction1(ctx, mnemonic, operand);
    free(operand);
}

static bool emit_expression(CodegenContext* ctx, const OpNode* node);

static bool emit_binary_left_fold(CodegenContext* ctx, const OpNode* node, const char* mnemonic)
{
    if (!node || node->operand_count == 0) {
        return false;
    }

    emit_expression(ctx, node->operands[0]);
    for (int i = 1; i < node->operand_count; i++) {
        emit_expression(ctx, node->operands[i]);
        emit_instruction(ctx, mnemonic, 0, NULL);
    }

    return true;
}

static int count_flattened_type_slots(const SubprogramCollection* subprograms, const char* type_name)
{
    const UserTypeInfo* type_info = findUserTypeInfo(subprograms, type_name);
    if (!type_info || type_info->kind != USER_TYPE_CLASS) {
        return 1;
    }

    int count = 0;
    for (int i = 0; i < type_info->resolved_field_count; i++) {
        const FieldInfo* field = &type_info->resolved_fields[i];
        if (is_builtin_type_name(field->type_name)) {
            count++;
        } else {
            count += count_flattened_type_slots(subprograms, field->type_name);
        }
    }
    return count;
}

static int get_subprogram_slot_count(const SubprogramCollection* subprograms, const SubprogramInfo* info)
{
    if (!info) {
        return 0;
    }

    int slot_count = 0;
    if (info->owner_type_name) {
        slot_count += count_flattened_type_slots(subprograms, info->owner_type_name);
    }
    for (int i = 0; i < info->param_count; i++) {
        slot_count += count_flattened_type_slots(subprograms, info->param_types ? info->param_types[i] : NULL);
    }
    return slot_count;
}

static bool emit_call_common(CodegenContext* ctx,
                             const SubprogramInfo* callee,
                             const OpNode* receiver_node,
                             const OpNode* call_node,
                             int explicit_arg_start)
{
    if (!ctx || !callee || !call_node) {
        return false;
    }

    if (callee->import_info.is_imported) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "Imported method '%s' cannot be executed because import calls are not supported by the VM backend.",
                 callee->name ? callee->name : "<unknown>");
        set_codegen_error(ctx, buffer);
        return false;
    }

    if (callee->owner_type_name && !receiver_node) {
        set_codegen_error(ctx, "Internal error: method call is missing receiver.");
        return false;
    }

    if (!ctx->return_sites) {
        set_codegen_error(ctx, "Internal error: call return-site tracker is missing.");
        return false;
    }

    if (!is_builtin_type_name(callee->return_type) && !equals_ignore_case(callee->return_type, "void")) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "Custom return type '%s' is not supported in backend calls to '%s'.",
                 callee->return_type ? callee->return_type : "<unknown>",
                 callee->name ? callee->name : "<unknown>");
        set_codegen_error(ctx, buffer);
        return false;
    }

    const ReturnSite* return_site = return_site_list_add(ctx->return_sites);
    if (!return_site || !return_site->continue_label) {
        set_codegen_error(ctx, "Out of memory while preparing call return site.");
        return false;
    }

    for (int i = 0; i < ctx->var_count; i++) {
        emit_indexed_instruction(ctx, "ldg", i);
    }

    if (receiver_node) {
        char* receiver_path = build_access_path(receiver_node);
        const char* receiver_type = callee->owner_type_name ? callee->owner_type_name
                                                            : infer_expression_type(ctx, receiver_node);
        if (!receiver_path || !receiver_type) {
            free(receiver_path);
            set_codegen_error(ctx, "Failed to resolve receiver for member call.");
            return false;
        }
        emit_receiver_slots(ctx, receiver_path, receiver_type);
        free(receiver_path);
    }

    for (int i = explicit_arg_start; i < call_node->operand_count; i++) {
        bool has_value = emit_expression(ctx, call_node->operands[i]);
        if (ctx->has_error) {
            return false;
        }
        if (!has_value) {
            emit_instruction1(ctx, "pushi", "0");
        }
    }

    int callee_slot_count = get_subprogram_slot_count(ctx->subprograms, callee);
    for (int i = callee_slot_count - 1; i >= 0; i--) {
        emit_indexed_instruction(ctx, "stg", i);
    }

    char* return_id = format_int(return_site->id);
    emit_instruction1(ctx, "pushi", return_id);
    free(return_id);

    char* label = sanitize_label(callee->asm_name ? callee->asm_name : callee->name);
    emit_instruction1(ctx, "jmp", label);
    free(label);

    emit_label(ctx, return_site->continue_label);

    for (int i = ctx->var_count - 1; i >= 0; i--) {
        emit_indexed_instruction(ctx, "stg", i);
    }

    if (subprogram_returns_value(callee)) {
        emit_indexed_instruction(ctx, "ldg", RUNTIME_RETVAL_SLOT);
        return true;
    }

    return false;
}

static bool emit_function_call(CodegenContext* ctx, const OpNode* node)
{
    if (!ctx || !node || !node->text) {
        return false;
    }

    const SubprogramInfo* callee = find_global_subprogram_by_name(ctx->subprograms, node->text);

    if (callee && is_read_builtin(callee)) {
        if (node->operand_count != 0) {
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "Method read() must be called without arguments in '%s'.",
                     ctx->info && ctx->info->name ? ctx->info->name : "<unknown>");
            set_codegen_error(ctx, buffer);
            return false;
        }
        emit_instruction(ctx, "in", 0, NULL);
        return true;
    }

    if (callee && is_write_builtin(callee)) {
        if (node->operand_count != 1) {
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "Method write(num: int) expects 1 argument in '%s'.",
                     ctx->info && ctx->info->name ? ctx->info->name : "<unknown>");
            set_codegen_error(ctx, buffer);
            return false;
        }

        bool has_value = emit_expression(ctx, node->operands[0]);
        if (!has_value) {
            emit_instruction1(ctx, "pushi", "0");
        }
        emit_instruction(ctx, "out", 0, NULL);
        return false;
    }

    if (strcmp(node->text, "setport") == 0) {
        if (node->operand_count == 1 && node->operands[0] && node->operands[0]->type == OP_LITERAL) {
            int value = 0;
            if (parse_int_literal(node->operands[0]->text, &value)) {
                char* operand = format_int(value);
                emit_instruction1(ctx, "setport", operand);
                free(operand);
                return false;
            }
        }

        if (node->operand_count == 1 && node->operands[0]) {
            emit_expression(ctx, node->operands[0]);
        }
        emit_instruction(ctx, "setport", 0, NULL);
        return false;
    }

    if (!callee) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "Unknown method call '%s' in '%s'.",
                 node->text,
                 ctx->info && ctx->info->name ? ctx->info->name : "<unknown>");
        set_codegen_error(ctx, buffer);
        return false;
    }

    if (node->operand_count != callee->param_count) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer),
                 "Method '%s' expects %d argument(s), but %d provided in '%s'.",
                 callee->name ? callee->name : "<unknown>",
                 callee->param_count,
                 node->operand_count,
                 ctx->info && ctx->info->name ? ctx->info->name : "<unknown>");
        set_codegen_error(ctx, buffer);
        return false;
    }

    return emit_call_common(ctx, callee, NULL, node, 0);
}

static bool emit_member_call(CodegenContext* ctx, const OpNode* node)
{
    if (!ctx || !node || !node->text || node->operand_count <= 0) {
        return false;
    }

    const char* owner_type = infer_expression_type(ctx, node->operands[0]);
    if (!owner_type) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "Cannot resolve receiver type for method '%s' in '%s'.",
                 node->text,
                 ctx->info && ctx->info->name ? ctx->info->name : "<unknown>");
        set_codegen_error(ctx, buffer);
        return false;
    }

    const SubprogramInfo* callee = find_method_on_type(ctx->subprograms, owner_type, node->text, node, ctx);
    if (!callee) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "Unknown method overload '%s' for receiver type '%s' in '%s'.",
                 node->text,
                 owner_type,
                 ctx->info && ctx->info->name ? ctx->info->name : "<unknown>");
        set_codegen_error(ctx, buffer);
        return false;
    }

    return emit_call_common(ctx, callee, node->operands[0], node, 1);
}

static bool emit_expression(CodegenContext* ctx, const OpNode* node)
{
    if (!ctx || !node || ctx->has_error) {
        return false;
    }

    switch (node->type) {
        case OP_LITERAL: {
            if (!node->text) {
                emit_instruction1(ctx, "pushi", "0");
                return true;
            }

            if (strcmp(node->text, "true") == 0) {
                emit_instruction1(ctx, "pushb", "1");
                return true;
            }
            if (strcmp(node->text, "false") == 0) {
                emit_instruction1(ctx, "pushb", "0");
                return true;
            }

            int value = 0;
            if (parse_int_literal(node->text, &value)) {
                char* operand = format_int(value);
                emit_instruction1(ctx, "pushi", operand);
                free(operand);
            } else {
                emit_instruction1(ctx, "pushi", "0");
            }
            return true;
        }
        case OP_IDENTIFIER: {
            emit_load_from_path(ctx, node->text);
            return true;
        }
        case OP_MEMBER_ACCESS: {
            char* path = build_access_path(node);
            if (path) {
                emit_load_from_path(ctx, path);
                free(path);
            } else {
                emit_instruction1(ctx, "pushi", "0");
            }
            return true;
        }
        case OP_ASSIGNMENT: {
            if (node->operand_count >= 2) {
                const OpNode* target = node->operands[0];
                const OpNode* value = node->operands[1];
                emit_expression(ctx, value);

                if (target && target->type == OP_IDENTIFIER) {
                    emit_store_to_path(ctx, target->text);
                } else if (target && target->type == OP_MEMBER_ACCESS) {
                    char* path = build_access_path(target);
                    if (path) {
                        emit_store_to_path(ctx, path);
                        free(path);
                    } else {
                        emit_instruction(ctx, "pop", 0, NULL);
                    }
                } else {
                    emit_instruction(ctx, "pop", 0, NULL);
                }
            }
            return false;
        }
        case OP_ADDITION:
            return emit_binary_left_fold(ctx, node, "add");
        case OP_SUBTRACTION:
            return emit_binary_left_fold(ctx, node, "sub");
        case OP_MULTIPLICATION:
            return emit_binary_left_fold(ctx, node, "mul");
        case OP_DIVISION:
            return emit_binary_left_fold(ctx, node, "div");
        case OP_MODULO:
            return emit_binary_left_fold(ctx, node, "mod");
        case OP_LOGICAL_AND:
            return emit_binary_left_fold(ctx, node, "and");
        case OP_LOGICAL_OR:
            return emit_binary_left_fold(ctx, node, "or");
        case OP_EQUAL:
            return emit_binary_left_fold(ctx, node, "eq");
        case OP_NOT_EQUAL:
            return emit_binary_left_fold(ctx, node, "ne");
        case OP_LESS_THAN:
            return emit_binary_left_fold(ctx, node, "lt");
        case OP_LESS_THAN_OR_EQUAL:
            return emit_binary_left_fold(ctx, node, "le");
        case OP_GREATER_THAN:
            return emit_binary_left_fold(ctx, node, "gt");
        case OP_GREATER_THAN_OR_EQUAL:
            return emit_binary_left_fold(ctx, node, "ge");
        case OP_UNARY_PLUS: {
            if (node->operand_count > 0) {
                return emit_expression(ctx, node->operands[0]);
            }
            return false;
        }
        case OP_UNARY_MINUS: {
            if (node->operand_count > 0) {
                emit_instruction1(ctx, "pushi", "0");
                emit_expression(ctx, node->operands[0]);
                emit_instruction(ctx, "sub", 0, NULL);
                return true;
            }
            return false;
        }
        case OP_LOGICAL_NOT: {
            if (node->operand_count > 0) {
                emit_expression(ctx, node->operands[0]);
                emit_instruction1(ctx, "pushi", "0");
                emit_instruction(ctx, "eq", 0, NULL);
                return true;
            }
            return false;
        }
        case OP_FUNCTION_CALL:
            return emit_function_call(ctx, node);
        case OP_MEMBER_CALL:
            return emit_member_call(ctx, node);
        case OP_ARRAY_INDEX:
        case OP_UNKNOWN:
        default: {
            bool result = false;
            for (int i = 0; i < node->operand_count; i++) {
                bool has_value = emit_expression(ctx, node->operands[i]);
                if (i < node->operand_count - 1 && has_value) {
                    emit_instruction(ctx, "pop", 0, NULL);
                }
                result = has_value;
            }
            if (node->operand_count == 0) {
                emit_instruction1(ctx, "pushi", "0");
                result = true;
            }
            return result;
        }
    }
}

static void emit_statement(CodegenContext* ctx, const OpNode* node)
{
    if (!ctx || !node || ctx->has_error) {
        return;
    }

    if (node->type == OP_ASSIGNMENT) {
        emit_expression(ctx, node);
        return;
    }

    bool has_value = emit_expression(ctx, node);
    if (has_value) {
        emit_instruction(ctx, "pop", 0, NULL);
    }
}

static int find_node_start(NodeEntry* entries, int count, CFGNode* target)
{
    for (int i = 0; i < count; i++) {
        if (entries[i].node == target) {
            return entries[i].start_index;
        }
    }
    return -1;
}

static bool has_incoming_if_true_edge(const ControlFlowGraph* cfg, const CFGNode* node)
{
    if (!cfg || !node) {
        return false;
    }

    for (int i = 0; i < cfg->edge_count; i++) {
        CFGEdge* edge = cfg->edges[i];
        if (!edge || edge->to != node) {
            continue;
        }

        if (edge->type == EDGE_TRUE && edge->from && edge->from->type == NODE_IF) {
            return true;
        }
    }

    return false;
}

static void emit_jump(CodegenContext* ctx, JumpPatchList* patches, const char* mnemonic, CFGNode* target)
{
    if (!ctx || !patches || !mnemonic || !target) {
        return;
    }

    const char* placeholder = "0";
    int instr_index = emit_instruction1(ctx, mnemonic, placeholder);
    jump_patch_list_add(patches, instr_index, 0, target);
}

static void emit_node(CodegenContext* ctx, CFGNode* node, JumpPatchList* patches)
{
    if (!ctx || !node || ctx->has_error) {
        return;
    }

    bool is_conditional = node->type == NODE_IF
        || node->type == NODE_WHILE
        || node->type == NODE_REPEAT_CONDITION;

    if (node->type == NODE_ENTRY && ctx->method_returns_value && !ctx->is_main_method) {
        emit_instruction1(ctx, "pushi", "0");
        emit_indexed_instruction(ctx, "stg", RUNTIME_RETVAL_SLOT);
    }

    if (node->type == NODE_EXIT) {
        if (ctx->is_main_method) {
            emit_instruction(ctx, "halt", 0, NULL);
        } else {
            emit_instruction1(ctx, "jmp", RUNTIME_DISPATCH_LABEL);
        }
        return;
    }

    if (is_conditional) {
        for (int i = 0; i < node->stmt_count; i++) {
            bool has_value = emit_expression(ctx, node->statements[i]);
            if (i < node->stmt_count - 1 && has_value) {
                emit_instruction(ctx, "pop", 0, NULL);
            }
        }

        if (node->nextConditional && node->nextDefault) {
            emit_jump(ctx, patches, "jz", node->nextDefault);
            emit_jump(ctx, patches, "jmp", node->nextConditional);
        } else if (node->nextConditional) {
            emit_jump(ctx, patches, "jnz", node->nextConditional);
        } else if (node->nextDefault) {
            emit_jump(ctx, patches, "jmp", node->nextDefault);
        }

        return;
    }

    bool tail_returns = ctx->method_returns_value
        && node->stmt_count > 0
        && node->nextDefault
        && node->nextDefault->type == NODE_EXIT
        && !node->nextConditional;
    int statement_count = tail_returns ? node->stmt_count - 1 : node->stmt_count;

    for (int i = 0; i < statement_count; i++) {
        emit_statement(ctx, node->statements[i]);
    }

    if (tail_returns) {
        bool has_value = emit_expression(ctx, node->statements[node->stmt_count - 1]);
        if (!has_value) {
            emit_instruction1(ctx, "pushi", "0");
        }
        emit_indexed_instruction(ctx, "stg", RUNTIME_RETVAL_SLOT);
    }

    if (ctx->halt_if_true_branch && has_incoming_if_true_edge(ctx->info ? ctx->info->cfg : NULL, node)) {
        emit_instruction(ctx, "halt", 0, NULL);
        return;
    }

    if (node->nextDefault) {
        emit_jump(ctx, patches, "jmp", node->nextDefault);
    } else if (node->nextConditional) {
        emit_jump(ctx, patches, "jmp", node->nextConditional);
    }
}

void printSubprogramImage(const SubprogramImage* image, const char* entry_label, FILE* out)
{
    if (!out) {
        out = stdout;
    }

    if (!image) {
        fprintf(out, "<null SubprogramImage>\n");
        return;
    }

    int count = image->instruction_count;
    char* entry = sanitize_label(entry_label);
    if (count <= 0) {
        fprintf(out, "%s:\n", entry);
        free(entry);
        return;
    }

    bool* skip_jump = calloc(count, sizeof(bool));
    bool* label_needed = calloc(count, sizeof(bool));
    char** label_names = calloc(count, sizeof(char*));

    if (!skip_jump || !label_needed || !label_names) {
        fprintf(out, "%s:\n", entry);
        for (int i = 0; i < count; i++) {
            const Instruction* instr = &image->instructions[i];
            if (instr && equals_ignore_case(instr->mnemonic, PSEUDO_LABEL_MNEMONIC) && instr->operand_count >= 1) {
                fprintf(out, "%s:\n", instr->operands[0] ? instr->operands[0] : "");
                continue;
            }
            fprintf(out, "    %s", instr->mnemonic ? instr->mnemonic : "");
            for (int op = 0; op < instr->operand_count; op++) {
                fprintf(out, " %s", instr->operands[op] ? instr->operands[op] : "");
            }
            fprintf(out, "\n");
        }
        free(skip_jump);
        free(label_needed);
        free(label_names);
        free(entry);
        return;
    }

    for (int i = 0; i < count; i++) {
        const Instruction* instr = &image->instructions[i];
        if (!instr || !equals_ignore_case(instr->mnemonic, "jmp") || instr->operand_count != 1) {
            continue;
        }

        int target = 0;
        if (parse_index_operand(instr->operands[0], &target) && target == i + 1) {
            skip_jump[i] = true;
        }
    }

    for (int i = 0; i < count; i++) {
        const Instruction* instr = &image->instructions[i];
        if (!instr || skip_jump[i]) {
            continue;
        }

        if (is_jump_mnemonic(instr->mnemonic) && instr->operand_count > 0) {
            int target = 0;
            if (parse_index_operand(instr->operands[0], &target) && target >= 0 && target < count) {
                label_needed[target] = true;
            }
        }
    }

    label_names[0] = entry;
    int label_index = 1;
    for (int i = 0; i < count; i++) {
        if (!label_needed[i]) {
            continue;
        }

        if (i == 0) {
            continue;
        }

        if (!label_names[i]) {
            char buffer[96];
            snprintf(buffer, sizeof(buffer), "%s_L%d", entry, label_index++);
            label_names[i] = strdup(buffer);
        }
    }

    fprintf(out, "%s:\n", entry);
    for (int i = 0; i < count; i++) {
        if (i != 0 && label_names[i]) {
            fprintf(out, "%s:\n", label_names[i]);
        }

        if (skip_jump[i]) {
            continue;
        }

        const Instruction* instr = &image->instructions[i];
        if (instr && equals_ignore_case(instr->mnemonic, PSEUDO_LABEL_MNEMONIC) && instr->operand_count >= 1) {
            fprintf(out, "%s:\n", instr->operands[0] ? instr->operands[0] : "");
            continue;
        }
        fprintf(out, "    %s", instr->mnemonic ? instr->mnemonic : "");
        for (int op = 0; op < instr->operand_count; op++) {
            const char* operand = instr->operands[op] ? instr->operands[op] : "";
            if (op == 0 && is_jump_mnemonic(instr->mnemonic)) {
                int target = 0;
                if (parse_index_operand(operand, &target) && target >= 0 && target < count && label_names[target]) {
                    operand = label_names[target];
                }
            }
            fprintf(out, " %s", operand);
        }
        fprintf(out, "\n");
    }

    for (int i = 0; i < count; i++) {
        if (label_names[i]) {
            free(label_names[i]);
        }
    }

    free(skip_jump);
    free(label_needed);
    free(label_names);
}

void printSubprogramImageConsole(const SubprogramImage* image, const char* entry_label)
{
    printSubprogramImage(image, entry_label, stdout);
}

void freeSubprogramImage(SubprogramImage* image)
{
    if (!image) {
        return;
    }

    for (int i = 0; i < image->instruction_count; i++) {
        Instruction* instr = &image->instructions[i];
        free(instr->mnemonic);
        for (int op = 0; op < instr->operand_count; op++) {
            free(instr->operands ? instr->operands[op] : NULL);
        }
        free(instr->operands);
    }
    free(image->instructions);

    for (int i = 0; i < image->data_item_count; i++) {
        if (image->data_items[i].kind == DATA_ITEM_LITERAL) {
            free(image->data_items[i].value.literal_name);
        }
    }
    free(image->data_items);

    free(image);
}

static SubprogramImage* toAsmModuleInternal(const SubprogramInfo* info,
                                            const SubprogramCollection* subprograms,
                                            ReturnSiteList* return_sites,
                                            bool is_main_method,
                                            bool halt_if_true_branch,
                                            char** error_message)
{
    if (error_message) {
        *error_message = NULL;
    }

    if (!info || !info->cfg) {
        if (error_message) {
            *error_message = strdup("Invalid subprogram for ASM generation.");
        }
        return NULL;
    }

    CodegenContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.info = info;
    ctx.subprograms = subprograms;
    ctx.return_sites = return_sites;
    ctx.is_main_method = is_main_method;
    ctx.method_returns_value = subprogram_returns_value(info);
    ctx.halt_if_true_branch = halt_if_true_branch;
    ctx.has_error = false;
    ctx.error_message[0] = '\0';
    instruction_list_init(&ctx.instructions);
    data_item_list_init(&ctx.data_items);

    if (ctx.method_returns_value && !is_builtin_type_name(info->return_type)) {
        if (error_message) {
            *error_message = strdup("Custom return types are not supported by the ASM backend.");
        }
        return NULL;
    }

    if (info->owner_type_name) {
        append_flattened_slots(&ctx, "this", info->owner_type_name);
    }
    for (int i = 0; i < info->param_count; i++) {
        append_flattened_slots(&ctx,
                               info->param_names && info->param_names[i] ? info->param_names[i] : "arg",
                               info->param_types && info->param_types[i] ? info->param_types[i] : "int");
    }
    for (int i = 0; i < info->local_count; i++) {
        append_flattened_slots(&ctx,
                               info->local_names && info->local_names[i] ? info->local_names[i] : "local",
                               info->local_types && info->local_types[i] ? info->local_types[i] : "int");
    }

    ControlFlowGraph* cfg = info->cfg;
    NodeEntry* entries = malloc(sizeof(NodeEntry) * cfg->node_count);
    if (!entries) {
        if (error_message) {
            *error_message = strdup("Out of memory while allocating CFG node table.");
        }
        free(ctx.var_names);
        free(ctx.var_types);
        free(ctx.data_items.items);
        return NULL;
    }
    JumpPatchList patches;
    jump_patch_list_init(&patches);

    for (int i = 0; i < cfg->node_count; i++) {
        entries[i].node = cfg->nodes[i];
        entries[i].start_index = ctx.instructions.count;
        emit_node(&ctx, cfg->nodes[i], &patches);
        if (ctx.has_error) {
            break;
        }
    }

    if (ctx.has_error) {
        if (error_message) {
            *error_message = strdup(ctx.error_message[0] ? ctx.error_message : "ASM generation failed.");
        }
        free(entries);
        free(patches.items);
        for (int i = 0; i < ctx.var_count; i++) {
            free((void*)ctx.var_names[i]);
            free((void*)ctx.var_types[i]);
        }
        free(ctx.var_names);
        free(ctx.var_types);
        for (int i = 0; i < ctx.instructions.count; i++) {
            free(ctx.instructions.items[i].mnemonic);
            for (int op = 0; op < ctx.instructions.items[i].operand_count; op++) {
                free(ctx.instructions.items[i].operands ? ctx.instructions.items[i].operands[op] : NULL);
            }
            free(ctx.instructions.items[i].operands);
        }
        free(ctx.instructions.items);
        free(ctx.data_items.items);
        return NULL;
    }

    for (int i = 0; i < patches.count; i++) {
        JumpPatch* patch = &patches.items[i];
        int target_index = find_node_start(entries, cfg->node_count, patch->target);
        if (target_index < 0) {
            target_index = 0;
        }

        Instruction* instr = &ctx.instructions.items[patch->instr_index];
        if (patch->operand_index < instr->operand_count) {
            free(instr->operands[patch->operand_index]);
            instr->operands[patch->operand_index] = format_int(target_index);
        }
    }

    SubprogramImage* image = malloc(sizeof(SubprogramImage));
    image->data_items = ctx.data_items.items;
    image->data_item_count = ctx.data_items.count;
    image->instructions = ctx.instructions.items;
    image->instruction_count = ctx.instructions.count;

    free(entries);
    free(patches.items);
    for (int i = 0; i < ctx.var_count; i++) {
        free((void*)ctx.var_names[i]);
        free((void*)ctx.var_types[i]);
    }
    free(ctx.var_names);
    free(ctx.var_types);

    return image;
}

SubprogramImage* toAsmModule(const SubprogramInfo* info)
{
    SubprogramCollection subprograms;
    subprograms.items = (SubprogramInfo*)info;
    subprograms.count = info ? 1 : 0;
    subprograms.user_types = NULL;
    subprograms.user_type_count = 0;
    subprograms.errors = NULL;
    subprograms.error_count = 0;
    ReturnSiteList return_sites;
    return_site_list_init(&return_sites);
    SubprogramImage* image = toAsmModuleInternal(info, &subprograms, &return_sites, true, false, NULL);
    return_site_list_free(&return_sites);
    return image;
}

static bool is_method_builtin_declaration(const SubprogramInfo* info)
{
    return is_read_builtin(info) || is_write_builtin(info);
}

static const SubprogramInfo* find_main_method(const SubprogramCollection* subprograms)
{
    if (!subprograms) {
        return NULL;
    }

    for (int i = 0; i < subprograms->count; i++) {
        const SubprogramInfo* info = &subprograms->items[i];
        if (!info || !info->name) {
            continue;
        }

        if (!info->owner_type_name && strcmp(info->name, "main") == 0 && info->param_count == 0) {
            return info;
        }
    }

    return NULL;
}

static void print_metadata_label(FILE* out, const char* raw_text)
{
    if (!out || !raw_text) {
        return;
    }

    char* label = sanitize_label(raw_text);
    if (!label) {
        return;
    }

    fprintf(out, "%s:\n", label);
    free(label);
}

static void printTypeMetadata(const SubprogramCollection* subprograms, FILE* out)
{
    if (!subprograms || !out || subprograms->user_type_count <= 0) {
        return;
    }

    fprintf(out, "[section TYPE_INFO]\n");
    for (int i = 0; i < subprograms->user_type_count; i++) {
        const UserTypeInfo* type_info = &subprograms->user_types[i];
        const char* kind = type_info->kind == USER_TYPE_INTERFACE ? "interface" : "class";
        char type_buffer[512];
        snprintf(type_buffer, sizeof(type_buffer),
                 "TYPEINFO_type_%s_%s_size_%d",
                 kind,
                 type_info->name ? type_info->name : "unnamed",
                 type_info->total_size_bytes);
        print_metadata_label(out, type_buffer);
        for (int j = 0; j < type_info->resolved_field_count; j++) {
            const FieldInfo* field = &type_info->resolved_fields[j];
            char field_buffer[512];
            snprintf(field_buffer, sizeof(field_buffer),
                     "TYPEINFO_field_%s_%s_%s_offset_%d",
                     type_info->name ? type_info->name : "unnamed",
                     field->name ? field->name : "field",
                     field->type_name ? field->type_name : "type",
                     field->offset_bytes);
            print_metadata_label(out, field_buffer);
        }
        for (int j = 0; j < type_info->interface_count; j++) {
            char implements_buffer[512];
            snprintf(implements_buffer, sizeof(implements_buffer),
                     "TYPEINFO_implements_%s_%s",
                     type_info->name ? type_info->name : "unnamed",
                     type_info->interface_names && type_info->interface_names[j] ? type_info->interface_names[j] : "interface");
            print_metadata_label(out, implements_buffer);
        }
    }
    fprintf(out, "\n");
}

bool generateProgramAsm(const SubprogramCollection* subprograms, FILE* out, char** error_message)
{
    if (error_message) {
        *error_message = NULL;
    }

    if (!out) {
        out = stdout;
    }

    if (!subprograms) {
        if (error_message) {
            *error_message = strdup("Subprogram collection is null.");
        }
        return false;
    }

    const SubprogramInfo* main_method = find_main_method(subprograms);
    if (!main_method) {
        printTypeMetadata(subprograms, out);
        fprintf(out, "[section CODE_CONST]\n");
        fprintf(out, "halt\n");
        return true;
    }

    ReturnSiteList return_sites;
    return_site_list_init(&return_sites);

    SubprogramImage** images = calloc(subprograms->count, sizeof(SubprogramImage*));
    if (!images) {
        if (error_message) {
            *error_message = strdup("Out of memory while preparing ASM images.");
        }
        return_site_list_free(&return_sites);
        return false;
    }

    for (int i = 0; i < subprograms->count; i++) {
        const SubprogramInfo* info = &subprograms->items[i];
        if (!info || !info->name || is_method_builtin_declaration(info)
            || info->import_info.is_imported || !info->has_body) {
            continue;
        }

        bool is_main = (info == main_method);
        char* local_error = NULL;
        images[i] = toAsmModuleInternal(info, subprograms, &return_sites, is_main, false, &local_error);
        if (!images[i]) {
            if (error_message) {
                if (local_error) {
                    *error_message = local_error;
                } else {
                    *error_message = strdup("Failed to generate ASM image.");
                }
            } else {
                free(local_error);
            }

            for (int j = 0; j < subprograms->count; j++) {
                freeSubprogramImage(images[j]);
            }
            free(images);
            return_site_list_free(&return_sites);
            return false;
        }
    }

    printTypeMetadata(subprograms, out);
    fprintf(out, "[section CODE_CONST]\n");
    fprintf(out, "\n");

    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < subprograms->count; i++) {
            const SubprogramInfo* info = &subprograms->items[i];
            if (!info || !info->name || is_method_builtin_declaration(info)
                || info->import_info.is_imported || !info->has_body) {
                continue;
            }

            bool is_main = (info == main_method);
            if ((pass == 0 && !is_main) || (pass == 1 && is_main)) {
                continue;
            }

            if (!images[i]) {
                continue;
            }

            printSubprogramImage(images[i], info->asm_name ? info->asm_name : info->name, out);
            fprintf(out, "\n");
        }
    }

    if (return_sites.count > 0) {
        fprintf(out, "%s:\n", RUNTIME_DISPATCH_LABEL);
        for (int i = 0; i < return_sites.count; i++) {
            const ReturnSite* site = &return_sites.items[i];
            fprintf(out, "    dup\n");
            fprintf(out, "    pushi %d\n", site->id);
            fprintf(out, "    eq\n");
            fprintf(out, "    jnz M_sys_ret_case_%d\n", site->id);
        }
        fprintf(out, "    pop\n");
        fprintf(out, "    halt\n");

        for (int i = 0; i < return_sites.count; i++) {
            const ReturnSite* site = &return_sites.items[i];
            fprintf(out, "M_sys_ret_case_%d:\n", site->id);
            fprintf(out, "    pop\n");
            fprintf(out, "    jmp %s\n", site->continue_label ? site->continue_label : "");
        }
        fprintf(out, "\n");
    }

    for (int i = 0; i < subprograms->count; i++) {
        freeSubprogramImage(images[i]);
    }
    free(images);
    return_site_list_free(&return_sites);

    return true;
}
