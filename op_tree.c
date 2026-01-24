#include "op_tree.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char* data;
    size_t length;
    size_t capacity;
} StringBuilder;

static void sbInit(StringBuilder* sb)
{
    sb->capacity = 64;
    sb->length = 0;
    sb->data = malloc(sb->capacity);
    if (sb->data) {
        sb->data[0] = '\0';
    }
}

static void sbEnsure(StringBuilder* sb, size_t extra)
{
    if (!sb->data) {
        return;
    }

    if (sb->length + extra + 1 <= sb->capacity) {
        return;
    }

    size_t new_capacity = sb->capacity * 2;
    while (new_capacity < sb->length + extra + 1) {
        new_capacity *= 2;
    }

    char* new_data = realloc(sb->data, new_capacity);
    if (!new_data) {
        return;
    }

    sb->data = new_data;
    sb->capacity = new_capacity;
}

static void sbAppend(StringBuilder* sb, const char* text)
{
    if (!sb->data || !text) {
        return;
    }

    size_t len = strlen(text);
    sbEnsure(sb, len);
    if (!sb->data) {
        return;
    }

    memcpy(sb->data + sb->length, text, len);
    sb->length += len;
    sb->data[sb->length] = '\0';
}

static const char* getNodeText(pANTLR3_BASE_TREE node)
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

static OpNode* createOpNode(OpType type)
{
    OpNode* node = malloc(sizeof(OpNode));
    if (!node) {
        return NULL;
    }

    node->type = type;
    node->operands = NULL;
    node->operand_count = 0;
    node->text = NULL;

    return node;
}

static void addOperand(OpNode* node, OpNode* operand)
{
    if (!node) {
        return;
    }

    OpNode** new_operands = realloc(node->operands, sizeof(OpNode*) * (node->operand_count + 1));
    if (!new_operands) {
        return;
    }

    node->operands = new_operands;
    node->operands[node->operand_count++] = operand;
}

static bool isSameAssociativeGroup(OpType left, OpType right)
{
    if (left == OP_ADDITION || left == OP_SUBTRACTION) {
        return right == OP_ADDITION || right == OP_SUBTRACTION;
    }

    if (left == OP_MULTIPLICATION || left == OP_DIVISION || left == OP_MODULO) {
        return right == OP_MULTIPLICATION || right == OP_DIVISION || right == OP_MODULO;
    }

    if (left == OP_LOGICAL_AND) {
        return right == OP_LOGICAL_AND;
    }

    if (left == OP_LOGICAL_OR) {
        return right == OP_LOGICAL_OR;
    }

    if (left == OP_EQUAL || left == OP_NOT_EQUAL) {
        return right == OP_EQUAL || right == OP_NOT_EQUAL;
    }

    if (left == OP_LESS_THAN || left == OP_LESS_THAN_OR_EQUAL
        || left == OP_GREATER_THAN || left == OP_GREATER_THAN_OR_EQUAL) {
        return right == OP_LESS_THAN || right == OP_LESS_THAN_OR_EQUAL
            || right == OP_GREATER_THAN || right == OP_GREATER_THAN_OR_EQUAL;
    }

    return false;
}

static OpNode* leftAssociateBinary(OpNode* node)
{
    if (!node || node->operand_count != 2) {
        return node;
    }

    OpNode* current = node;
    while (current && current->operand_count == 2) {
        OpNode* right = current->operands[1];
        if (!right || right->operand_count != 2) {
            break;
        }

        if (!isSameAssociativeGroup(current->type, right->type)) {
            break;
        }

        OpNode* middle = right->operands[0];
        right->operands[0] = current;
        current->operands[1] = middle;
        current = right;
    }

    return current;
}

static bool isWrapperToken(const char* text)
{
    return strcmp(text, "EXPRESSION") == 0
        || strcmp(text, "CONDITION") == 0
        || strcmp(text, "UNTIL") == 0
        || strcmp(text, "IN_BRACES") == 0
        || strcmp(text, "VALUE") == 0
        || strcmp(text, "ARRAY_ELEMENT_INDEX") == 0;
}

static OpNode* buildOpNodeWithChildren(OpType type, pANTLR3_BASE_TREE node)
{
    OpNode* op_node = createOpNode(type);
    if (!op_node || !node) {
        return op_node;
    }

    ANTLR3_UINT32 child_count = node->getChildCount(node);
    for (ANTLR3_UINT32 i = 0; i < child_count; i++) {
        addOperand(op_node, buildOpTree(node->getChild(node, i)));
    }

    return op_node;
}

static const char* getIdentifierName(pANTLR3_BASE_TREE node)
{
    if (!node) {
        return "";
    }

    if (node->getChildCount(node) > 0) {
        return getNodeText(node->getChild(node, 0));
    }

    return getNodeText(node);
}

const char* opTypeToString(OpType type)
{
    switch (type) {
        case OP_ASSIGNMENT: return "ASSIGN";
        case OP_ADDITION: return "ADD";
        case OP_SUBTRACTION: return "SUBTRACTION";
        case OP_MULTIPLICATION: return "MULTIPLICATION";
        case OP_DIVISION: return "DIVISION";
        case OP_MODULO: return "MODULO";
        case OP_LOGICAL_AND: return "LOGICAL_AND";
        case OP_LOGICAL_OR: return "LOGICAL_OR";
        case OP_EQUAL: return "EQUAL";
        case OP_NOT_EQUAL: return "NOT_EQUAL";
        case OP_LESS_THAN: return "LESS_THAN";
        case OP_LESS_THAN_OR_EQUAL: return "LESS_THAN_OR_EQUAL";
        case OP_GREATER_THAN: return "GREATER_THAN";
        case OP_GREATER_THAN_OR_EQUAL: return "GREATER_THAN_OR_EQUAL";
        case OP_UNARY_PLUS: return "UNARY_PLUS";
        case OP_UNARY_MINUS: return "UNARY_MINUS";
        case OP_LOGICAL_NOT: return "LOGICAL_NOT";
        case OP_FUNCTION_CALL: return "CALL";
        case OP_ARRAY_INDEX: return "ARRAY_INDEX";
        case OP_IDENTIFIER: return "IDENTIFIER";
        case OP_LITERAL: return "LITERAL";
        case OP_UNKNOWN: return "UNKNOWN";
        default: return "UNKNOWN";
    }
}

static OpNode* buildUnaryOp(pANTLR3_BASE_TREE node)
{
    if (!node || node->getChildCount(node) < 2) {
        return NULL;
    }

    pANTLR3_BASE_TREE op_token = node->getChild(node, 0);
    const char* op_text = getNodeText(op_token);

    OpType type = OP_UNKNOWN;
    if (strcmp(op_text, "+") == 0) {
        type = OP_UNARY_PLUS;
    } else if (strcmp(op_text, "-") == 0) {
        type = OP_UNARY_MINUS;
    } else if (strcmp(op_text, "!") == 0) {
        type = OP_LOGICAL_NOT;
    }

    OpNode* op_node = createOpNode(type);
    addOperand(op_node, buildOpTree(node->getChild(node, 1)));

    return op_node;
}

static OpNode* buildCallOp(pANTLR3_BASE_TREE node)
{
    if (!node) {
        return NULL;
    }

    OpNode* op_node = createOpNode(OP_FUNCTION_CALL);
    if (!op_node) {
        return NULL;
    }

    if (node->getChildCount(node) > 0) {
        pANTLR3_BASE_TREE id_node = node->getChild(node, 0);
        const char* name = getIdentifierName(id_node);
        op_node->text = strdup(name);
    }

    if (node->getChildCount(node) > 1) {
        pANTLR3_BASE_TREE args_node = node->getChild(node, 1);
        const char* args_text = getNodeText(args_node);

        if (strcmp(args_text, "ARGUMENTS") == 0) {
            ANTLR3_UINT32 arg_count = args_node->getChildCount(args_node);
            for (ANTLR3_UINT32 i = 0; i < arg_count; i++) {
                addOperand(op_node, buildOpTree(args_node->getChild(args_node, i)));
            }
        } else {
            addOperand(op_node, buildOpTree(args_node));
        }
    }

    return op_node;
}

static OpNode* buildArrayIndexOp(pANTLR3_BASE_TREE node)
{
    if (!node) {
        return NULL;
    }

    OpNode* op_node = createOpNode(OP_ARRAY_INDEX);
    if (!op_node) {
        return NULL;
    }

    ANTLR3_UINT32 child_count = node->getChildCount(node);
    for (ANTLR3_UINT32 i = 0; i < child_count; i++) {
        addOperand(op_node, buildOpTree(node->getChild(node, i)));
    }

    return op_node;
}

OpNode* buildOpTree(pANTLR3_BASE_TREE node)
{
    if (!node) {
        return NULL;
    }

    const char* text = getNodeText(node);

    if (isWrapperToken(text)) {
        if (node->getChildCount(node) > 0) {
            return buildOpTree(node->getChild(node, 0));
        }
        return NULL;
    }

    if (strcmp(text, "ID") == 0 || strcmp(text, "ARRAY_ID") == 0) {
        OpNode* op_node = createOpNode(OP_IDENTIFIER);
        const char* name = getIdentifierName(node);
        op_node->text = strdup(name);
        return op_node;
    }

    if (strcmp(text, "ASSIGN") == 0) {
        return buildOpNodeWithChildren(OP_ASSIGNMENT, node);
    }

    if (strcmp(text, "ADD") == 0) {
        return leftAssociateBinary(buildOpNodeWithChildren(OP_ADDITION, node));
    }
    if (strcmp(text, "SUBTRACT") == 0) {
        return leftAssociateBinary(buildOpNodeWithChildren(OP_SUBTRACTION, node));
    }
    if (strcmp(text, "MULTIPLY") == 0) {
        return leftAssociateBinary(buildOpNodeWithChildren(OP_MULTIPLICATION, node));
    }
    if (strcmp(text, "DIVISION") == 0) {
        return leftAssociateBinary(buildOpNodeWithChildren(OP_DIVISION, node));
    }
    if (strcmp(text, "RESIDUE") == 0) {
        return leftAssociateBinary(buildOpNodeWithChildren(OP_MODULO, node));
    }

    if (strcmp(text, "AND") == 0) {
        return leftAssociateBinary(buildOpNodeWithChildren(OP_LOGICAL_AND, node));
    }
    if (strcmp(text, "OR") == 0) {
        return leftAssociateBinary(buildOpNodeWithChildren(OP_LOGICAL_OR, node));
    }

    if (strcmp(text, "EQUALS") == 0) {
        return leftAssociateBinary(buildOpNodeWithChildren(OP_EQUAL, node));
    }
    if (strcmp(text, "NOT_EQUALS") == 0) {
        return leftAssociateBinary(buildOpNodeWithChildren(OP_NOT_EQUAL, node));
    }
    if (strcmp(text, "LESS_THAN") == 0) {
        return leftAssociateBinary(buildOpNodeWithChildren(OP_LESS_THAN, node));
    }
    if (strcmp(text, "LESS_THAN_OR_EQUALS") == 0) {
        return leftAssociateBinary(buildOpNodeWithChildren(OP_LESS_THAN_OR_EQUAL, node));
    }
    if (strcmp(text, "MORE_THAN") == 0) {
        return leftAssociateBinary(buildOpNodeWithChildren(OP_GREATER_THAN, node));
    }
    if (strcmp(text, "MORE_THAN_OR_EQUALS") == 0) {
        return leftAssociateBinary(buildOpNodeWithChildren(OP_GREATER_THAN_OR_EQUAL, node));
    }

    if (strcmp(text, "UNARY_OPERATION") == 0) {
        return buildUnaryOp(node);
    }

    if (strcmp(text, "CALL") == 0) {
        return buildCallOp(node);
    }

    if (strcmp(text, "ARRAY_ELEMENT") == 0) {
        return buildArrayIndexOp(node);
    }

    if (node->getChildCount(node) == 0) {
        OpNode* op_node = createOpNode(OP_LITERAL);
        op_node->text = strdup(text);
        return op_node;
    }

    OpNode* op_node = buildOpNodeWithChildren(OP_UNKNOWN, node);
    op_node->text = strdup(text);
    return op_node;
}

void freeOpTree(OpNode* node)
{
    if (!node) {
        return;
    }

    for (int i = 0; i < node->operand_count; i++) {
        freeOpTree(node->operands[i]);
    }

    free(node->operands);
    free(node->text);
    free(node);
}

void printOpTree(const OpNode* node, int indent)
{
    if (!node) {
        return;
    }

    for (int i = 0; i < indent; i++) {
        printf("  ");
    }

    const char* type_str = opTypeToString(node->type);
    if (node->text && node->text[0] != '\0') {
        printf("%s: %s\n", type_str, node->text);
    } else {
        printf("%s\n", type_str);
    }

    for (int i = 0; i < node->operand_count; i++) {
        printOpTree(node->operands[i], indent + 1);
    }
}

static void fprintEscaped(FILE* out, const char* s)
{
    for (; *s; ++s) {
        if (*s == '\\') {
            fputs("\\\\", out);
        } else if (*s == '"') {
            fputs("\\\"", out);
        } else if (*s == '\n') {
            fputs("\\n", out);
        } else if (*s != '\r') {
            fputc(*s, out);
        }
    }
}

static void opNodeToDot(const OpNode* node, FILE* out, int* node_id)
{
    if (!node || !out || !node_id) {
        return;
    }

    int current_id = (*node_id)++;

    fprintf(out, "  node%d [label=\"", current_id);
    fprintEscaped(out, opTypeToString(node->type));
    if (node->text && node->text[0] != '\0') {
        fputs("\\n", out);
        fprintEscaped(out, node->text);
    }
    fprintf(out, "\"];\n");

    for (int i = 0; i < node->operand_count; i++) {
        int child_id = *node_id;
        opNodeToDot(node->operands[i], out, node_id);
        fprintf(out, "  node%d -> node%d;\n", current_id, child_id);
    }
}

void opTreeToDot(const OpNode* node, FILE* out)
{
    if (!out) {
        return;
    }

    fprintf(out, "digraph OpTree {\n");
    fprintf(out, "  node [shape=box];\n");

    int node_id = 0;
    opNodeToDot(node, out, &node_id);

    fprintf(out, "}\n");
}

static void opTreeToStringRec(const OpNode* node, StringBuilder* sb)
{
    if (!node) {
        sbAppend(sb, "<null>");
        return;
    }

    sbAppend(sb, opTypeToString(node->type));
    if (node->text && node->text[0] != '\0') {
        sbAppend(sb, ":");
        sbAppend(sb, node->text);
    }

    if (node->operand_count > 0) {
        sbAppend(sb, "(");
        for (int i = 0; i < node->operand_count; i++) {
            if (i > 0) {
                sbAppend(sb, ", ");
            }
            opTreeToStringRec(node->operands[i], sb);
        }
        sbAppend(sb, ")");
    }
}

char* opTreeToString(const OpNode* node)
{
    StringBuilder sb;
    sbInit(&sb);
    if (!sb.data) {
        return strdup("<null>");
    }
    opTreeToStringRec(node, &sb);
    return sb.data;
}
