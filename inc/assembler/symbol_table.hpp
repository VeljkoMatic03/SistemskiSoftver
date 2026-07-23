#ifndef ASSEMBLER_SYMBOL_TABLE_HPP
#define ASSEMBLER_SYMBOL_TABLE_HPP

#include <string>
#include <unordered_map>
#include <vector>

#include "utils/structs.hpp"

// Holds all symbols of the assembled file. Key is the symbol name (unique within the file).
// num=0 is reserved for the UND (undefined) placeholder symbol, real symbols start at 1.
class SymbolTable {
public:
    SymbolTable();

    // Defines a symbol as a label at the current offset within the given section.
    // Throws AssemblerError if the symbol is ALREADY defined (redefinition).
    void defineLabel(const std::string& name, int sectionId, int offset, int currentLine, const std::string& lineText);

    // Handles .global or .extern - sets bind=GLOBAL.
    // If the symbol doesn't exist, creates it as isDefined=false.
    // If it exists, only changes its bind (does NOT touch isDefined).
    void declareGlobal(const std::string& name);

    // Whether the symbol exists in the table (regardless of whether it's defined).
    bool exists(const std::string& name) const;

    // Returns a reference to the symbol; creates it (as undefined, LOCAL, sectionId=-1) if it doesn't exist.
    // Used e.g. when a symbol first appears as an operand (forward reference).
    SymbolTableEntry& getOrCreate(const std::string& name);

    const SymbolTableEntry& get(const std::string& name) const;

    // Looks up a symbol by its num (needed to resolve a RelocationTableEntry's symbolId back
    // to a full entry). O(n) - fine at this scale, called only during finalizeRelocations.
    const SymbolTableEntry& getByNum(int num) const;

    // Registers the auto-generated SEC symbol for a section the first time it's opened (a
    // no-op if the section was already registered - i.e. reopened via a later .section of the
    // same name). Throws AssemblerError if a non-section symbol with this name already exists.
    void defineSection(const std::string& name, int sectionId);

    // Returns all symbols sorted by num (for serialization into the .o file).
    std::vector<SymbolTableEntry> allSortedByNum() const;

    int size() const { return table.size(); }

private:
    int allocateNum();

    std::unordered_map<std::string, SymbolTableEntry> table;
    int nextNum;
};

#endif // ASSEMBLER_SYMBOL_TABLE_HPP
