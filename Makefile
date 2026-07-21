CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Iinc
SRC_DIR := src
BUILD_DIR := build

SRCS := \
	$(SRC_DIR)/assembler/main_assembler.cpp \
	$(SRC_DIR)/assembler/assembler.cpp \
	$(SRC_DIR)/assembler/backpatch.cpp \
	$(SRC_DIR)/assembler/section_manager.cpp \
	$(SRC_DIR)/assembler/symbol_table.cpp \
	$(SRC_DIR)/assembler/lexer.cpp

OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))

TARGET := assembler.exe

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
