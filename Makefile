CXX ?= g++
FLEX ?= flex
BISON ?= bison
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wno-register -Isrc -Ibuild

TARGET := compiler
BUILD_DIR := build
LEXER_SRC := src/frontend/lexer.l
PARSER_SRC := src/frontend/parser.y
LEXER_GEN := $(BUILD_DIR)/lexer.lex.cpp
LEXER_HDR := $(BUILD_DIR)/lexer.lex.hpp
PARSER_GEN := $(BUILD_DIR)/parser.tab.cpp
PARSER_HDR := $(BUILD_DIR)/parser.tab.hpp

SOURCES := \
	src/main.cpp \
	src/ast/ast.cpp \
	src/backend/riscv.cpp \
	src/frontend/parser_driver.cpp \
	src/ir/cfg.cpp \
	src/ir/ir.cpp \
	src/ir/ir_builder.cpp \
	src/ir/optim.cpp \
	src/sema/semantic.cpp \
	src/support/diagnostic.cpp \
	$(LEXER_GEN) \
	$(PARSER_GEN)

.PHONY: all clean

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(PARSER_GEN) $(PARSER_HDR): $(PARSER_SRC) | $(BUILD_DIR)
	$(BISON) -d -o $(PARSER_GEN) $(PARSER_SRC)

$(LEXER_GEN) $(LEXER_HDR): $(LEXER_SRC) $(PARSER_GEN) $(PARSER_HDR) | $(BUILD_DIR)
	$(FLEX) --header-file=$(LEXER_HDR) -o $(LEXER_GEN) $(LEXER_SRC)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $@ $(SOURCES)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
