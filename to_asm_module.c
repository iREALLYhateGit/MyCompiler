#include "to_asm_module.h"

#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    const SubprogramInfo* info;
    InstructionList instructions;
    DataItemList data_items;
    const char** var_names;
    int var_count;
} CodegenContext;

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

static int type_size_bytes(const char* type)
{
    if (!type) {
        return 4;
    }

    if (equals_ignore_case(type, "bool")) {
        return 1;
    }
    if (equals_ignore_case(type, "byte")) {
        return 1;
    }
    if (equals_ignore_case(type, "char")) {
        return 1;
    }
    if (equals_ignore_case(type, "long") || equals_ignore_case(type, "ulong")) {
        return 8;
    }

    return 4;
}

static int find_var_index(const CodegenContext* ctx, const char* name)
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

static int emit_instruction(CodegenContext* ctx, const char* mnemonic, int operand_count, const char** operands)
{
    return instruction_list_add(&ctx->instructions, mnemonic, operand_count, operands);
}

static int emit_instruction1(CodegenContext* ctx, const char* mnemonic, const char* operand)
{
    const char* ops[1] = { operand };
    return emit_instruction(ctx, mnemonic, 1, ops);
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

static bool emit_function_call(CodegenContext* ctx, const OpNode* node)
{
    if (!node || !node->text) {
        return false;
    }

    if (strcmp(node->text, "out") == 0) {
        if (node->operand_count == 0) {
            emit_instruction(ctx, "out", 0, NULL);
        } else {
            for (int i = 0; i < node->operand_count; i++) {
                emit_expression(ctx, node->operands[i]);
                emit_instruction(ctx, "out", 0, NULL);
            }
        }
        return false;
    }

    if (strcmp(node->text, "in") == 0) {
        emit_instruction(ctx, "in", 0, NULL);
        return true;
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

    for (int i = 0; i < node->operand_count; i++) {
        bool has_value = emit_expression(ctx, node->operands[i]);
        if (has_value) {
            emit_instruction(ctx, "pop", 0, NULL);
        }
    }

    return false;
}

static bool emit_expression(CodegenContext* ctx, const OpNode* node)
{
    if (!ctx || !node) {
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
            int index = find_var_index(ctx, node->text);
            if (index >= 0) {
                char* operand = format_int(index);
                emit_instruction1(ctx, "ldg", operand);
                free(operand);
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
                    int index = find_var_index(ctx, target->text);
                    if (index >= 0) {
                        char* operand = format_int(index);
                        emit_instruction1(ctx, "stg", operand);
                        free(operand);
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
    if (!node) {
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
    if (!ctx || !node) {
        return;
    }

    bool is_conditional = node->type == NODE_IF
        || node->type == NODE_WHILE
        || node->type == NODE_REPEAT_CONDITION;

    if (node->type == NODE_EXIT) {
        emit_instruction(ctx, "halt", 0, NULL);
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

    for (int i = 0; i < node->stmt_count; i++) {
        emit_statement(ctx, node->statements[i]);
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
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "L%d", label_index++);
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

SubprogramImage* toAsmModule(const SubprogramInfo* info)
{
    if (!info || !info->cfg) {
        return NULL;
    }

    CodegenContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.info = info;
    instruction_list_init(&ctx.instructions);
    data_item_list_init(&ctx.data_items);

    ctx.var_count = info->param_count + info->local_count;
    if (ctx.var_count > 0) {
        ctx.var_names = malloc(sizeof(char*) * ctx.var_count);
    }

    int var_index = 0;
    for (int i = 0; i < info->param_count; i++) {
        ctx.var_names[var_index++] = info->param_names[i];
        data_item_list_add_size(&ctx.data_items, type_size_bytes(info->param_types[i]));
    }

    for (int i = 0; i < info->local_count; i++) {
        ctx.var_names[var_index++] = info->local_names[i];
        data_item_list_add_size(&ctx.data_items, type_size_bytes(info->local_types[i]));
    }

    ControlFlowGraph* cfg = info->cfg;
    NodeEntry* entries = malloc(sizeof(NodeEntry) * cfg->node_count);
    JumpPatchList patches;
    jump_patch_list_init(&patches);

    for (int i = 0; i < cfg->node_count; i++) {
        entries[i].node = cfg->nodes[i];
        entries[i].start_index = ctx.instructions.count;
        emit_node(&ctx, cfg->nodes[i], &patches);
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
    free(ctx.var_names);

    return image;
}
