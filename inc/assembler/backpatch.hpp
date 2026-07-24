#ifndef ASSEMBLER_BACKPATCH_HPP
#define ASSEMBLER_BACKPATCH_HPP

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "utils/structs.hpp"

// Width of the field that needs to be filled in once the symbol is resolved.
enum class PatchWidth {
    W32,        // .word initializer - 4 bytes, outside the instruction format
    W12_SIGNED, // Disp field inside an instruction, PC-relative (branch/jmp/call targets) -
                // may become an R_PC12S relocation if still unresolved (.extern) at EOF
    W12_ABS     // Disp field inside an instruction, NOT PC-relative (e.g. [reg + symbol]) -
                // never becomes a relocation; must resolve locally by EOF or it's an
                // assembler error, regardless of .extern/.global
};

struct BackpatchEntry {
    int         sectionId;
    int         patchOffset;  // for W32/W12_SIGNED: start of the 4 bytes/instruction; same for W12_ABS
    PatchWidth  width;
    int         addend;       // additive constant for W32/W12_SIGNED; sign multiplier (+1/-1) for W12_ABS
};

class BackpatchTable {
public:
    void add(const std::string& symbolName, const BackpatchEntry& entry);

    // Returns and removes all entries tied to the given symbol (called when the symbol gets defined).
    std::vector<BackpatchEntry> resolveAll(const std::string& symbolName);

    // all symbol names that remain unresolved (for the final check at .end/EOF).
    std::vector<std::string> remainingSymbolNames() const;

private:
    std::unordered_map<std::string, std::vector<BackpatchEntry>> table;
};

#endif // ASSEMBLER_BACKPATCH_HPP
