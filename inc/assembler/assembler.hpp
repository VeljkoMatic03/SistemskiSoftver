#ifndef ASSEMBLER_ASSEMBLER_HPP
#define ASSEMBLER_ASSEMBLER_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "assembler/backpatch.hpp"
#include "assembler/lexer.hpp"
#include "assembler/section_manager.hpp"
#include "assembler/symbol_table.hpp"
#include "utils/structs.hpp"


class Assembler {
public:
    Assembler() : poolCounter(0), endDirectiveSeen(false) {}

    // goes line by line and calls parseLine for each line
    // throws AssemblerError
    void assembleFile(const std::string& inputPath);

    // dumps into .o file
    void writeObjectFile(const std::string& outputPath) const;
    void writeBinaryObjectFile(const std::string& outputPath) const;

    const SymbolTable& symbolTable() const { return symtab; }
    const SectionManager& sections() const { return sectionManager; }
    const std::vector<RelocationTableEntry>& relocations() const { return relocs; }

private:
    // process of parsed line
    void processParsedLine(const ParsedLine& parsed, int lineNum, const std::string& rawLine);

    // label handler (checkup with SymbolTable)
    void handleLabel(const std::string& name, int lineNum, const std::string& rawLine);

    // directives dispatcher which fans out to specific handlers
    void dispatchDirective(const std::string& name, const std::vector<std::string>& args,
                            int lineNum, const std::string& rawLine);
    void handleGlobalOrExtern(const std::vector<std::string>& args);
    void handleSection(const std::vector<std::string>& args, int lineNum, const std::string& rawLine);
    void handleWord(const std::vector<std::string>& args, int lineNum, const std::string& rawLine);
    void handleSkip(const std::vector<std::string>& args, int lineNum, const std::string& rawLine);
    void handleAscii(const std::vector<std::string>& args, int lineNum, const std::string& rawLine);

    // instruction dispatcher which fans out to specific shape handlers
    void dispatchInstruction(const std::string& mnemonic, const std::vector<std::string>& operands,
                              int lineNum, const std::string& rawLine);

    // shape handlers - one per instruction encoding shape, not one per mnemonic
    void handleNoOpInstruction(const std::string& mnemonic, const std::vector<std::string>& operands,
                                int lineNum, const std::string& rawLine); // halt, int, iret, ret
    void handleOneOpInstruction(const std::string& mnemonic, const std::vector<std::string>& operands,
                                 int lineNum, const std::string& rawLine); // push, pop, not
    void handleTwoOpInstruction(const std::string& mnemonic, const std::vector<std::string>& operands,
                                 int lineNum, const std::string& rawLine); // xchg, add, sub, mul, div, and, or, xor, shl, shr
    void handleBranchInstruction(const std::string& mnemonic, const std::vector<std::string>& operands,
                                  int lineNum, const std::string& rawLine); // beq, bne, bgt
    void handleJmpCallInstruction(const std::string& mnemonic, const std::vector<std::string>& operands,
                                   int lineNum, const std::string& rawLine); // jmp, call
    void handleLoadInstruction(const std::string& mnemonic, const std::vector<std::string>& operands,
                                int lineNum, const std::string& rawLine); // ld
    void handleStoreInstruction(const std::string& mnemonic, const std::vector<std::string>& operands,
                                 int lineNum, const std::string& rawLine); // st
    void handleCsrInstruction(const std::string& mnemonic, const std::vector<std::string>& operands,
                               int lineNum, const std::string& rawLine); // csrrd, csrwr

    // final steps (symbol/relocation table checks)
    void finalizeAssembling(int lineNum, const std::string& rawLine);

    // Rewrites every relocation that targets a LOCAL symbol to reference that symbol's own
    // SEC entry instead, folding the symbol's own offset into the addend. Also the only place
    // that catches an undefined symbol referenced solely via .word (which emits its relocation
    // directly, bypassing the backpatch table entirely - see addRelocationOrBackpatch), so it
    // needs lineNum/rawLine to report that error properly instead of crashing. Runs once, at the
    // very end (after every .global/label has been processed and every symbol's final bind
    // is settled) - a symbol's bind can still change via a .global appearing anywhere in the
    // file, including after the symbol was used in a relocation, so this can't be decided
    // eagerly at the point each relocation is first created.
    void finalizeRelocations(int lineNum, const std::string& rawLine);

    // appends a relocation entry - used both for .word initializers (addRelocationOrBackpatch)
    // and for backpatch entries that turn out to need linker resolution (handleLabel)
    void emitRelocation(int sectionId, int offset, RelocationType type, int symbolId, int addend);

    void addRelocationOrBackpatch(const std::string& symbolName, PatchWidth width,
                                   int addend, int lineNum, const std::string& rawLine);

    // converts one pending backpatch entry for the given symbol into a relocation
    // (used when the value can't be patched directly - cross-section, W32, or still
    // unresolved at EOF for an .extern symbol)
    void relocateEntry(const std::string& name, const BackpatchEntry& entry);

    // resolves a PC-relative operand (literal or symbol) for an already-appended 4-byte
    // instruction at (sectionId, pc): patches the Disp field directly if the target is
    // known now, or emits a relocation / backpatch entry if it isn't. Shared by every
    // jump-family shape handler (branch, jmp/call, ...).
    void resolvePcRelativeOperand(const std::string& operand, int sectionId, int pc,
                                   int lineNum, const std::string& rawLine);

    // resolves the displacement portion of a [reg +/- operand] addressing mode into the
    // NOT-YET-appended instruction bytes. operand may be a literal or a symbol; sign is
    // +1 or -1 depending on whether it was "+" or "-" in the source. Never emits a
    // relocation - see PatchWidth::W12_ABS.
    void resolveDisplacement(std::string operand, int lineNum, const std::string& rawLine,
                              int sign, std::vector<uint8_t>& instr);

    // resolves a $literal/$symbol (immediate, memoryDirect=false) or bare literal/symbol
    // (memory-direct, memoryDirect=true) operand for ld/st: ALWAYS routes through the
    // literal pool, unconditionally - never encodes directly into the 12-bit Disp field,
    // even when the value would fit. instr is NOT yet appended; this function owns
    // appending it either way. If memoryDirect, also appends a second instruction to
    // dereference through the loaded value, since the pool read only recovers the
    // symbol's/literal's raw address, not what's stored there.
    void routeThroughPool(const std::string& operand, bool memoryDirect, std::vector<uint8_t>& instr,
                           int lineNum, const std::string& rawLine);

    // registers a pending literal-pool entry (synthetic label -> literal/symbol text) and
    // returns the synthetic label name, to be referenced via resolvePcRelativeOperand.
    std::string requestPoolSlot(const std::string& value);

    // defines each pending pool entry's synthetic label at the current position and emits
    // a .word for it (reusing handleWord's own relocation-aware logic), then clears the
    // list. Must run before the active section changes, and at EOF - see handleSection
    // and finalizeAssembling.
    void flushLiteralPool(int lineNum, const std::string& rawLine);

    SymbolTable symtab;
    SectionManager sectionManager;
    BackpatchTable backpatch;
    std::vector<RelocationTableEntry> relocs;
    std::vector<std::pair<std::string, std::string>> pendingPool; // (syntheticLabel, literalOrSymbolText)
    int poolCounter;
    bool endDirectiveSeen;
};


#endif // ASSEMBLER_ASSEMBLER_HPP
