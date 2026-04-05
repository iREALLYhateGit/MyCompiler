# Description #

This project is a C language tool, that can scan, analyze and transform code written in custom language code (defined in
Grammar.g) into .asm file.

Execution: while executing this tool scans given path(s) to .txt file(s) with custom code, then for each file create ast
tree, cfg tree, then create subprogram and build callgraph to show relations between methods in custom code, then
transform it into .asm file, which can be executed by remote machine.

## Workflow policy ##

Always use Superpowers skills for:

- feature design → brainstorming
- multi-file changes → writing-plans
- implementation → test-driven-development

Do not skip planning phase.

Before starting active code stage, please decide how to test that, what are the test cases and etc. You may create
folder tests to put tests there.

## Never do ##

You are always prohibited to:

- use any Git commands
- modify any external files beyond this directory
- modify antlr directory, antlr3c, CMakeLists.txt, cmake-build-debug

## Documentation ##

Используй DOCS.md (если такого файла нет - создай) для добавления описания новой функциональности и способа
взаимодействия.

## Дополнительные ограничения для нового кода ##

1. «Нет» статичности. Все структуры данных должны допускать создание множества их экземпляров.
2. «Нет» «магическим» константам. Все значения должны либо вычисляться из обрабатываемых
   программой данных, либо задаваться с помощью аргументов командной строки или
   конфигурационных файлов.
3. «Нет» бесконечным циклам. Все циклы должны иметь понятные условия выхода: не допускается
   использовать, например, while (true), for (; ;) и т.д.
4. «Нет» утечке ресурсов. Все ресурсы, которые были использованы в программе и требуют
   освобождения (закрытия), должны корректно освобождаться (закрываться) независимо от
   возникновения ошибочных ситуаций или исключений. Например, открытый файл должен быть закрыт
   после того, как он перестал использоваться в программе; аллоцированная вручную память
   обязательно должна освобождаться.
5. «Нет» неожиданным завершениям программы. Все процессы, нити (threads) должны корректно
   завершаться в результате выполнения работы, а не прерываться функциями вида Abort/Exit.
6. «Нет» побайтовому вводу-выводу. Все данные должны обрабатываться частями (блоками)
   известного размера, с учетом целесообразного размера буфера.

## Build ##

To build project use command: "C:\Users\ПК\AppData\Local\Programs\CLion\bin\cmake\win\x64\bin\cmake.exe --build S:
\CLionProjects\MyCompiler\cmake-build-debug --target MyCompiler -j 14"

## Custom language ##

- The grammar of custom language is defined in Grammar.g file.

If custom language is intended to be changed (only if explicitly asked by user), then the order is the following:

1. Change Grammar.g file.
2. cd to "antlr" folder.
3. Run "java -jar antlr-3.4-complete.jar -Dlanguage=C ../Grammar.g". If Grammar.g is correct, then after executing
   command in current folder "antlr" files "Grammar.tokens", "GrammarLexer.c", "GrammarLexer.h", "GrammarParser.c", "
   GrammarParser.h" will be generated. If files are not generated, there is error -> sth went wrong, Grammar.g should be
   rewritten.
4. Copy-paste generated files to root folder (MyCompiler), they should rewrite existing ones.
5. Rebuild project (build section) to see if everything went successfully.

## Repository and Pipeline ##

- `MyCompiler` is a C11 project built with CMake and linked against the ANTLR3 C runtime from `antlr3c`.
- `Grammar.g` is the authoritative grammar file. `GrammarLexer.c/.h` and `GrammarParser.c/.h` are generated artifacts
  and should normally be regenerated from the grammar instead of being hand-edited.
- Main pipeline modules:
    - `parser_module.*`: parses the custom source language into an ANTLR AST and can export the AST to Graphviz `.dot`.
    - `op_tree.*`: converts parsed expressions into operation trees.
    - `cfg_builder_module.*`: builds per-method CFGs, collects subprogram metadata, and constructs the call graph.
    - `to_asm_module.*`: lowers CFG/subprogram data to target VM assembly.
    - `main.c`: orchestrates file processing and writes output artifacts.
- Output artifacts currently produced by the executable:
    - `<basename>_ast.dot`
    - `<basename>.<subprogram>.dot`
    - `<basename>.callgraph.dot`
    - `<basename>.asm`
- AST and CFG files are written to the directories passed on the command line (or the default multiple-mode
  directories). Call graph and ASM files are written in the current working directory using the sanitized input
  basename.

## Source Language Context ##

- Top-level declaration is `method`.
- Methods may be declaration-only (`;`) or may have a body with optional `var` declarations followed by a
  `begin ... end;` block.
- Grammar-level built-in types: `bool`, `byte`, `int`, `uint`, `long`, `ulong`, `char`, `string`.
- The grammar also supports `array[ ... ] of <type>` syntax.
- Statement forms supported by the grammar:
    - `if ... then ... else ...`
    - `while ... do ...`
    - `repeat ... while ...;`
    - `repeat ... until ...;`
    - `break;`
    - nested `begin ... end;`
    - expression statements
- Expression forms supported by the grammar:
    - assignment `:=`
    - function calls
    - identifiers and literals
    - unary `+`, `-`, `!`
    - binary `+`, `-`, `*`, `/`, `%`
    - comparisons `==`, `!=`, `<`, `<=`, `>`, `>=`
    - logical `&&`, `||`
    - parenthesized expressions
    - array indexing
- Literal forms recognized by the grammar: `true`, `false`, string, char, hexadecimal (`0x...`), binary (`0b...`),
  decimal.
- `generateProgramAsm(...)` uses `main()` with zero parameters as the full-program entry point. If `main()` is absent,
  the emitted assembly is just `[section CODE_CONST]` followed by `halt`.
- Empty/declaration-only `read()` and `write(num: int)` methods are treated as built-ins by the code generator and
  mapped to VM I/O. The generator also treats `setport(...)` as a special call form.

## Generated Assembly Conventions ##

- Generated `.asm` is currently emitted inside `[section CODE_CONST]`.
- Each subprogram is emitted as a label derived from a sanitized method name.
- The generator prints non-`main` methods first and `main` last.
- Non-void method results are stored in global slot `7160` (`RUNTIME_RETVAL_SLOT` in `to_asm_module.c`).
- The call/return sequence uses synthetic labels such as `M_sys_ret_dispatch` and `M_sys_ret_<id>`.
- Before a call, the generator saves caller variables with `ldg`, evaluates arguments left-to-right, stores arguments
  with `stg`, pushes a numeric return-site id, and jumps to the callee label.
- `read()` lowers to `in`. `write(int)` lowers to `out`.
- Backend type-size assumptions in `to_asm_module.c`: `bool`, `byte`, and `char` use 1 byte; `long` and `ulong` use 8
  bytes; other types default to 4 bytes.

## Target Machine (`target-definitions.pdsl`) ##

Resulted .asm file can be run by some remote machine, that architecture is defined by target-definitions.pdsl.

### All Target Mnemonics ###

| Mnemonic  | Instruction | Operands | Opcode | Meaning                                                           |
|-----------|-------------|----------|--------|-------------------------------------------------------------------|
| `pushi`   | `PUSHI`     | `IMM32`  | `0x01` | Push signed 32-bit immediate onto the stack.                      |
| `pushb`   | `PUSHB`     | `IMM1`   | `0x02` | Push boolean immediate `0` or `1`.                                |
| `pushc`   | `PUSHC`     | `IDX16`  | `0x03` | Push constant from `CODE_CONST[IDX]`.                             |
| `ldg`     | `LDG`       | `IDX16`  | `0x04` | Load global value at `DATA[0x2000 + IDX * 8]`.                    |
| `stg`     | `STG`       | `IDX16`  | `0x05` | Pop and store to global slot `IDX`.                               |
| `ldl`     | `LDL`       | `OFF16`  | `0x06` | Load local value at `DATA[fp + OFF]`.                             |
| `stl`     | `STL`       | `OFF16`  | `0x07` | Pop and store to local slot at `fp + OFF`.                        |
| `dup`     | `DUP`       | none     | `0x08` | Duplicate the top stack value.                                    |
| `pop`     | `POP`       | none     | `0x09` | Discard the top stack value.                                      |
| `add`     | `ADD`       | none     | `0x10` | Replace the top two stack values with their sum.                  |
| `sub`     | `SUB`       | none     | `0x11` | Replace the top two stack values with their difference.           |
| `mul`     | `MUL`       | none     | `0x12` | Replace the top two stack values with their product.              |
| `div`     | `DIV`       | none     | `0x13` | Replace the top two stack values with their quotient.             |
| `mod`     | `MOD`       | none     | `0x14` | Replace the top two stack values with their remainder.            |
| `eq`      | `EQ`        | none     | `0x18` | Compare equality, push a boolean result, and update `ZF`.         |
| `ne`      | `NE`        | none     | `0x19` | Compare inequality, push a boolean result, and update `ZF`.       |
| `lt`      | `LT`        | none     | `0x1A` | Compare less-than, push a boolean result, and update `ZF`.        |
| `le`      | `LE`        | none     | `0x1B` | Compare less-or-equal, push a boolean result, and update `ZF`.    |
| `gt`      | `GT`        | none     | `0x1C` | Compare greater-than, push a boolean result, and update `ZF`.     |
| `ge`      | `GE`        | none     | `0x1D` | Compare greater-or-equal, push a boolean result, and update `ZF`. |
| `and_`    | `AND`       | none     | `0x20` | Boolean AND on the top two stack values, then update `ZF`.        |
| `or_`     | `OR`        | none     | `0x21` | Boolean OR on the top two stack values, then update `ZF`.         |
| `jmp`     | `JMP`       | `T24`    | `0x30` | Unconditional jump.                                               |
| `jz`      | `JZ`        | `T24`    | `0x31` | Pop a condition and jump if it is zero.                           |
| `jnz`     | `JNZ`       | `T24`    | `0x32` | Pop a condition and jump if it is non-zero.                       |
| `halt`    | `HALT`      | none     | `0x3F` | Stop execution.                                                   |
| `setport` | `SETPORT`   | `P16`    | `0x40` | Set the hidden I/O port register.                                 |
| `in`      | `IN`        | none     | `0x41` | Read one decimal integer from input and push it.                  |
| `out`     | `OUT`       | none     | `0x42` | Pop and print one integer as decimal text.                        |

### Important Note ###

- `target-definitions.pdsl` defines logical mnemonics as `and_` and `or_`.
- `to_asm_module.c` currently emits plain `and` and `or` for logical expressions.
- If strict conformance to the PDSL is required, keep this naming mismatch in mind when editing the assembler backend.

## Выполненные задания ##

### Задание 1 ###

Реализован модуль разбора кода (parser_module.c и parser_module.h) согласно кастомному языку в Grammar.g, реализовано
построение по исходному файлу(ам) с кодом синтаксического дерева с узлами, соответствующими элементам синтаксической
модели
языка. AST дерево записывается в файл .dot.

В каком порядке задание было выполнено:

1. ANTLR3C выбран в качестве средства синтаксического анализа.
2. Описан Grammar.g файл, содержащий синтаксис языка.
3. Реализован модуль (parser_module.c и parser_module.h), принимающий на вход файл(ы) с кодом и путь, по которому
   необходимо сохранить сгенерированное AST дерево .dot.

### Задание 2 ###

Реализован модуль (cfg_builder_module.c и cfg_builder_module.h) построения графа потока управления (CFG) по AST
представлению(ях) файла(ов).

В каком порядке задание было выполнено:

1. В cfg_builder_module.h описаны структуры данных, необходимые для представления информации о наборе подпрограмм (
   SubprogramInfo) и графе потока управления (ControlFlowGraph) для каждой подпрограммы.
    - Каждая функция - подпрограмма (SubprogramInfo), содержит ControlFlowGraph.
    - Для каждого узла в графе потока управления, представляющего собой базовый блок, указаны узлы для безусловного и
      условного перехода, дерево операций (op_tree).
2. Реализован модуль (cfg_builder_module.c) построения графа потока управления.
3. Поскольку функции (подпрограммы) могут вызывать друг друга, то создаётся также граф вызовов (CallGraph), описывающий
   отношения между ними в плане вызовов друг друга, main метод считать отправной точкой.

### Задание 3 ###

Реализован модуль (to_asm_module.c и to_asm_module.h) формирования линейного кода посредством анализа графа потока
управления для набора подпрограмм. Полученный линейный код записан в мнемонической форме в .asm файл.

1. Составлено описание виртуальной машины с набором инструкций и моделью памяти по варианту (target-definitions.pdsl).
    - Описан набор регистров и банков памяти.
    - Описан набор инструкций: для каждой инструкции задана структура операционного кода, содержащего описание операндов
      и набор операций, изменяющих состояние ВМ.
    - Описаны инструкции перемещения данных и загрузки констант.
    - Описаны инструкции арифметических и логических операций.
    - Описаны инструкции условной и безусловной передачи управления.
    - Описаны инструкции ввода-вывода с использованием скрытого регистра в качестве порта ввода-вывода.
    - Описан набор мнемоник, соответствующих инструкциям ВМ.
2. Готовый .asm листинг можно транслировать в бинарный модуль по описанию ВМ (target-definitions.pdsl) с помощью
   удалённой машины. После чего запустить полученный бинарный модуль на исполнение и получить результат работы.
   Проверить корректность решения посредством сборки сгенерированного листинга и запуска
   полученного бинарного модуля на эмуляторе ВМ.

### Задание 5 ###

Дополнить разработанную программу поддержкой пользовательских типов данных (структуры можно найти ниже) с возможностью
прямого наследования членов.

1. Описать необходимые структуры данных для представления информации о пользовательских типах, их членах (полях и
   методах) и отношениях согласно структуре, описанной после порядка реализации). Добавить поддержку новых
   синтаксических конструкций в Grammar.g, получить новые Grammar.tokens, GrammarLexer.h,
   GrammarLexer.c, GrammarParser.c, GrammarParser.h через antlr. Добавить обработку в parser_module.

Реализованная структура:
source: sourceItem*;
typeRef: {
|builtin: 'bool'|'byte'|'int'|'uint'|'long'|'ulong'|'char'|'string';
|custom: identifier;
|array: 'array' '[' (',')* ']' 'of' typeRef;
};
funcSignature: identifier '(' list<argDef> ')' (':' typeRef)? {
argDef: identifier (':' typeRef)?;
};
sourceItem: {
|funcDef: 'method' funcSignature (body|';') {
body: ('var' (list<identifier> (':' typeRef)? ';')*)? statement.block;
};
statement: {
|if: 'if' expr 'then' statement ('else' statement)?;
|block: 'begin' statement* 'end' ';';
|while: 'while' expr 'do' statement;
|do: 'repeat' statement ('while'|'until') expr ';';
|break: 'break' ';';
|expression: expr ';';
};
expr: { // присваивание через ':='
|binary: expr binOp expr; // где binOp - символ бинарного оператора
|unary: unOp expr; // где unOp - символ унарного оператора
|braces: '(' expr ')';
|call: expr '(' list<expr> ')';
|indexer: expr '[' list<expr> ']';
|place: identifier;
|literal: bool|str|char|hex|bits|dec;
};

Структура, которую нужно добавить для этого задания:
sourceItem: {
|funcDef: 'method' funcSignature (body|';'|importSpec) {
body: varsSpec? statement.block;
varsSpec: 'var' (list<identifier> (':' typeRef)? ';')*;
importSpec: 'from' (dllEntryName 'in')? dllName ';';
dllName: str; // имя DLL, содержащей импортируемую функцию
dllEntryName: str; // имя точки входа при отличии от объявляемого
};
|classDef: 'class' identifier funcDef.varsSpec 'begin' member* 'end' {
member: modifier? (funcDef);
modifier: 'public'|'private';
};
};

Реализовать:

- Полиморфизм перегрузкой.
- Ресширение типов через интерфейсы.

Характеристики:

- Прямое наследование от одного предка разрешено.
- Полиморфизм подпрограмм разрешён только для методов пользовательских типов.
- Переопределение методов базового типа запрещено, все методы в одной цепочке наследования имеют разные сигнатуры.
- Для пользовательских типов разрешено реализовывать несколько интерфейсов, поддержка расширения между интерфейсами не
  нужна.

2. Добавить поддержку новых операций для работы с членами типов в дереве операций (op_tree и cfg_builder_module).
3. Поддержать формирование необходимых последовательностей инструкций в линейном коде для добавленных операций в
   модуле to_asm_module.
4. Используя псевдоинструкции определения данных, добавить в секцию с информацией о структуре программы информацию,
   описывающую пользовательские типы и их поля (имена и типы значений).