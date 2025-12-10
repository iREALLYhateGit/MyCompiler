cmake_minimum_required(VERSION 3.10)
project(MyCompiler C)

set(CMAKE_C_STANDARD 11)

include_directories(${CMAKE_SOURCE_DIR}/antlr3c/include)


file(GLOB ANTLR3C_SOURCES "antlr3c/src/*.c")

add_executable(MyCompiler
        main.c
        GrammarLexer.c
        GrammarParser.c
        parser_module.c
        ${ANTLR3C_SOURCES}
)

target_link_libraries(MyCompiler ws2_32)

