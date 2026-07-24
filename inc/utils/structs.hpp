#ifndef UTILS_STRUCTS_HPP
#define UTILS_STRUCTS_HPP

#include <cstdint>
#include <string>
#include <vector>

enum class SymbolBind {
    LOCAL,
    GLOBAL
};

// UND: not yet defined (forward reference or still-unresolved .extern).
// SYM: an ordinary user symbol, defined via a label.
// SEC: an auto-generated symbol representing a section itself (one per section, value=0,
// bind=GLOBAL) - relocations against a LOCAL symbol are rewritten to reference the LOCAL
// symbol's own SEC entry instead, with the LOCAL symbol's own offset folded into the addend
enum class SymbolType {
    UND,
    SYM,
    SEC
};

struct SymbolTableEntry {
    std::string name;
    int sectionId; // num of the section in the section table (-1 while the symbol is not yet defined)
    SymbolBind bind;
    int value; // value of the symbol (offset relative to section start)
    bool isDefined;
    int num; // order of the symbol in the symbol table
    SymbolType type;
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


// structs needed for binary file

struct BinarySymbolTableEntry {
    int num;
    int nameOffset; // offset from string pool
    int sectionId;
    int bind;
    int value;
    int defined;
    int type; // raw SymbolType value: UND=0, SYM=1, SEC=2
};

struct BinarySectionTableEntry {
    int num;
    int nameOffset; // offset from string pool
    int size;
    int dataOffset; // offset for data memory of this section
};

struct BinaryRelocationTableEntry {
    int sectionId;
    int symbolNum;
    int offset; // where is the address in memory (relative to section base)
                // that needs to be patched by linker
    int addend;
    int relocationType;
};

constexpr int HEADER_SIZE = 7 * sizeof(int);
constexpr int SYMTAB_SIZE = sizeof(BinarySymbolTableEntry);
constexpr int SECTAB_SIZE = sizeof(BinarySectionTableEntry);
constexpr int RELTAB_SIZE = sizeof(BinaryRelocationTableEntry);

#endif // UTILS_STRUCTS_HPP
