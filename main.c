#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir _mkdir
#endif

#include "parser_module.h"
#include "cfg_builder_module.h"
#include "to_asm_module.h"

#define PATH_SEPARATOR '\\'

int create_directory(const char* path)
{
    return mkdir(path);
}

char* get_clean_filename(const char* fullpath)
{
    const char* filename = fullpath;

    const char* last_sep = strrchr(fullpath, PATH_SEPARATOR);
    const char* last_unix_sep = strrchr(fullpath, '/');
    if (last_unix_sep && (!last_sep || last_unix_sep > last_sep)) {
        last_sep = last_unix_sep;
    }

    if (last_sep) {
        filename = last_sep + 1;
    }

    char* name = strdup(filename);
    if (!name) {
        return NULL;
    }

    char* dot = strrchr(name, '.');
    if (dot) {
        *dot = '\0';
    }

    for (char* p = name; *p; p++) {
        if (*p == '.' || *p == '/' || *p == '\\' || *p == ':' ||
            *p == '*' || *p == '?' || *p == '"' || *p == '<' ||
            *p == '>' || *p == '|') {
            *p = '_';
        }
    }

    return name;
}

static char* sanitize_filename_component(const char* name)
{
    if (!name || name[0] == '\0') {
        return strdup("unnamed");
    }

    char* clean = strdup(name);
    if (!clean) {
        return strdup("unnamed");
    }

    for (char* p = clean; *p; p++) {
        if (*p == '.' || *p == '/' || *p == '\\' || *p == ':' ||
            *p == '*' || *p == '?' || *p == '"' || *p == '<' ||
            *p == '>' || *p == '|') {
            *p = '_';
        }
    }

    return clean;
}

int process_file(const char* input_file_path, const char* ast_dir, const char* cfg_dir)
{
    printf("\n=== Processing file: %s ===\n", input_file_path);

    ParseResult result = parseFile(input_file_path);
    char* base_name = NULL;
    SubprogramCollection subprograms = {0};
    CallGraph* call_graph = NULL;
    int success = 0;

    if (!result.tree) {
        fprintf(stderr, "AST tree creation failed due to some unexpected ERROR.\n");
        goto cleanup;
    }

    if (result.errorCount != 0) {
        printf("AST tree created with errors:\n");
        for (int i = 0; i < result.errorCount; i++) {
            fprintf(stderr, "Error: %s\n", result.errors[i]);
        }
        goto cleanup;
    }

    base_name = get_clean_filename(input_file_path);
    if (!base_name) {
        fprintf(stderr, "Failed to allocate base filename for '%s'.\n", input_file_path);
        goto cleanup;
    }

    char ast_path[1024];
    snprintf(ast_path, sizeof(ast_path), "%s%c%s_ast.dot", ast_dir, PATH_SEPARATOR, base_name);

    FILE* ast_file = fopen(ast_path, "w");
    if (!ast_file) {
        fprintf(stderr, "Cannot open AST output file: %s\n", ast_path);
        goto cleanup;
    }
    treeToDot(result.tree, ast_file);
    fclose(ast_file);
    printf("AST saved to: %s\n", ast_path);

    subprograms = generateSubprogramInfoCollection(input_file_path, result.tree);
    if (subprograms.error_count > 0) {
        fprintf(stderr, "Semantic analysis failed for source: %s\n", input_file_path);
        for (int i = 0; i < subprograms.error_count; i++) {
            fprintf(stderr, "Error: %s\n", subprograms.errors[i]);
        }
        goto cleanup;
    }

    if (subprograms.count <= 0) {
        fprintf(stderr, "No methods were found in source: %s\n", input_file_path);
        goto cleanup;
    }

    for (int i = 0; i < subprograms.count; i++) {
        SubprogramInfo* subprogram = &subprograms.items[i];
        if (!subprogram->cfg) {
            continue;
        }

        const char* cfg_name = subprogram->asm_name ? subprogram->asm_name : subprogram->name;
        char* method_component = sanitize_filename_component(cfg_name);
        char cfg_path[1024];
        snprintf(cfg_path, sizeof(cfg_path), "%s%c%s.%s.dot",
                 cfg_dir, PATH_SEPARATOR, base_name, method_component);
        free(method_component);

        FILE* cfg_file = fopen(cfg_path, "w");
        if (!cfg_file) {
            fprintf(stderr, "Cannot open CFG output file: %s\n", cfg_path);
            goto cleanup;
        }

        cfgNodesToDot(subprogram->cfg, cfg_file);
        fclose(cfg_file);

        printf("CFG saved to: %s\n", cfg_path);
    }

    call_graph = buildCallGraph(&subprograms);
    if (!call_graph) {
        fprintf(stderr, "Failed to build call graph for: %s\n", input_file_path);
        goto cleanup;
    }

    char call_graph_path[1024];
    snprintf(call_graph_path, sizeof(call_graph_path), "%s.callgraph.dot", base_name);
    FILE* call_graph_file = fopen(call_graph_path, "w");
    if (!call_graph_file) {
        fprintf(stderr, "Cannot open call graph output file: %s\n", call_graph_path);
        goto cleanup;
    }

    callGraphToDot(call_graph, call_graph_file);
    fclose(call_graph_file);
    printf("Call graph saved to: %s\n", call_graph_path);

    char asm_path[1024];
    snprintf(asm_path, sizeof(asm_path), "%s.asm", base_name);
    FILE* asm_file = fopen(asm_path, "w");
    if (!asm_file) {
        fprintf(stderr, "Cannot open ASM output file: %s\n", asm_path);
        goto cleanup;
    }

    char* asm_error = NULL;
    bool asm_ok = generateProgramAsm(&subprograms, asm_file, &asm_error);
    fclose(asm_file);

    if (!asm_ok) {
        remove(asm_path);
        fprintf(stderr, "ASM generation failed: %s\n", asm_error ? asm_error : "unknown error");
        free(asm_error);
        goto cleanup;
    }

    free(asm_error);
    printf("ASM saved to: %s\n", asm_path);

    FILE* asm_readback = fopen(asm_path, "r");
    if (asm_readback) {
        char line[1024];
        while (fgets(line, sizeof(line), asm_readback)) {
            fputs(line, stdout);
        }
        fclose(asm_readback);
    }

    success = 1;

cleanup:
    freeCallGraph(call_graph);
    freeSubprogramCollection(&subprograms);
    free(base_name);
    freeParseResult(&result);
    return success;
}

void print_help(const char* program_name)
{
    printf("\nBase usage:\n");
    printf("    %s <input_file> <output_ast_dir> <output_cfg_dir>\n", program_name);
    printf("\n");
    printf("Multiple files processing mode:\n");
    printf("    %s --multiple <input_file1> <input_file2> ... <input_fileN>\n", program_name);
    printf("\n");
    printf("Options:\n");
    printf("    --help        Display this help message\n");
    printf("    --multiple    Enter multiple files mode\n");
}

void print_results(const char* ast_dir, const char* cfg_dir, int processed_files_count, int total_files_count)
{
    printf("=== Processing completed ===\n");
    printf("Successfully processed: %d/%d files\n", processed_files_count, total_files_count);
    printf("AST trees saved in: %s/\n", ast_dir);
    printf("CFG trees saved in: %s/\n", cfg_dir);
    printf("\nTo visualise execute: dot -Tpng *file_name*.dot -o *file_name*.png\n");
}

int main(int argc, char* argv[])
{
    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print_help(argv[0]);
        return 0;
    }

    if (argc == 4 && strcmp(argv[1], "--multiple") != 0) {
        const char* input_file_path = argv[1];
        const char* ast_dir = argv[2];
        const char* cfg_dir = argv[3];
        int processed_files_count = 0;

        if (process_file(input_file_path, ast_dir, cfg_dir)) {
            processed_files_count = 1;
        }

        printf("\n");
        print_results(ast_dir, cfg_dir, processed_files_count, 1);
        return 0;
    }

    if (argc >= 3 && strcmp(argv[1], "--multiple") == 0) {
        const char* ast_dir = "output_ast_trees";
        const char* cfg_dir = "output_cfg_trees";

        if (create_directory(ast_dir) != 0) {
            struct stat st;
            if (stat(ast_dir, &st) != 0) {
                fprintf(stderr, "Failed to create AST directory: %s\n", ast_dir);
                return 1;
            }
        }

        if (create_directory(cfg_dir) != 0) {
            struct stat st;
            if (stat(cfg_dir, &st) != 0) {
                fprintf(stderr, "Failed to create CFG directory: %s\n", cfg_dir);
                return 1;
            }
        }

        int processed_files_count = 0;
        int total_files_count = argc - 2;

        for (int i = 2; i < argc; i++) {
            if (process_file(argv[i], ast_dir, cfg_dir)) {
                processed_files_count++;
            }
            printf("\n");
        }

        print_results(ast_dir, cfg_dir, processed_files_count, total_files_count);
        return 0;
    }

    fprintf(stderr, "Error: Invalid arguments\n\n");
    print_help(argv[0]);
    return 1;
}
