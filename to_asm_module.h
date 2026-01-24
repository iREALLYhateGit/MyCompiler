#ifndef TO_ASM_MODULE_H
#define TO_ASM_MODULE_H

#include "cfg_builder_module.h"

typedef enum {
    DATA_ITEM_LITERAL,
    DATA_ITEM_TYPE_SIZE
} DataItemKind;

typedef struct {
    DataItemKind kind;
    union {
        char* literal_name;
        int size_bytes;
    } value;
} DataItem;

typedef struct {
    char* mnemonic;
    char** operands;
    int operand_count;
} Instruction;

typedef struct {
    DataItem* data_items;
    int data_item_count;
    Instruction* instructions;
    int instruction_count;
} SubprogramImage;

SubprogramImage* toAsmModule(const SubprogramInfo* info);
void printSubprogramImage(const SubprogramImage* image, const char* entry_label, FILE* out);
void printSubprogramImageConsole(const SubprogramImage* image, const char* entry_label);

#endif
