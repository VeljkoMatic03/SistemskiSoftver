#include "assembler/assembler.hpp"
#include "common/byte_utils.hpp"
#include "common/errors.hpp"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <functional>
#include <unordered_map>


void Assembler::assembleFile(const std::string& inputPath) {
    std::ifstream in(inputPath);
    if (!in.is_open()) {
        throw AssemblerError("Error: input file cannot be accessed: " + inputPath);
    }

    std::string rawLine;
    int lineNum = 0;
    while (!endDirectiveSeen && std::getline(in, rawLine)) {
        lineNum++;
        ParsedLine parsed = parseLine(rawLine, lineNum);
        processParsedLine(parsed, lineNum, rawLine);
    }

    finalizeAssembling(lineNum, rawLine);
}

void Assembler::processParsedLine(const ParsedLine& parsed, int lineNum, const std::string& rawLine) {
    if (parsed.label != "") {
        handleLabel(parsed.label, lineNum, rawLine);
    }

    if (parsed.mnemonicOrDirective == "") {
        return;
    }

    const std::string& word = parsed.mnemonicOrDirective;
    if (isDirectiveWord(word)) {
        dispatchDirective(word, parsed.rawArgs, lineNum, rawLine);
    } else {
        dispatchInstruction(word, parsed.rawArgs, lineNum, rawLine);
    }
}

void Assembler::handleLabel(const std::string& name, int lineNum, const std::string& rawLine) {
    if (!sectionManager.hasActiveSection()) {
        throw AssemblerError("Label '" + name + "' is defined out of section", lineNum, rawLine);
    }

    int sectionId = sectionManager.currentSectionId();
    int offset = sectionManager.currentOffset();

    symtab.defineLabel(name, sectionId, offset, lineNum, rawLine);

    auto entries = backpatch.resolveAll(name);
    for (const auto& entry : entries) {
        bool sameSection = (entry.sectionId == sectionId);

        // Directly patchable ONLY if same section AND PC-relative.
        // Absolute (W32) or cross-section -> value depends on the linker -> relocation.
        if (entry.width == PatchWidth::W32 || !sameSection) {
            relocateEntry(name, entry);
            continue;
        }

        // W12_SIGNED, same section: displacement is fully known now.
        uint8_t* target = sectionManager.rawPointerAt(entry.sectionId, entry.patchOffset);
        int disp = offset - entry.patchOffset // + instruction length if PC = next instruction
                 + entry.addend;
        if (!encodeDisp12(target, disp)) {
            throw AssemblerError("displacement to '" + name + "' doesn't fit in 12 bits", lineNum, rawLine);
        }
    }
}

// ============================================================
// Directives
// ============================================================

void Assembler::dispatchDirective(const std::string& name, const std::vector<std::string>& args,
                                   int lineNum, const std::string& rawLine) {
    if (name == ".global" || name == ".extern") {
        handleGlobalOrExtern(args);
    } else if (name == ".section") {
        handleSection(args, lineNum, rawLine);
    } else if (name == ".word") {
        handleWord(args, lineNum, rawLine);
    } else if (name == ".skip") {
        handleSkip(args, lineNum, rawLine);
    } else if (name == ".ascii") {
        handleAscii(args, lineNum, rawLine);
    } else if (name == ".end") {
        endDirectiveSeen = true;
    } else if (name == ".equ") {
        throw AssemblerError(".equ nije implementirano u ovoj verziji", lineNum, rawLine);
    } else {
        throw AssemblerError("Unknown directive: " + name, lineNum, rawLine);
    }
}

void Assembler::handleGlobalOrExtern(const std::vector<std::string>& args) {
    for (const auto& name : args) {
        symtab.declareGlobal(name);
    }
}

void Assembler::handleSection(const std::vector<std::string>& args, int lineNum, const std::string& rawLine) {
    if (args.size() != 1) {
        throw AssemblerError(".section ocekuje tacno jedan argument (ime sekcije)", lineNum, rawLine);
    }
    sectionManager.openSection(args[0]);
}

void Assembler::handleWord(const std::vector<std::string>& args, int lineNum, const std::string& rawLine) {
    if (args.empty()) {
        throw AssemblerError(".word ocekuje bar jedan inicijalizator", lineNum, rawLine);
    }
    for (const auto& initItem : args) {
        // Is the initializer a literal (number) or a symbol?
        bool isNumber = !initItem.empty() &&
                         ((initItem[0] >= '0' && initItem[0] <= '9') ||
                          (initItem[0] == '-' && initItem.size() > 1));

        int offset = sectionManager.appendZeros(4); // reserve 4 bytes, placeholder = 0

        if (isNumber) {
            int value = std::stoi(initItem, nullptr, 0);
            uint8_t* target = sectionManager.rawPointerAt(sectionManager.currentSectionId(), offset);
            writeU32LE(target, value);
        } else {
            // Symbol - even if it's already defined in THIS file, the final address is still
            // computed by the linker (the assembler doesn't know a section's base address) -
            // so we ALWAYS emit a relocation for .word + symbol.
            addRelocationOrBackpatch(initItem, PatchWidth::W32, /*addend=*/0, lineNum, rawLine);
        }
    }
}

void Assembler::handleSkip(const std::vector<std::string>& args, int lineNum, const std::string& rawLine) {
    if (args.size() != 1) {
        throw AssemblerError(".skip ocekuje tacno jedan argument (broj bajtova)", lineNum, rawLine);
    }
    long count = std::stol(args[0], nullptr, 0);
    if (count < 0) {
        throw AssemblerError(".skip ocekuje nenegativan broj bajtova", lineNum, rawLine);
    }
    sectionManager.appendZeros(static_cast<int>(count));
}

void Assembler::handleAscii(const std::vector<std::string>& args, int lineNum, const std::string& rawLine) {
    if (args.size() != 1) {
        throw AssemblerError(".ascii ocekuje tacno jedan string argument", lineNum, rawLine);
    }
    const std::string& raw = args[0];
    if (raw.size() < 2 || raw.front() != '"' || raw.back() != '"') {
        throw AssemblerError(".ascii argument mora biti string u navodnicima", lineNum, rawLine);
    }
    std::string content = raw.substr(1, raw.size() - 2);
    std::vector<uint8_t> bytes(content.begin(), content.end());
    sectionManager.appendBytes(bytes);
}

// ============================================================
// Relocations / backpatch
// ============================================================

void Assembler::emitRelocation(int sectionId, int offset, RelocationType type, int symbolId, int addend) {
    RelocationTableEntry reloc;
    reloc.sectionId = sectionId;
    reloc.offset = offset;
    reloc.type = type;
    reloc.symbolId = symbolId;
    reloc.addend = addend;
    relocs.push_back(reloc);
}

void Assembler::relocateEntry(const std::string& name, const BackpatchEntry& entry) {
    const SymbolTableEntry& sym = symtab.get(name);
    RelocationType relType = (entry.width == PatchWidth::W32) ? RelocationType::R_32 : RelocationType::R_PC12S;
    emitRelocation(entry.sectionId, entry.patchOffset, relType, sym.num, entry.addend);
}

void Assembler::addRelocationOrBackpatch(const std::string& symbolName, PatchWidth width,
                                          int addend, int lineNum, const std::string& rawLine) {
    (void)lineNum;
    (void)rawLine;

    int sectionId = sectionManager.currentSectionId();
    int offset = sectionManager.currentOffset() - 4; // both widths reserve 4 bytes today

    RelocationType relType = (width == PatchWidth::W32) ? RelocationType::R_32 : RelocationType::R_PC12S;

    SymbolTableEntry& sym = symtab.getOrCreate(symbolName);

    emitRelocation(sectionId, offset, relType, sym.num, addend);
}

// ============================================================
// Instructions - TODO: fill in the dispatch table (shape handlers) in the next step
// ============================================================

void Assembler::dispatchInstruction(const std::string& mnemonic, const std::vector<std::string>& operands,
                                     int lineNum, const std::string& rawLine) {
    (void)operands;
    // TODO: this is where the unordered_map<string, InstrDef> dispatch table from our discussion goes
    // (shape handler + OC/MOD parameters). Just halt as a placeholder for now:
    if (mnemonic == "halt") {
        std::vector<uint8_t> instr = {0x00, 0x00, 0x00, 0x00};
        sectionManager.appendBytes(instr);
        return;
    }
    throw AssemblerError("instrukcija '" + mnemonic + "' jos nije implementirana", lineNum, rawLine);
}

// ============================================================
// Finalization
// ============================================================

void Assembler::finalizeAssembling(int lineNum, const std::string& rawLine) {
    auto remaining = backpatch.remainingSymbolNames();
    for (const auto& name : remaining) {
        const SymbolTableEntry& sym = symtab.get(name);
        if (sym.bind != SymbolBind::GLOBAL || sym.isDefined) {
            throw AssemblerError("nedefinisan simbol: " + name, lineNum, rawLine);
        }
        // symbol is .extern and still undefined here - every pending reference to it
        // must be resolved by the linker instead.
        auto entries = backpatch.resolveAll(name);
        for (const auto& entry : entries) {
            relocateEntry(name, entry);
        }
    }
}

// ============================================================
// Serialization (TODO - fill in according to the agreed-upon format)
// ============================================================

void Assembler::writeObjectFile(const std::string& outputPath) const {
    std::ofstream out(outputPath);
    if (!out.is_open()) {
        throw AssemblerError("ne mogu da otvorim izlaznu datoteku: " + outputPath);
    }

    out << "#SYMTAB\n";
    out << "# num name section bind value defined\n";
    for (const auto& sym : symtab.allSortedByNum()) {
        out << sym.num << " " << sym.name << " " << sym.sectionId << " "
            << (sym.bind == SymbolBind::GLOBAL ? "GLOBAL" : "LOCAL") << " "
            << sym.value << " " << (sym.isDefined ? "1" : "0") << "\n";
    }

    out << "#SECTIONS\n";
    out << "# num name size\n";
    for (const auto& secName : sectionManager.order()) {
        const SectionData& sec = sectionManager.get(secName);
        out << sec.num << " " << sec.name << " " << sec.data.size() << "\n";
    }

    out << "#RELOCATIONS\n";
    out << "# section offset type symbol addend\n";
    for (const auto& r : relocs) {
        out << r.sectionId << " " << r.offset << " "
            << (r.type == RelocationType::R_32 ? "R_32" : "R_PC12S") << " "
            << r.symbolId << " " << r.addend << "\n";
    }

    out << "#DATA\n";
    for (const auto& secName : sectionManager.order()) {
        const SectionData& sec = sectionManager.get(secName);
        out << "#" << sec.name << "\n";
        int dataLen = static_cast<int>(sec.data.size()); // vector::size() is size_t, narrowed once here
        for (int i = 0; i < dataLen; i++) {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%02X", sec.data[i]);
            out << buf << (((i + 1) % 8 == 0) ? '\n' : ' ');
        }
        out << "\n";
    }
}
