#include <stdio.h>
#include "parser_module.h"

int main(int argc, char* argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input_file> <output_file>\n", argv[0]);
        return 1;
    }

    ParseResult result = parseFile(argv[1]);

    // Ошибки
    for (int i = 0; i < result.errorCount; i++)
        fprintf(stderr, "Error: %s\n", result.errors[i]);

    if (!result.tree) {
        fprintf(stderr, "Tree creation failed.\n");
        freeParseResult(&result);
        return 1;
    }

    printf("Syntactic tree:\n");
    printTree(result.tree, 0);

    FILE* out = fopen(argv[2], "w");
    if (!out) {
        fprintf(stderr, "Cannot open output file\n");
        freeParseResult(&result);
        return 1;
    }

    fprintf(out, "digraph AST {\n  node [shape=box];\n");
    int nodeId = 0;
    treeToDot(result.tree, out, &nodeId);
    fprintf(out, "}\n");
    fclose(out);

    freeParseResult(&result);

    printf("\nTree is stored into tree.dot\n");
    printf("To visualise execute: dot -Tpng tree.dot -o tree.png\n");

    return 0;
}
