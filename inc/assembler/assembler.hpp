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
    Assembler() : endDirectiveSeen(false) {}

    // goes line by line and calls parseLine for each line
    // throws AssemblerError
    void assembleFile(const std::string& inputPath);

    // dumps into .o file
    void writeObjectFile(const std::string& outputPath) const;

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

    // instruction dispatcher which fans out to specific handlers
    void dispatchInstruction(const std::string& mnemonic, const std::vector<std::string>& operands,
                              int lineNum, const std::string& rawLine);

    // final steps (symbol/relocation table checks)
    void finalizeAssembling(int lineNum, const std::string& rawLine);

    // appends a relocation entry - used both for .word initializers (addRelocationOrBackpatch)
    // and for backpatch entries that turn out to need linker resolution (handleLabel)
    void emitRelocation(int sectionId, int offset, RelocationType type, int symbolId, int addend);

    void addRelocationOrBackpatch(const std::string& symbolName, PatchWidth width,
                                   int addend, int lineNum, const std::string& rawLine);

    // converts one pending backpatch entry for the given symbol into a relocation
    // (used when the value can't be patched directly - cross-section, W32, or still
    // unresolved at EOF for an .extern symbol)
    void relocateEntry(const std::string& name, const BackpatchEntry& entry);

    SymbolTable symtab;
    SectionManager sectionManager;
    BackpatchTable backpatch;
    std::vector<RelocationTableEntry> relocs;
    bool endDirectiveSeen;
};

#endif // ASSEMBLER_ASSEMBLER_HPP
