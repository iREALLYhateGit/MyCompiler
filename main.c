#include <stdio.h>

#include "parser_module.h"
#include "cfg_builder_module.h"

#define PATH_SEPARATOR '\\'


// Функция для создания директории
int create_directory(const char* path) {
    return mkdir(path);
}


// Функция для извлечения чистого имени файла без пути и расширения
char* get_clean_filename(const char* fullpath) {
    const char* filename = fullpath;

    // Находим последний разделитель пути
    const char* last_sep = strrchr(fullpath, PATH_SEPARATOR);

    // Для Unix также проверяем прямой слеш
    const char* last_unix_sep = strrchr(fullpath, '/');
    if (last_unix_sep && (!last_sep || last_unix_sep > last_sep)) {
        last_sep = last_unix_sep;
    }

    if (last_sep) {
        filename = last_sep + 1;
    }

    // Копируем имя файла
    char* name = strdup(filename);

    // Удаляем расширение (все после последней точки)
    char* dot = strrchr(name, '.');
    if (dot) {
        *dot = '\0';
    }

    // Заменяем недопустимые символы на подчеркивания
    for (char* p = name; *p; p++) {
        if (*p == '.' || *p == '/' || *p == '\\' || *p == ':' ||
            *p == '*' || *p == '?' || *p == '"' || *p == '<' ||
            *p == '>' || *p == '|') {
            *p = '_';
        }
    }

    return name;
}

// Функция обработки одного файла
int process_file(const char* input_file, const char* ast_dir, const char* cfg_dir)
{
    printf("\n=== Processing file: %s ===\n", input_file);

    ParseResult result = parseFile(input_file);

    // Проверка ошибок
    int has_errors = 0;
    for (int i = 0; i < result.errorCount; i++) {
        fprintf(stderr, "Error in %s: %s\n", input_file, result.errors[i]);
        has_errors = 1;
    }

    if (!result.tree) {
        fprintf(stderr, "Tree creation failed for %s\n", input_file);
        freeParseResult(&result);
        return 0;
    }

    // Получаем чистое имя файла (без пути и расширения)
    char* base_name = get_clean_filename(input_file);

    // Генерируем AST DOT файл
    char ast_path[1024];
    snprintf(ast_path, sizeof(ast_path), "%s%c%s_ast.dot",
             ast_dir, PATH_SEPARATOR, base_name);

    FILE* ast_file = fopen(ast_path, "w");
    if (!ast_file) {
        fprintf(stderr, "Cannot open AST output file: %s\n", ast_path);
        free(base_name);
        freeParseResult(&result);
        return 0;
    }

    treeToDot(result.tree, ast_file);
    fclose(ast_file);

    printf("AST saved to: %s\n", ast_path);

    // Строим CFG только если нет ошибок в AST
    if (!has_errors) {
        ControlFlowGraph* cfg = buildCFG(result.tree);

        if (cfg) {
            // Генерируем CFG DOT файл
            char cfg_path[1024];
            snprintf(cfg_path, sizeof(cfg_path), "%s%c%s_cfg.dot",
                     cfg_dir, PATH_SEPARATOR, base_name);

            FILE* cfg_file = fopen(cfg_path, "w");
            if (!cfg_file) {
                fprintf(stderr, "Cannot open CFG output file: %s\n", cfg_path);
                // freeCFG(cfg);
                free(base_name);
                freeParseResult(&result);
                return 0;
            }

            // cfgToDot(cfg, cfg_file);
            cfgNodesToDot(cfg, cfg_file);
            fclose(cfg_file);

            printf("CFG saved to: %s\n", cfg_path);

            // freeCFG(cfg);
        } else {
            fprintf(stderr, "CFG construction failed for %s\n", input_file);
        }
    } else {
        printf("Skipping CFG generation due to AST errors\n");
    }

    free(base_name);
    freeParseResult(&result);

    return 1;
}

// Функция для создания всех необходимых директорий в пути
int create_directories_for_path(const char* path) {
    char temp[1024];
    char* p = temp;

    strncpy(temp, path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    #ifdef _WIN32
        // Пропускаем букву диска, если есть
        if (strlen(temp) > 2 && temp[1] == ':') {
            p = temp + 2;
        }
    #endif

    // Создаем все директории в пути
    while (*p) {
        if (*p == PATH_SEPARATOR || *p == '/') {
            char old_char = *p;
            *p = '\0';

            if (strlen(temp) > 0) {
                struct stat st;
                if (stat(temp, &st) != 0) {
                    if (create_directory(temp) != 0) {
                        return 0;
                    }
                }
            }

            *p = old_char;
        }
        p++;
    }

    return 1;
}

// Функция для вывода справки
void print_help(const char* program_name) {
    printf("\nBase usage:\n");
    printf("    %s <input_file> <output_ast_file> <output_cfg_file>\n", program_name);
    printf("\n");
    printf("Multiple files processing mode:\n");
    printf("    %s --multiple <input_file1> <input_file2> ... <input_fileN>\n", program_name);
    printf("\n");
    printf("Options:\n");
    printf("    --help    Display this help message\n");
    printf("    --multiple    Enter multiple files mode\n");
}

int main(int argc, char* argv[])
{
    // Проверка на запрос справки
    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0))
    {
        print_help(argv[0]);
        return 0;
    }

    // Режим обработки одного файла
    if (argc == 4 && strcmp(argv[1], "--multiple") != 0)
    {

        ParseResult result = parseFile(argv[1]);

        if (!result.tree)
        {
            fprintf(stderr, "AST tree creation failed due to some unexpected ERROR.\n");
            freeParseResult(&result);
            return 1;
        }

        if (result.errorCount == 0)
            printf("Input file has been successfully parsed!\n");

        else
        {
            printf("Input file was parsed with errors:\n");
            // Ошибки
            for (int i = 0; i < result.errorCount; i++)
                fprintf(stderr, "Error: %s\n", result.errors[i]);
        }

        printf("Syntactic tree:\n");
        printTree(result.tree, 0);

        FILE* dotFile = fopen(argv[2], "w");
        if (!dotFile)
        {
            fprintf(stderr, "Cannot open output file for ast.\n");
            freeParseResult(&result);
            return 1;
        }


        treeToDot(result.tree, dotFile);
        fclose(dotFile);

        if (result.errorCount == 0)
        {
            ControlFlowGraph* cfg = buildCFG(result.tree);

            if (cfg)
            {
                FILE* cfgDot = fopen(argv[3], "w");
                if (!cfgDot)
                {
                    fprintf(stderr, "Cannot open output file for ast.\n");
                    freeParseResult(&result);
                    return 1;
                }

                // cfgToDot(cfg, cfgDot);
                cfgNodesToDot(cfg, cfgDot);

                fclose(cfgDot);
                // freeCFG(cfg);
            }
        }

        freeParseResult(&result);

        printf("\nAST is stored into %s", argv[2]);
        printf("\nCFG is stored into %s", argv[3]);
        printf("\nTo visualise execute: dot -Tpng %s -o %s.png", argv[2], argv[2]);
        return 0;
    }

    // Режим обработки нескольких файлов
    if (argc >= 3 && strcmp(argv[1], "--multiple") == 0)
    {
        // Создаем выходные директории
        const char* ast_dir = "output_ast_trees";
        const char* cfg_dir = "output_cfg_trees";

        // Создаем директории, если они не существуют
        if (create_directory(ast_dir) != 0)
        {
            // Проверяем, существует ли уже директория
            struct stat st;
            if (stat(ast_dir, &st) != 0)
            {
                fprintf(stderr, "Failed to create AST directory: %s\n", ast_dir);
                return 1;
            }
        }

        if (create_directory(cfg_dir) != 0)
        {
            struct stat st;
            if (stat(cfg_dir, &st) != 0)
            {
                fprintf(stderr, "Failed to create CFG directory: %s\n", cfg_dir);
                return 1;
            }
        }

        printf("AST output directory: %s/\n", ast_dir);
        printf("CFG output directory: %s/\n", cfg_dir);
        printf("\n");

        // Обрабатываем все переданные файлы
        int processed_count = 0;
        int total_files = argc - 2; // минус имя программы и --multiple

        for (int i = 2; i < argc; i++)
        {
            if (process_file(argv[i], ast_dir, cfg_dir))
            {
                processed_count++;
            }
            printf("\n");
        }

        printf("=== Processing completed ===\n");
        printf("Successfully processed: %d/%d files\n", processed_count, total_files);
        printf("AST trees saved in: %s/\n", ast_dir);
        printf("CFG trees saved in: %s/\n", cfg_dir);
        printf("\nTo visualise execute: dot -Tpng *file_name*.dot -o *file_name*.png");

        return 0;
    }

    // Неправильное использование
    fprintf(stderr, "Error: Invalid arguments\n\n");
    print_help(argv[0]);
    return 1;
}
