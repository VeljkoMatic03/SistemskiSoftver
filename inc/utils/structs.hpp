#ifndef UTILS_STRUCTS_HPP
#define UTILS_STRUCTS_HPP

#include <cstdint>
#include <string>
#include <vector>

enum class SymbolBind {
    LOCAL,
    GLOBAL
};

struct SymbolTableEntry {
    std::string name;
    int sectionId; // num of the section in the section table (-1 while the symbol is not yet defined)
    SymbolBind bind;
    int value; // value of the symbol (offset relative to section start)
    bool isDefined;
    int num; // order of the symbol in the symbol table
};

struct SectionTableEntry {
    std::string name;
    int num; // order of the section in the section table
};

enum class RelocationType {
    R_32,
    R_PC12S
};

struct RelocationTableEntry {
    int sectionId; // num in SectionTableEntry
    int offset; // offset inside the section where this patching is done
    int symbolId; // what is being written/patched
    int addend;
    RelocationType type; // 12bit displacement or 32bit value
};

#endif // UTILS_STRUCTS_HPP
