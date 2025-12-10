#ifndef PARSER_MODULE_H
#define PARSER_MODULE_H

#include <antlr3.h>
#include "GrammarLexer.h"
#include "GrammarParser.h"

typedef struct {
    pANTLR3_BASE_TREE tree;           // AST root
    char** errors;                    // array of error messages
    int errorCount;

    // ANTLR objects needed for proper cleanup
    pGrammarParser parser;
    pANTLR3_COMMON_TOKEN_STREAM tokens;
    pGrammarLexer lexer;
    pANTLR3_INPUT_STREAM input;
} ParseResult;

ParseResult parseFile(const char* filename);
void freeParseResult(ParseResult* result);
void printTree(pANTLR3_BASE_TREE tree, int indent);
void treeToDot(pANTLR3_BASE_TREE tree, FILE* out, int* nodeId);

#endif
