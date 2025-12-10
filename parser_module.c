#include "parser_module.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Custom ANTLR error collector
static void collectError(pANTLR3_BASE_RECOGNIZER recognizer,
                         pANTLR3_UINT8* tokenNames)
{
    pANTLR3_EXCEPTION ex = recognizer->state->exception;
    recognizer->state->errorCount++;

    const char* msg = (const char*)ex->message;

    char*** listPtr = (char***)&recognizer->state->userp;
    char** list = *listPtr;

    int index = recognizer->state->errorCount - 1;

    list[index] = malloc(strlen(msg) + 1);
    strcpy(list[index], msg);
}

ParseResult parseFile(const char* filename)
{
    ParseResult result;
    result.tree = NULL;
    result.errorCount = 0;
    result.errors = calloc(128, sizeof(char*));

    result.parser = NULL;
    result.tokens = NULL;
    result.lexer = NULL;
    result.input = NULL;

    // Создаем ANTLR file stream
    pANTLR3_INPUT_STREAM input = antlr3FileStreamNew(
        (pANTLR3_UINT8)filename,
        ANTLR3_ENC_8BIT
    );
    if (!input) {
        result.errorCount = 1;
        result.errors[0] = strdup("Failed to open input file.");
        return result;
    }

    // Лексер
    pGrammarLexer lexer = GrammarLexerNew(input);

    // Токены
    pANTLR3_COMMON_TOKEN_STREAM tokens =
        antlr3CommonTokenStreamSourceNew(ANTLR3_SIZE_HINT, TOKENSOURCE(lexer));

    // Парсер
    pGrammarParser parser = GrammarParserNew(tokens);

    // Error listener
    parser->pParser->rec->displayRecognitionError = collectError;
    parser->pParser->rec->state->userp = result.errors;

    // Парсим
    GrammarParser_source_return ret = parser->source(parser);
    result.tree = ret.tree;
    result.errorCount = parser->pParser->rec->state->errorCount;

    // Сохраняем объекты для очистки в main
    result.parser = parser;
    result.tokens = tokens;
    result.lexer = lexer;
    result.input = input;

    return result;
}

void freeParseResult(ParseResult* result)
{
    for (int i = 0; i < result->errorCount; i++)
        free(result->errors[i]);
    free(result->errors);

    if (result->parser)
        result->parser->free(result->parser);
    if (result->tokens)
        result->tokens->free(result->tokens);
    if (result->lexer)
        result->lexer->free(result->lexer);
    if (result->input)
        result->input->close(result->input);
}


// Рекурсивный вывод дерева
void printTree(pANTLR3_BASE_TREE tree, int indent) {
    if (!tree) return;

    for (int i = 0; i < indent; i++)
        printf("  ");

    pANTLR3_STRING text = tree->getText(tree);
    printf("%s\n", text->chars);

    ANTLR3_UINT32 childCount = tree->getChildCount(tree);
    for (ANTLR3_UINT32 i = 0; i < childCount; i++) {
        pANTLR3_BASE_TREE child = (pANTLR3_BASE_TREE)tree->getChild(tree, i);
        printTree(child, indent + 1);
    }
}

void treeToDot(pANTLR3_BASE_TREE tree, FILE* out, int* nodeId) {
    if (!tree) return;

    int currentId = (*nodeId)++;
    pANTLR3_STRING text = tree->getText(tree);
    fprintf(out, "  node%d [label=\"%s\"];\n", currentId, text->chars);

    ANTLR3_UINT32 childCount = tree->getChildCount(tree);
    for (ANTLR3_UINT32 i = 0; i < childCount; i++) {
        int childId = *nodeId;
        pANTLR3_BASE_TREE child = (pANTLR3_BASE_TREE)tree->getChild(tree, i);

        treeToDot(child, out, nodeId);
        fprintf(out, "  node%d -> node%d;\n", currentId, childId);
    }
}
