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




static void buildIfStatement(pANTLR3_BASE_TREE if_node,
                            ControlFlowGraph* cfg,
                            CFGNode* current_block);

static void buildWhileStatement(pANTLR3_BASE_TREE while_node,
                               ControlFlowGraph* cfg,
                               CFGNode* current_block);

static void buildRepeatStatement(pANTLR3_BASE_TREE repeat_node,
                                ControlFlowGraph* cfg,
                                CFGNode* current_block);

static void buildBreakStatement(pANTLR3_BASE_TREE break_node,
                               ControlFlowGraph* cfg,
                               CFGNode* current_block);


// Вспомогательная функция для получения текста узла
static char* getNodeText(pANTLR3_BASE_TREE node) {
    if (!node) return strdup("");
    pANTLR3_STRING text = node->getText(node);
    return strdup((char*)text->chars);
}


// Создание нового узла CFG
static CFGNode* createCFGNode(NodeType type, pANTLR3_BASE_TREE ast_node) {
    CFGNode* node = malloc(sizeof(CFGNode));
    static int next_id = 0;

    node->id = next_id++;
    node->type = type;
    node->ast_node = ast_node;
    node->label = getNodeText(ast_node);
    node->successors = NULL;
    node->succ_count = 0;
    node->predecessors = NULL;
    node->pred_count = 0;
    node->statements = NULL;
    node->stmt_count = 0;

    return node;
}

// Добавление связи между узлами
static void addEdge(CFGNode* from, CFGNode* to) {
    // Добавляем to в successors of from
    from->successors = realloc(from->successors,
                              (from->succ_count + 1) * sizeof(CFGNode*));
    from->successors[from->succ_count++] = to;

    // Добавляем from в predecessors of to
    to->predecessors = realloc(to->predecessors,
                              (to->pred_count + 1) * sizeof(CFGNode*));
    to->predecessors[to->pred_count++] = from;
}

// Рекурсивная функция обхода AST и построения CFG
static void buildCFGRecursive(pANTLR3_BASE_TREE treeNode,
                             ControlFlowGraph* cfg,
                             CFGNode* currentCfgNode) {
    if (!treeNode) return;

    pANTLR3_STRING node_text = treeNode->getText(treeNode);
    char* text = (char*)node_text->chars;

    // Проверяем тип узла
    if (strcmp(text, "IF") == 0) {
        buildIfStatement(treeNode, cfg, currentCfgNode);
    }
    else if (strcmp(text, "WHILE") == 0) {
        // Обработка цикла while
        buildWhileStatement(treeNode, cfg, currentCfgNode);
    }
    else if (strcmp(text, "REPEAT") == 0) {
        // Обработка цикла repeat
        buildRepeatStatement(treeNode, cfg, currentCfgNode);
    }
    else if (strcmp(text, "BREAK") == 0) {
        // Обработка break
        buildBreakStatement(treeNode, cfg, currentCfgNode);
    }
    else if (strcmp(text, "BLOCK") == 0) {
        // Обработка блока statements
        ANTLR3_UINT32 child_count = treeNode->getChildCount(treeNode);
        for (ANTLR3_UINT32 i = 0; i < child_count; i++) {
            pANTLR3_BASE_TREE child = (pANTLR3_BASE_TREE)treeNode->getChild(treeNode, i);
            buildCFGRecursive(child, cfg, currentCfgNode);
        }
    }
    else {
        // Обычный statement - добавляем в текущий basic block
        if (currentCfgNode) {
            currentCfgNode->statements = realloc(currentCfgNode->statements,
                                               (currentCfgNode->stmt_count + 1) *
                                               sizeof(pANTLR3_BASE_TREE));
            currentCfgNode->statements[currentCfgNode->stmt_count++] = treeNode;
        }

        ANTLR3_UINT32 child_count = treeNode->getChildCount(treeNode);
        for (ANTLR3_UINT32 i = 0; i < child_count; i++) {
            pANTLR3_BASE_TREE child = treeNode->getChild(treeNode, i);
            buildCFGRecursive(child, cfg, currentCfgNode);
        }
    }
    // printf("%d%s\n", currentCfgNode->id, currentCfgNode->label);
}



// Обработка IF statement
static void buildIfStatement(pANTLR3_BASE_TREE if_node,
                            ControlFlowGraph* cfg,
                            CFGNode* current_block) {
    // Создаем узлы для if
    CFGNode* if_block = createCFGNode(NODE_IF, if_node);
    // Добавляем узел в граф
    cfg->nodes[cfg->node_count++] = if_block;

    // Связываем с предыдущим блоком
    addEdge(current_block, if_block);

    // Находим condition, then и else части
    ANTLR3_UINT32 child_count = if_node->getChildCount(if_node);
    pANTLR3_BASE_TREE condition_node = NULL;
    pANTLR3_BASE_TREE then_node = NULL;
    pANTLR3_BASE_TREE else_node = NULL;

    for (ANTLR3_UINT32 i = 0; i < child_count; i++) {
        pANTLR3_BASE_TREE child = if_node->getChild(if_node, i);

        if (strcmp(getNodeText(child), "CONDITION") == 0) {
            condition_node = child;
        }
        else if (strcmp(getNodeText(child), "THEN") == 0) {
            then_node = child;
        }
        else if (strcmp(getNodeText(child), "ELSE") == 0) {
            else_node = child;
        }
    }

    //Закидываем в if_block всё, что есть внутри Condition ноды нашей ноде.
    buildCFGRecursive(condition_node, cfg, if_block);

    // Обрабатываем then часть
    CFGNode* then_block = NULL;
    if (then_node){
        then_block = createCFGNode(NODE_THEN, then_node);
        cfg->nodes[cfg->node_count++] = then_block;
        addEdge(if_block, then_block);
        buildCFGRecursive(then_node, cfg, then_block);
    }

    // Обрабатываем else часть
    CFGNode* else_block = NULL;
    if (else_node) {
        else_block = createCFGNode(NODE_ELSE, else_node);
        cfg->nodes[cfg->node_count++] = else_block;
        addEdge(if_block, else_block);
        // Рекурсивно обрабатываем содержимое else
        buildCFGRecursive(else_node, cfg, else_block);
    }

    // Создаем точку слияния после if
    CFGNode* merge_block = createCFGNode(NODE_MERGE, NULL);
    cfg->nodes[cfg->node_count++] = merge_block;

    // Связываем then и else с merge блоком
    if (then_block) {
        addEdge(then_block, merge_block);
    }
    if (else_block) {
        addEdge(else_block, merge_block);
    }

    addEdge(merge_block, current_block);

    printf("%d%s\n", current_block->id, current_block->label);
}

// Обработка WHILE statement
static void buildWhileStatement(pANTLR3_BASE_TREE while_node,
                               ControlFlowGraph* cfg,
                               CFGNode* current_block) {
    // Создаем узлы для цикла
    CFGNode* while_entry = createCFGNode(NODE_WHILE_ENTRY, while_node);
    cfg->nodes[cfg->node_count++] = while_entry;

    if (current_block) {
        addEdge(current_block, while_entry);
    }

    // Находим condition и body
    ANTLR3_UINT32 child_count = while_node->getChildCount(while_node);
    pANTLR3_BASE_TREE condition = NULL;
    pANTLR3_BASE_TREE body = NULL;

    for (ANTLR3_UINT32 i = 0; i < child_count; i++) {
        pANTLR3_BASE_TREE child = (pANTLR3_BASE_TREE)while_node->getChild(while_node, i);
        pANTLR3_STRING child_text = child->getText(child);

        if (strcmp((char*)child_text->chars, "CONDITION") == 0) {
            condition = child;
        }
        else if (strcmp((char*)child_text->chars, "DO") == 0) {
            body = child;
        }
    }

    // Блок проверки условия
    CFGNode* cond_block = createCFGNode(NODE_BASIC_BLOCK, condition);
    cfg->nodes[cfg->node_count++] = cond_block;
    addEdge(while_entry, cond_block);

    // Блок тела цикла
    CFGNode* body_block = NULL;
    if (body && body->getChildCount(body) > 0) {
        body_block = createCFGNode(NODE_WHILE_BODY, body);
        cfg->nodes[cfg->node_count++] = body_block;
        addEdge(cond_block, body_block);

        // Рекурсивно обрабатываем тело
        buildCFGRecursive(body, cfg, body_block);

        // Обратная связь к проверке условия
        addEdge(body_block, cond_block);
    }

    // Выход из цикла
    CFGNode* exit_block = createCFGNode(NODE_WHILE_EXIT, NULL);
    cfg->nodes[cfg->node_count++] = exit_block;
    addEdge(cond_block, exit_block);

    current_block = exit_block;
}

// Обработка REPEAT statement
static void buildRepeatStatement(pANTLR3_BASE_TREE repeat_node,
                                ControlFlowGraph* cfg,
                                CFGNode* current_block) {
    // Аналогично WHILE, но с другими условиями
    CFGNode* repeat_entry = createCFGNode(NODE_REPEAT_ENTRY, repeat_node);
    cfg->nodes[cfg->node_count++] = repeat_entry;

    if (current_block) {
        addEdge(current_block, repeat_entry);
    }

    // ... аналогичная логика обработки
}

// Обработка BREAK statement
static void buildBreakStatement(pANTLR3_BASE_TREE break_node,
                               ControlFlowGraph* cfg,
                               CFGNode* current_block) {
    CFGNode* break_node_cfg = createCFGNode(NODE_BREAK, break_node);
    cfg->nodes[cfg->node_count++] = break_node_cfg;

    if (current_block) {
        addEdge(current_block, break_node_cfg);
    }
}



ControlFlowGraph* buildCFG(pANTLR3_BASE_TREE tree) {
    if (!tree) return NULL;

    ControlFlowGraph* cfg = malloc(sizeof(ControlFlowGraph));
    cfg->max_nodes = 100;
    cfg->nodes = malloc(cfg->max_nodes * sizeof(CFGNode*));
    cfg->node_count = 0;

    // Создаем entry и exit узлы
    cfg->entry = createCFGNode(NODE_ENTRY, NULL);
    cfg->entry->label = "ENTRYPOINT";
    cfg->exit = createCFGNode(NODE_EXIT, NULL);
    cfg->exit->label = "EXIT";

    cfg->nodes[cfg->node_count++] = cfg->entry;
    cfg->nodes[cfg->node_count++] = cfg->exit;

    // // Создаем начальный basic block
    // CFGNode* start_block = createCFGNode(NODE_BASIC_BLOCK, NULL);
    // cfg->nodes[cfg->node_count++] = start_block;
    // addEdge(cfg->entry, start_block);

    // Рекурсивно строим CFG
    buildCFGRecursive(tree, cfg, cfg->entry);

    // Связываем последний блок с exit
    addEdge(cfg->nodes[cfg->node_count - 2], cfg->exit);

    return cfg;
}





