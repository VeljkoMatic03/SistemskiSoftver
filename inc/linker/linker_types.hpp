#ifndef LINKER_LINKER_TYPES_HPP
#define LINKER_LINKER_TYPES_HPP

#include <string>
#include <unordered_map>
#include <vector>

#include "utils/structs.hpp"

// One fully-parsed input .o (binary) file, before any aggregation/linking - exactly what
// readBinaryObjectFile produces. Everything here is still in this file's own local terms
// (symbol nums, section ids) - nothing has been merged with any other input file yet.
struct ParsedObjectFile {
    std::string path;
    std::vector<SymbolTableEntry> symbols;
    std::unordered_map<int, int> symbolByNum;      // symbol num -> index into symbols
                                                     // (num is NOT contiguous/index-aligned)
    std::vector<SectionTableEntry> sections;         // name+num, file order
    std::vector<std::vector<uint8_t>> sectionData;    // parallel to sections
    std::vector<RelocationTableEntry> relocations;
};

#endif // LINKER_LINKER_TYPES_HPP
