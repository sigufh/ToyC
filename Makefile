CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -pedantic

TARGET := compiler
SRC := src/main.cpp src/compiler.cpp
HDR := src/compiler.hpp

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC)

clean:
	rm -f $(TARGET)
