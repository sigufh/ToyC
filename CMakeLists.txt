cmake_minimum_required(VERSION 3.13)
project(ToyCCompiler LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W3)
else()
  add_compile_options(-Wall -Wno-register)
endif()

find_package(FLEX REQUIRED)
find_package(BISON 3.0 REQUIRED)

set(FB_EXT ".cpp")
set(LEXER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/lexer.l")
set(PARSER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/parser.y")
set(LEXER_OUT "${CMAKE_CURRENT_BINARY_DIR}/lexer.lex${FB_EXT}")
set(PARSER_OUT "${CMAKE_CURRENT_BINARY_DIR}/parser.tab${FB_EXT}")

flex_target(Lexer ${LEXER_SRC} ${LEXER_OUT}
            DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/lexer.lex.hpp)
bison_target(Parser ${PARSER_SRC} ${PARSER_OUT})
add_flex_bison_dependency(Lexer Parser)

include_directories(src)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

set(SOURCES
  src/main.cpp
  src/ast/ast.cpp
  src/backend/riscv.cpp
  src/frontend/parser_driver.cpp
  src/ir/cfg.cpp
  src/ir/ir.cpp
  src/ir/ir_builder.cpp
  src/ir/optim.cpp
  src/sema/semantic.cpp
  src/support/diagnostic.cpp
  ${FLEX_Lexer_OUTPUTS}
  ${BISON_Parser_OUTPUT_SOURCE}
)

add_executable(compiler ${SOURCES})
