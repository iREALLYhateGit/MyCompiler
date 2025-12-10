#include <stdio.h>
#include <antlr3.h>
#include "GrammarLexer.h"
#include "GrammarParser.h"

// Функция для рекурсивного вывода дерева
void printTree(pANTLR3_BASE_TREE tree, int indent) {
    if (tree == NULL) return;

    // Отступ для визуализации уровня вложенности
    for (int i = 0; i < indent; i++) {
        printf("  ");
    }

    // Получаем текст узла
    pANTLR3_STRING text = tree->getText(tree);
    printf("%s\n", text->chars);

    // Рекурсивно обрабатываем дочерние узлы
    ANTLR3_UINT32 childCount = tree->getChildCount(tree);
    for (ANTLR3_UINT32 i = 0; i < childCount; i++) {
        pANTLR3_BASE_TREE child = (pANTLR3_BASE_TREE)tree->getChild(tree, i);
        printTree(child, indent + 1);
    }
}

// Функция для вывода дерева в DOT формат (для Graphviz)
void treeToDoT(pANTLR3_BASE_TREE tree, FILE* out, int* nodeId) {
    if (tree == NULL) return;

    int currentId = (*nodeId)++;
    pANTLR3_STRING text = tree->getText(tree);

    // Экранируем специальные символы
    fprintf(out, "  node%d [label=\"%s\"];\n", currentId, text->chars);

    ANTLR3_UINT32 childCount = tree->getChildCount(tree);
    for (ANTLR3_UINT32 i = 0; i < childCount; i++) {
        int childId = *nodeId;
        pANTLR3_BASE_TREE child = (pANTLR3_BASE_TREE)tree->getChild(tree, i);

        treeToDoT(child, out, nodeId);
        fprintf(out, "  node%d -> node%d;\n", currentId, childId);
    }
}



int main(int argc, char* argv[]) {
    // Проверка аргументов командной строки
    if (argc < 2) {
        fprintf(stderr, "Использование: %s <входной_файл>\n", argv[0]);
        return 1;
    }

    // Открываем входной файл
    pANTLR3_INPUT_STREAM input = antlr3FileStreamNew(
        (pANTLR3_UINT8)argv[1],
        ANTLR3_ENC_8BIT
    );

    if (input == NULL) {
        fprintf(stderr, "Exception while opening file: %s\n", argv[1]);
        return 1;
    }

    // Создаем лексер
    pGrammarLexer lexer = GrammarLexerNew(input);

    // Создаем поток токенов
    pANTLR3_COMMON_TOKEN_STREAM tokens = antlr3CommonTokenStreamSourceNew(
        ANTLR3_SIZE_HINT,
        TOKENSOURCE(lexer)
    );

    // Создаем парсер
    pGrammarParser parser = GrammarParserNew(tokens);

    // Парсим входной файл (source - стартовое правило из грамматики)
    GrammarParser_source_return result = parser->source(parser);

    // Получаем дерево разбора
    pANTLR3_BASE_TREE tree = result.tree;

    // Проверяем на ошибки
    if (parser->pParser->rec->state->errorCount > 0) {
        fprintf(stderr, "Parser faced %d exceptions\n",
                parser->pParser->rec->state->errorCount);
        return 1;
    }

    // Выводим дерево в консоль
    printf("Syntactic tree:\n");
    printTree(tree, 0);

    // Сохраняем в DOT формат для визуализации
    FILE* dotFile = fopen("tree.dot", "w");
    if (dotFile != NULL) {
        fprintf(dotFile, "digraph AST {\n");
        fprintf(dotFile, "  node [shape=box];\n");

        int nodeId = 0;
        treeToDoT(tree, dotFile, &nodeId);

        fprintf(dotFile, "}\n");
        fclose(dotFile);
        printf("\nTree is stored into tree.dot\n");
        printf("To visualise execute: dot -Tpng tree.dot -o tree.png\n");
    }

    // Освобождаем ресурсы
    parser->free(parser);
    tokens->free(tokens);
    lexer->free(lexer);
    input->close(input);

    return 0;
}