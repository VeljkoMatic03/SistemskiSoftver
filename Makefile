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

TARGET := assembler

SRCS_LINKER := \
	$(SRC_DIR)/linker/main_linker.cpp \
	$(SRC_DIR)/linker/object_reader.cpp \
	$(SRC_DIR)/linker/aggregator.cpp \
	$(SRC_DIR)/linker/cli.cpp \
	$(SRC_DIR)/linker/linker.cpp \
	$(SRC_DIR)/linker/placement.cpp \
	$(SRC_DIR)/linker/relocator.cpp \
	$(SRC_DIR)/linker/relocatable_writer.cpp \
	$(SRC_DIR)/linker/hex_writer.cpp

OBJS_LINKER := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS_LINKER))

TARGET_LINKER := linker

.PHONY: all clean

all: $(TARGET) $(TARGET_LINKER)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

$(TARGET_LINKER): $(OBJS_LINKER)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS_LINKER)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET) $(TARGET_LINKER)
