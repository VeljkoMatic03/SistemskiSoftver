#ifndef ASSEMBLER_BACKPATCH_HPP
#define ASSEMBLER_BACKPATCH_HPP

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "utils/structs.hpp"

// Width of the field that needs to be filled in once the symbol is resolved.
enum class PatchWidth {
    W32,       // .word initializer - 4 bytes, outside the instruction format
    W12_SIGNED // Disp field inside an instruction
};

struct BackpatchEntry {
    int         sectionId;
    int         patchOffset;  // for W32: start of the 4 bytes; for W12_SIGNED: start of the instruction
    PatchWidth  width;
    int         addend;
};

// Holds all forward references within the current file that are STILL UNRESOLVED, keyed by symbol name.
// When a symbol is defined, resolveAll() returns all of its entries so they can be written immediately.
// Whatever remains in the table at .end/EOF must become a relocation (if the symbol is .extern)
// or be reported as an "undefined symbol" error.
class BackpatchTable {
public:
    void add(const std::string& symbolName, const BackpatchEntry& entry);

    // Returns and REMOVES all entries tied to the given symbol (called when the symbol gets defined).
    std::vector<BackpatchEntry> resolveAll(const std::string& symbolName);

    // All symbol names that remain unresolved (for the final check at .end/EOF).
    std::vector<std::string> remainingSymbolNames() const;

private:
    std::unordered_map<std::string, std::vector<BackpatchEntry>> table;
};

#endif // ASSEMBLER_BACKPATCH_HPP
