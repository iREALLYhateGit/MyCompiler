grammar Grammar;

options {
    language = C;
    output = AST;
}

tokens {
    SOURCE;
    METHOD_DECL;
    CLASS_DECL;
    INTERFACE_DECL;
    PARAMETERS;
    PARAMETER;
    TYPE;
    VOID_VALUE;
    RETURN_TYPE;
    BODY;
    VAR_DECLARATIONS;
    VAR_DECLARATION;
    VARIABLES;
    BLOCK;
    EXPRESSION;
    CALL;
    MEMBER_CALL;
    ASSIGN;
    VALUE;
    ARGUMENTS;
    IN_BRACES;
    UNARY_OPERATION;
    ARRAY_ELEMENT;
    ARRAY_ID;
    ARRAY_ELEMENT_INDEX;
    MEMBER_ACCESS;
    IMPORT_SPEC;
    DLL_NAME;
    DLL_ENTRY;
    BASE_TYPE;
    IMPLEMENTS;
    MEMBER;
    PUBLIC;
    PRIVATE;
    ADD;
    SUBTRACT;
    MULTIPLY;
    DIVISION;
    RESIDUE;
    EQUALS;
    NOT_EQUALS;
    LESS_THAN;
    LESS_THAN_OR_EQUALS;
    MORE_THAN;
    MORE_THAN_OR_EQUALS;
    AND;
    OR;
    ID;
    IF;
    CONDITION;
    THEN;
    ELSE;
    WHILE;
    DO;
    REPEAT;
    REPEATABLE_PART;
    BREAK;
    UNTIL;
}

/* ===========================
   LEXER
   =========================== */

fragment LETTER: 'a'..'z'|'A'..'Z'|'_';
fragment DIGIT: '0'..'9';

WS: (' '|'\t'|'\r'|'\n')+ { $channel=HIDDEN; };

K_TRUE: 'true';
K_FALSE: 'false';
K_METHOD: 'method';
K_VAR: 'var';
K_IF: 'if';
K_THEN: 'then';
K_ELSE: 'else';
K_BEGIN: 'begin';
K_END: 'end';
K_WHILE: 'while';
K_DO: 'do';
K_REPEAT: 'repeat';
K_UNTIL: 'until';
K_BREAK: 'break';
K_OF: 'of';
K_ARRAY: 'array';
K_BOOL: 'bool';
K_BYTE: 'byte';
K_INT: 'int';
K_UINT: 'uint';
K_LONG: 'long';
K_ULONG: 'ulong';
K_CHAR: 'char';
K_STRING: 'string';
K_CLASS: 'class';
K_INTERFACE: 'interface';
K_IMPLEMENTS: 'implements';
K_FROM: 'from';
K_IN: 'in';
K_PUBLIC: 'public';
K_PRIVATE: 'private';

IDENTIFIER: LETTER (LETTER|DIGIT)*;

STRING: '"' ( ~('"'|'\\') | '\\' . )* '"';
CHAR: '\'' ( ~('\''|'\\') | '\\' . ) '\'';
HEX: '0' ('x'|'X') ('0'..'9'|'a'..'f'|'A'..'F')+;
BITS: '0' ('b'|'B') ('0'|'1')+;
DEC: DIGIT+;

/* ===========================
   PARSER
   =========================== */

source
    : sourceItem* EOF
      -> ^(SOURCE sourceItem*)
    ;

sourceItem
    : funcDef
    | classDef
    | interfaceDef
    ;

typeRef
    : builtinType
    | IDENTIFIER
    | K_ARRAY '[' commaList ']' K_OF typeRef
    ;

commaList: (',' )*;

builtinType
    : K_BOOL | K_BYTE | K_INT | K_UINT | K_LONG | K_ULONG | K_CHAR | K_STRING
    ;

funcDef
    : K_METHOD funcSignature (methodBody | importSpec | ';')
      -> ^(METHOD_DECL funcSignature methodBody? importSpec?)
    ;

funcSignature
    : methodName '(' methodParameters? ')' methodReturnType
      -> methodName methodParameters? methodReturnType
    ;

methodName
    : IDENTIFIER -> ^(ID IDENTIFIER)
    ;

methodParameter
    : IDENTIFIER (':' typeRef)?
      -> ^(PARAMETER ^(ID IDENTIFIER) ^(TYPE typeRef)?)
    ;

methodParameters
    : methodParameter (',' methodParameter)*
      -> ^(PARAMETERS methodParameter+)
    ;

methodReturnType
    : ':' typeRef -> ^(RETURN_TYPE typeRef)
    | -> ^(RETURN_TYPE VOID_VALUE)
    ;

methodBody
    : varsSpec? statementBlock
      -> ^(BODY varsSpec? statementBlock)
    ;

varsSpec
    : K_VAR varDeclaration*
      -> ^(VAR_DECLARATIONS varDeclaration*)
    ;

varDeclaration
    : variableNameList (':' typeRef)? ';'
      -> ^(VAR_DECLARATION ^(TYPE typeRef)? variableNameList)
    ;

variableNameList
    : IDENTIFIER (',' IDENTIFIER)*
      -> ^(VARIABLES (^(ID IDENTIFIER))+)
    ;

importSpec
    : K_FROM STRING K_IN STRING ';'
      -> ^(IMPORT_SPEC ^(DLL_ENTRY STRING) ^(DLL_NAME STRING))
    | K_FROM STRING ';'
      -> ^(IMPORT_SPEC ^(DLL_NAME STRING))
    ;

classDef
    : K_CLASS IDENTIFIER classBaseSpec? classImplementsSpec? varsSpec K_BEGIN member* K_END
      -> ^(CLASS_DECL ^(ID IDENTIFIER) classBaseSpec? classImplementsSpec? varsSpec member*)
    ;

classBaseSpec
    : ':' IDENTIFIER
      -> ^(BASE_TYPE ^(ID IDENTIFIER))
    ;

classImplementsSpec
    : K_IMPLEMENTS IDENTIFIER (',' IDENTIFIER)*
      -> ^(IMPLEMENTS (^(ID IDENTIFIER))+)
    ;

member
    : modifier? funcDef
      -> ^(MEMBER modifier? funcDef)
    ;

modifier
    : K_PUBLIC -> PUBLIC
    | K_PRIVATE -> PRIVATE
    ;

interfaceDef
    : K_INTERFACE IDENTIFIER K_BEGIN interfaceMember* K_END
      -> ^(INTERFACE_DECL ^(ID IDENTIFIER) interfaceMember*)
    ;

interfaceMember
    : K_METHOD funcSignature ';'
      -> ^(METHOD_DECL funcSignature)
    ;

/* ===========================
   STATEMENTS
   =========================== */

statement
    : ifStatement
    | whileStatement
    | repeatStatement
    | breakStatement
    | statementBlock
    | expressionStatement
    ;

ifStatement
    : K_IF expression K_THEN statement (K_ELSE statement)?
      -> ^(IF ^(CONDITION expression) ^(THEN statement) ^(ELSE statement)?)
    ;

whileStatement
    : K_WHILE expression K_DO statement
      -> ^(WHILE ^(CONDITION expression) ^(DO statement))
    ;

repeatStatement
    : K_REPEAT statement (K_WHILE | K_UNTIL) expression ';'
      -> ^(REPEAT ^(REPEATABLE_PART statement) ^(UNTIL expression))
    ;

breakStatement
    : K_BREAK ';'
      -> ^(BREAK)
    ;

statementBlock
    : K_BEGIN statement* K_END ';'
      -> ^(BLOCK statement*)
    ;

expressionStatement
    : expression ';' -> expression
    ;

/* ===========================
   EXPRESSIONS
   =========================== */

expression
    : (assignableExpression ':=') => assignExpression
    | calculationExpression -> ^(EXPRESSION calculationExpression)
    ;

assignExpression
    : assignableExpression ':=' calculationExpression
      -> ^(ASSIGN assignableExpression ^(VALUE calculationExpression))
    ;

assignableExpression
    : memberAccessExpression
    | indexerExpression
    | placeExpression
    ;

calculationExpression
    : orExpression
    ;

orExpression
    : andExpression ('||'^ andExpression)*
    ;

andExpression
    : eqExpression ('&&'^ eqExpression)*
    ;

eqExpression
    : relExpression (equalOperator^ relExpression)*
    ;

equalOperator
    : '==' -> ^(EQUALS)
    | '!=' -> ^(NOT_EQUALS)
    ;

relExpression
    : addExpression (compareOperator^ addExpression)*
    ;

compareOperator
    : '<' -> ^(LESS_THAN)
    | '<=' -> ^(LESS_THAN_OR_EQUALS)
    | '>' -> ^(MORE_THAN)
    | '>=' -> ^(MORE_THAN_OR_EQUALS)
    ;

addExpression
    : mulExpression (plusSubtractOperator^ mulExpression)*
    ;

plusSubtractOperator
    : '+' -> ^(ADD)
    | '-' -> ^(SUBTRACT)
    ;

mulExpression
    : unaryExpression (mulOperator^ unaryExpression)*
    ;

mulOperator
    : '*' -> ^(MULTIPLY)
    | '/' -> ^(DIVISION)
    | '%' -> ^(RESIDUE)
    ;

unaryExpression
    : unaryOp unaryExpression -> ^(UNARY_OPERATION unaryOp unaryExpression)
    | primaryExpr
    ;

unaryOp
    : '+'
    | '-'
    | '!'
    ;

primaryExpr
    : memberCallExpression
    | memberAccessExpression
    | callExpression
    | bracesExpression
    | indexerExpression
    | placeExpression
    | literal
    ;

callExpression
    : IDENTIFIER '(' argumentList? ')'
      -> ^(CALL ^(ID IDENTIFIER) argumentList?)
    ;

memberChain
    : IDENTIFIER '.' IDENTIFIER ('.' IDENTIFIER)*
      -> ^(MEMBER_ACCESS IDENTIFIER+)
    ;

memberCallExpression
    : memberChain '(' argumentList? ')'
      -> ^(MEMBER_CALL memberChain argumentList?)
    ;

memberAccessExpression
    : memberChain
    ;

argumentList
    : calculationExpression (',' calculationExpression)*
      -> ^(ARGUMENTS calculationExpression+)
    ;

bracesExpression
    : '(' calculationExpression ')'
      -> ^(IN_BRACES calculationExpression)
    ;

indexerExpression
    : IDENTIFIER '[' calculationExpression ']'
      -> ^(ARRAY_ELEMENT ^(ARRAY_ID IDENTIFIER) ^(ARRAY_ELEMENT_INDEX calculationExpression))
    ;

placeExpression
    : IDENTIFIER -> ^(ID IDENTIFIER)
    ;

literal
    : K_TRUE -> K_TRUE
    | K_FALSE -> K_FALSE
    | STRING -> STRING
    | CHAR -> CHAR
    | HEX -> HEX
    | BITS -> BITS
    | DEC -> DEC
    ;
