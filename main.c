#include <stdio.h>
#include "parser_module.h"

int main(int argc, char* argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input_file> <output_file>\n", argv[0]);
        return 1;
    }

    const ParseResult result = parseFile(argv[1]);

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

    const FILE* dotFile = fopen(argv[2], "w");
    if (!dotFile) {
        fprintf(stderr, "Cannot open output file\n");
        freeParseResult(&result);
        return 1;
    }

    fprintf(dotFile, "digraph AST {\n  node [shape=box];\n");
    int nodeId = 0;
    treeToDot(result.tree, dotFile, &nodeId);
    fprintf(dotFile, "}\n");
    fclose(dotFile);

    ControlFlowGraph* cfg = buildCFG(result.tree);

    for (int i = 0; i < cfg->node_count; i++) {
        printf("%d\n", cfg->nodes[i]->id);
        printf("%s\n", cfg->nodes[i]->label);
        for (int k = 0; k < cfg->nodes[i]->stmt_count; k++) {
            printf("  %s\n", cfg->nodes[i]->statements[k]->getText(cfg->nodes[i]->statements[k])->chars);
        }
    }

    freeParseResult(&result);

    printf("\nTree is stored into tree.dot\n");
    printf("To visualise execute: dot -Tpng tree.dot -o tree.png\n");
    return 0;
}
