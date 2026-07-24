#include "assembler/assembler.hpp"
#include "common/byte_utils.hpp"
#include "common/errors.hpp"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <unordered_map>

// if stol fails catch error and propagate our error message
long parseLiteral(const std::string& text, int lineNum, const std::string& rawLine) {
    try {
        return std::stol(text, nullptr, 0);
    } catch (const std::invalid_argument&) {
        throw AssemblerError("invalid numeric literal: '" + text + "'", lineNum, rawLine);
    } catch (const std::out_of_range&) {
        throw AssemblerError("numeric literal out of range: '" + text + "'", lineNum, rawLine);
    }
}

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
        // used for regind with displ
        if (entry.width == PatchWidth::W12_ABS) {
            uint8_t* target = sectionManager.rawPointerAt(entry.sectionId, entry.patchOffset);
            int disp = entry.addend * offset; // addend holds the sign; offset = this symbol's own value
            if (!encodeDisp12(target, disp)) {
                throw AssemblerError("value of symbol '" + name + "' doesn't fit in 12 bits", lineNum, rawLine);
            }
            continue;
        }

        bool sameSection = (entry.sectionId == sectionId);

        // directly patchable only if same section and PC-relative.
        if (entry.width == PatchWidth::W32 || !sameSection) {
            relocateEntry(name, entry);
            continue;
        }

        // for PC-relative
        uint8_t* target = sectionManager.rawPointerAt(entry.sectionId, entry.patchOffset);
        int disp = offset - (entry.patchOffset + 4) + entry.addend;
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
        throw AssemblerError(".equ not implemented", lineNum, rawLine);
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
        throw AssemblerError(".section expects exactly one argument (section name)", lineNum, rawLine);
    }
    // as we end section flush literal pool so PC-relative addressing is possible
    flushLiteralPool(lineNum, rawLine);
    sectionManager.openSection(args[0]);
    symtab.defineSection(args[0], sectionManager.currentSectionId());
}

void Assembler::handleWord(const std::vector<std::string>& args, int lineNum, const std::string& rawLine) {
    if (args.empty()) {
        throw AssemblerError(".word expects at least one initializer", lineNum, rawLine);
    }
    for (const auto& initItem : args) {
        bool isNumber = !initItem.empty() &&
                         ((initItem[0] >= '0' && initItem[0] <= '9') ||
                          (initItem[0] == '-' && initItem.size() > 1));

        int offset = sectionManager.appendZeros(4); // reserve 4 bytes, placeholder = 0

        if (isNumber) {
            int value = parseLiteral(initItem, lineNum, rawLine);
            uint8_t* target = sectionManager.rawPointerAt(sectionManager.currentSectionId(), offset);
            writeU32LE(target, value);
        } else {
            // only linker knows address of the symbol, which is effectively it's value
            // must be a relocation record
            addRelocationOrBackpatch(initItem, PatchWidth::W32, /*addend=*/0, lineNum, rawLine);
        }
    }
}

void Assembler::handleSkip(const std::vector<std::string>& args, int lineNum, const std::string& rawLine) {
    if (args.size() != 1) {
        throw AssemblerError(".skip expects exactly one argument (number of bytes)", lineNum, rawLine);
    }
    long count = parseLiteral(args[0], lineNum, rawLine);
    if (count < 0) {
        throw AssemblerError(".skip expects a non-negative number of bytes", lineNum, rawLine);
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

// =================================================================
// Relocations / backpatch
// =================================================================

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

// ==========================================================
// Instructions
// ==========================================================

// every instruction is translated into this, which is by switch-case delegated to 
// the right handler
enum class InstrShape {
    NoOp, OneOp, TwoOp, Branch, JmpCall, Load, Store, Csr
};

const std::unordered_map<std::string, InstrShape> kMnemonicShapes = {
    {"halt", InstrShape::NoOp},
    {"int", InstrShape::NoOp},
    {"push", InstrShape::OneOp},
    {"pop", InstrShape::OneOp},
    {"not", InstrShape::OneOp},
    {"xchg", InstrShape::TwoOp},
    {"add", InstrShape::TwoOp},
    {"sub", InstrShape::TwoOp},
    {"mul", InstrShape::TwoOp},
    {"div", InstrShape::TwoOp},
    {"and", InstrShape::TwoOp},
    {"or", InstrShape::TwoOp},
    {"xor", InstrShape::TwoOp},
    {"shl", InstrShape::TwoOp},
    {"shr", InstrShape::TwoOp},
    {"beq", InstrShape::Branch},
    {"bne", InstrShape::Branch},
    {"bgt", InstrShape::Branch},
    {"jmp", InstrShape::JmpCall},
    {"call", InstrShape::JmpCall},
    {"ld", InstrShape::Load},
    {"st", InstrShape::Store},
    {"csrrd", InstrShape::Csr},
    {"csrwr", InstrShape::Csr},
    {"iret", InstrShape::NoOp},
    {"ret", InstrShape::NoOp}
};

std::unordered_map<std::string, std::vector<uint8_t>> instructionOpcodes = {
    {"halt", {0x00, 0x00, 0x00, 0x00}},
    {"int", {0x10, 0x00, 0x00, 0x00}},
    {"not", {0x60, 0x00, 0x00, 0x00}},
    {"and", {0x61, 0x00, 0x00, 0x00}},
    {"or", {0x62, 0x00, 0x00, 0x00}},
    {"xor", {0x63, 0x00, 0x00, 0x00}},
    {"shl", {0x70, 0x00, 0x00, 0x00}},
    {"shr", {0x71, 0x00, 0x00, 0x00}},
    {"add", {0x50, 0x00, 0x00, 0x00}},
    {"sub", {0x51, 0x00, 0x00, 0x00}},
    {"mul", {0x52, 0x00, 0x00, 0x00}},
    {"div", {0x53, 0x00, 0x00, 0x00}},
    {"xchg", {0x40, 0x00, 0x00, 0x00}},
    {"jmp", {0x30, 0x00, 0x00, 0x00}},
    {"beq", {0x39, 0x00, 0x00, 0x00}},
    {"bne", {0x3A, 0x00, 0x00, 0x00}},
    {"bgt", {0x3B, 0x00, 0x00, 0x00}},
    {"call", {0x20, 0x00, 0x00, 0x00}},
    {"ld", {0x91, 0x00, 0x00, 0x00}},
    {"st", {0x80, 0x00, 0x00, 0x00}},
    {"csrrd", {0x90, 0x00, 0x00, 0x00}},
    {"csrwr", {0x94, 0x00, 0x00, 0x00}},
    // if we first pop pc, we will skip pop status, so we find a little hack around it
    {"iret", {0x96, 0x0E, 0x00, 0x04}},   // csr[0]<=mem32[sp+4] (status, no increment)
    {"iret2", {0x93, 0xFE, 0x00, 0x08}},  // pc<=mem32[sp]; sp<=sp+8 (pc, last - redirects control flow)
    {"ret", {0x93, 0xFE, 0x00, 0x04}},
    {"pop", {0x93, 0x0E, 0x00, 0x04}},
    {"push", {0x81, 0xE0, 0x0F, 0xFC}}
};

void patchRegA(uint8_t* instr, int regA) {
    if (regA < 0 || regA > 15) {
        throw std::invalid_argument("Invalid register number for regA: " + std::to_string(regA));
    }
    instr[1] = (instr[1] & 0x0F) | (regA << 4);
}

void patchRegB(uint8_t* instr, int regB) {
    if (regB < 0 || regB > 15) {
        throw std::invalid_argument("Invalid register number for regB: " + std::to_string(regB));
    }
    instr[1] = (instr[1] & 0xF0) | (regB & 0x0F);
}

void patchRegC(uint8_t* instr, int regC) {
    if (regC < 0 || regC > 15) {
        throw std::invalid_argument("Invalid register number for regC: " + std::to_string(regC));
    }
    instr[2] = (instr[2] & 0x0F) | (regC << 4);
}

void patchMod(uint8_t* instr, int mod) {
    instr[0] = (instr[0] & 0xF0) | (mod & 0x0F);
}


int parseRegister(const std::string& operand, int lineNum, const std::string& rawLine) {
    if (operand.empty() || operand[0] != '%') {
        throw AssemblerError("Expected register operand starting with '%', got: " + operand, lineNum, rawLine);
    }
    // %sp/%pc are aliases for %r14/%r15 - checked before the
    // '%rN' parsing below since they don't fit that shape
    if (operand == "%sp") return 14;
    if (operand == "%pc") return 15;
    if (operand.size() < 2) {
        throw AssemblerError("Invalid register format: " + operand, lineNum, rawLine);
    }
    if (operand[1] != 'r') {
        throw AssemblerError("Expected register format '%rN', got: " + operand, lineNum, rawLine);
    }
    try {
        int regNum = std::stol(operand.substr(2));
        if (regNum < 0 || regNum > 15) {
            throw AssemblerError("Register number out of range (0-15): " + std::to_string(regNum), lineNum, rawLine);
        }
        return regNum;
    } catch (const std::invalid_argument&) {
        throw AssemblerError("Invalid register format: " + operand, lineNum, rawLine);
    } catch (const std::out_of_range&) {
        throw AssemblerError("Register number out of range: " + operand, lineNum, rawLine);
    }
}

int parseCsrRegister(const std::string& operand, int lineNum, const std::string& rawLine) {
    if (operand == "%status") return 0;
    if (operand == "%handler") return 1;
    if (operand == "%cause") return 2;
    throw AssemblerError("Expected control register (%status, %handler, %cause), got: " + operand, lineNum, rawLine);
}

void Assembler::resolveDisplacement(std::string operand, int lineNum, const std::string& rawLine,
    int sign, std::vector<uint8_t>& instr) {
    bool isNumber = !operand.empty() &&
                    ((operand[0] >= '0' && operand[0] <= '9') ||
                     (operand[0] == '-' && operand.size() > 1));
    if(isNumber) {
        int value = parseLiteral(operand, lineNum, rawLine);
        value *= sign;
        if (!encodeDisp12(instr.data(), value)) {
            throw AssemblerError("literal displacement '" + operand + "' doesn't fit in 12 bits", lineNum, rawLine);
        }
        return;
    }

    // Symbol operand. Per spec: this is NEVER relocated - either its value is known by
    // the time assembly finishes, or it's a hard assembler error, regardless of .extern.
    // Also, unlike PC-relative resolution, there's no section check at all: we just want
    // the symbol's own resolved value (its offset within its own section), not a distance
    // between two locations, so it doesn't matter which section the symbol lives in.
    if (symtab.exists(operand) && symtab.get(operand).isDefined) {
        const SymbolTableEntry& sym = symtab.get(operand);
        int disp = sign * sym.value;
        if (!encodeDisp12(instr.data(), disp)) {
            throw AssemblerError("value of symbol '" + operand + "' doesn't fit in 12 bits", lineNum, rawLine);
        }
        return;
    }

    // not yet defined
    symtab.getOrCreate(operand); // guarantee a table entry exists so later resolution can find it
    BackpatchEntry entry;
    entry.sectionId = sectionManager.currentSectionId();
    entry.patchOffset = sectionManager.currentOffset(); // where this instruction will land once appended
    entry.width = PatchWidth::W12_ABS;
    entry.addend = sign; // sign multiplier (+1/-1), applied as sign * sym.value on resolve
    backpatch.add(operand, entry);
}

void Assembler::resolvePcRelativeOperand(const std::string& operand, int sectionId, int pc,
                                          int lineNum, const std::string& rawLine) {
    // bool isNumber = !operand.empty() &&
    //                 ((operand[0] >= '0' && operand[0] <= '9') ||
    //                  (operand[0] == '-' && operand.size() > 1));
    //
    // if (isNumber) {
    //     // Fully known right now: no symbol involved, and "pc" is this instruction's own
    //     // offset, which we already have. No relocation/backpatch needed at all.
    //     int value = std::stol(operand, nullptr, 0);
    //     int disp = value - (pc + 4); // -4: pc has already advanced past this instruction
    //     uint8_t* patchAt = sectionManager.rawPointerAt(sectionId, pc);
    //     if (!encodeDisp12(patchAt, disp)) {
    //         throw AssemblerError("displacement to '" + operand + "' doesn't fit in 12 bits", lineNum, rawLine);
    //     }
    //     return;
    // }

    if (symtab.exists(operand) && symtab.get(operand).isDefined) {
        const SymbolTableEntry& sym = symtab.get(operand);
        if (sym.sectionId == sectionId) {
            // if its same section then displacement is already known
            int disp = sym.value - (pc + 4);
            uint8_t* patchAt = sectionManager.rawPointerAt(sectionId, pc);
            if (!encodeDisp12(patchAt, disp)) {
                throw AssemblerError("displacement to '" + operand + "' doesn't fit in 12 bits", lineNum, rawLine);
            }
        } else {
            // Different section: value depends on where the linker places it.
            emitRelocation(sectionId, pc, RelocationType::R_PC12S, sym.num, /*addend=*/0);
        }
        return;
    }

    // not yet defined
    symtab.getOrCreate(operand); // guarantee a table entry exists so later resolution can find it
    BackpatchEntry entry;
    entry.sectionId = sectionId;
    entry.patchOffset = pc;
    entry.width = PatchWidth::W12_SIGNED;
    entry.addend = 0;
    backpatch.add(operand, entry);
}

std::string Assembler::requestPoolSlot(const std::string& value) {
    std::string label = "__pool" + std::to_string(poolCounter++);
    pendingPool.emplace_back(label, value);
    return label;
}

void Assembler::flushLiteralPool(int lineNum, const std::string& rawLine) {
    std::vector<std::pair<std::string, std::string>> pool = std::move(pendingPool);
    pendingPool.clear();

    for (const auto& poolEntry : pool) {
        const std::string& label = poolEntry.first;
        const std::string& value = poolEntry.second;

        // we treat this as if we parsed a line of this format
        // label: .word value

        handleLabel(label, lineNum, rawLine);
        handleWord({value}, lineNum, rawLine);
    }
}

// used by ld and st for immed and memdir addressing - makes entry to literal pool
// where it stores literal/symbol that is 32bit and should be stored/loaded
void Assembler::routeThroughPool(const std::string& operand, bool memoryDirect, std::vector<uint8_t>& instr,
                                  int lineNum, const std::string& rawLine) {
    // Rewrite to load through a nearby pool word: gpr[A] <= mem32[pc + offsetToPoolEntry]
    // (MOD=2, regB=pc, regC=r0 so that term vanishes). regA (the destination) must already
    // be patched by the caller before calling this.
    patchMod(instr.data(), 2);
    patchRegB(instr.data(), 15); // pc
    patchRegC(instr.data(), 0);  // r0, hardwired zero

    int sectionId = sectionManager.currentSectionId();
    int pc = sectionManager.currentOffset();
    sectionManager.appendBytes(instr);

    std::string poolLabel = requestPoolSlot(operand);
    resolvePcRelativeOperand(poolLabel, sectionId, pc, lineNum, rawLine);

    if (!memoryDirect) {
        return; // $symbol/$literal: the pool read above already produced the final value
    }

    // Bare symbol/literal (memory-direct)
    int regA = (instr[1] >> 4) & 0x0F;
    std::vector<uint8_t> deref = {instr[0], 0x00, 0x00, 0x00};
    patchMod(deref.data(), 2);
    patchRegA(deref.data(), regA);
    patchRegB(deref.data(), regA);
    patchRegC(deref.data(), 0);
    sectionManager.appendBytes(deref);
}

// halt, int, iret, ret
void Assembler::handleNoOpInstruction(const std::string& mnemonic, const std::vector<std::string>& operands,
                                     int lineNum, const std::string& rawLine)
{
    std::vector<uint8_t> instr = instructionOpcodes[mnemonic];
    sectionManager.appendBytes(instr);
    if(mnemonic == "iret") {
        instr = instructionOpcodes["iret2"];
        sectionManager.appendBytes(instr);
    }
    return; 
}

// push, pop, not
void Assembler::handleOneOpInstruction(const std::string& mnemonic, const std::vector<std::string>& operands,
                                     int lineNum, const std::string& rawLine) 
{
    if (operands.size() != 1) {
        throw AssemblerError("Instruction '" + mnemonic + "' expects exactly one operand", lineNum, rawLine);
    }
    std::vector<uint8_t> instr = instructionOpcodes[mnemonic];
    if (mnemonic == "not") {
        // gpr[A] <= ~gpr[B] (OC=0110, MOD=0000)
        int regA = parseRegister(operands[0], lineNum, rawLine);
        patchRegA(instr.data(), regA);
        patchRegB(instr.data(), regA);
        sectionManager.appendBytes(instr);
        return;
    }
    else if (mnemonic == "push") {
        // push %gpr: gpr[14] <= gpr[14] - 4; mem32[gpr[14]] <= gpr[C] (OC=1000, MOD=0001)
        int regC = parseRegister(operands[0], lineNum, rawLine);
        patchRegC(instr.data(), regC);
        sectionManager.appendBytes(instr);
        return;
    }
    else if (mnemonic == "pop") {
        // pop %gpr: gpr[A] <= mem32[gpr[14]]; gpr[14] <= gpr[14] + 4 (OC=1001, MOD=0011)
        int regA = parseRegister(operands[0], lineNum, rawLine);
        patchRegA(instr.data(), regA);
        sectionManager.appendBytes(instr);
        return;
    }
    throw AssemblerError("Instruction '" + mnemonic + "' is not yet implemented", lineNum, rawLine);
}

// xchg, add, sub, mul, div, and, or, xor, shl, shr
void Assembler::handleTwoOpInstruction(const std::string& mnemonic, const std::vector<std::string>& operands,
                                     int lineNum, const std::string& rawLine)
{
    if(operands.size() != 2) {
        throw AssemblerError("Instruction '" + mnemonic + "' expects exactly two operands", lineNum, rawLine);
    }
    std::vector<uint8_t> instr = instructionOpcodes[mnemonic];
    if(mnemonic == "xchg") {
        int register1 = parseRegister(operands[0], lineNum, rawLine);
        int register2 = parseRegister(operands[1], lineNum, rawLine);
        patchRegB(instr.data(), register1);
        patchRegC(instr.data(), register2);
        sectionManager.appendBytes(instr);
        return;
    }
    int register1 = parseRegister(operands[0], lineNum, rawLine);
    int register2 = parseRegister(operands[1], lineNum, rawLine);
    patchRegA(instr.data(), register2);
    patchRegB(instr.data(), register2);
    patchRegC(instr.data(), register1);
    sectionManager.appendBytes(instr);
}

// beq, bne, bgt
void Assembler::handleBranchInstruction(const std::string& mnemonic, const std::vector<std::string>& operands,
                                         int lineNum, const std::string& rawLine)
{
    if (operands.size() != 3) {
        throw AssemblerError("Instruction '" + mnemonic + "' expects exactly three operands", lineNum, rawLine);
    }
    const int kPcRegister = 15; // %pc is register 15
    int regB = parseRegister(operands[0], lineNum, rawLine);
    int regC = parseRegister(operands[1], lineNum, rawLine);
    const std::string& target = operands[2];

    std::vector<uint8_t> instr = instructionOpcodes[mnemonic];
    patchRegA(instr.data(), kPcRegister);
    patchRegB(instr.data(), regB);
    patchRegC(instr.data(), regC);

    int sectionId = sectionManager.currentSectionId();
    int pc = sectionManager.currentOffset(); // this instruction's own start offset
    sectionManager.appendBytes(instr);       // OC/MOD + regA/regB/regC written now, disp still 0

    // through literal pool because pc jump is absolute
    std::string literalName = requestPoolSlot(target);
    resolvePcRelativeOperand(literalName, sectionId, pc, lineNum, rawLine);
}

// call, jmp
void Assembler::handleJmpCallInstruction(const std::string& mnemonic, const std::vector<std::string>& operands,
                                          int lineNum, const std::string& rawLine)
{
    if (operands.size() != 1) {
        throw AssemblerError("Invalid number of operands for '" + mnemonic + "' instruction", lineNum, rawLine);
    }
   std::vector<uint8_t> instr = instructionOpcodes[mnemonic];
   if(mnemonic == "jmp") patchMod(instr.data(), 8);
   else patchMod(instr.data(), 1);
   patchRegA(instr.data(), 15);
   patchRegB(instr.data(), 0);
   int pc = sectionManager.currentOffset();
   int sectionId = sectionManager.currentSectionId();
   sectionManager.appendBytes(instr);
   std::string operand = operands[0];
   std::string literalName = requestPoolSlot(operand);
   resolvePcRelativeOperand(literalName, sectionId, pc, lineNum, rawLine);
}

void Assembler::handleLoadInstruction(const std::string& mnemonic, const std::vector<std::string>& operands,
                                     int lineNum, const std::string& rawLine)
{
    if(operands.size() != 2) {
        throw AssemblerError("Instruction '" + mnemonic + "' expects exactly two operands", lineNum, rawLine);
    }
    std::string target = operands[0];
    int regA = parseRegister(operands[1], lineNum, rawLine);
    std::vector<uint8_t> instr = instructionOpcodes[mnemonic];
    patchRegA(instr.data(), regA);
    if(target.size() == 0) {
        throw AssemblerError("Invalid target operand for '" + mnemonic + "' instruction", lineNum, rawLine);
    }
    if(target[0] == '%') {
        int regB = parseRegister(target, lineNum, rawLine);
        patchRegB(instr.data(), regB);
        sectionManager.appendBytes(instr);
        return;
    }
    if(target[0] == '[') {
        try {
            patchMod(instr.data(), 2);
            std::string regString = "";
            regString += target.at(1);
            regString += target.at(2);
            regString += target.at(3);
            int restIndex = 4;
            if(target.at(4) >= '0' && target.at(4) <= '9') {
                regString += target.at(4);
                restIndex = 5;
            }
            int regB = parseRegister(regString, lineNum, rawLine);
            patchRegB(instr.data(), regB);
            if(target.at(restIndex) != '+' && target.at(restIndex) != '-' && target.at(restIndex) != ']' && target.at(restIndex) != ' ') {
                throw AssemblerError("Invalid target operand for '" + mnemonic + "' instruction", lineNum, rawLine);
            }
            while(target.at(restIndex) == ' ') {
                restIndex++;
            }
            if(target.at(restIndex) == ']') {
                sectionManager.appendBytes(instr);
                return;
            }
            int sign = 1;
            if(target.at(restIndex) == '-') {
                sign = -1;
                restIndex++;
            } else if(target.at(restIndex) == '+') {
                restIndex++;
            }
            while(target.at(restIndex) == ' ') {
                restIndex++;
            }
            std::string operandString = "";
            while(target.at(restIndex) != ']') {
                operandString += target.at(restIndex);
                restIndex++;
            }
            resolveDisplacement(operandString, lineNum, rawLine, sign, instr);
            sectionManager.appendBytes(instr);
            return;
        } catch (const std::out_of_range&) {
            throw AssemblerError("malformed addressing operand: '" + target + "'", lineNum, rawLine);
        }
    }
    // $literal / $symbol -> immediate, always via the literal pool
    if(target[0] == '$') {
        std::string operandString = target.substr(1);
        routeThroughPool(operandString, /*memoryDirect=*/false, instr, lineNum, rawLine);
        return;
    }
    // bare literal / symbol -> memory-direct, always via the literal pool (see routeThroughPool).
    // this will result in two load instructions
    std::string operandString = target;
    routeThroughPool(operandString, /*memoryDirect=*/true, instr, lineNum, rawLine);
}

void Assembler::handleStoreInstruction(const std::string& mnemonic, const std::vector<std::string>& operands,
                                     int lineNum, const std::string& rawLine)
{
    if(operands.size() != 2) {
        throw AssemblerError("Instruction '" + mnemonic + "' expects exactly two operands", lineNum, rawLine);
    }
    int regSrc = parseRegister(operands[0], lineNum, rawLine);
    std::string target = operands[1];
    if(target[0] == '%' || target[0] == '$') {
        throw AssemblerError("Invalid target operand for '" + mnemonic + "' instruction", lineNum, rawLine);
    }
    std::vector<uint8_t> instr = instructionOpcodes[mnemonic];
    patchRegC(instr.data(), regSrc);
    patchRegA(instr.data(), 0);
    if(target[0] == '[') {
        try {
            patchMod(instr.data(), 0);
            std::string regString = "";
            regString += target.at(1);
            regString += target.at(2);
            regString += target.at(3);
            int restIndex = 4;
            if(target.at(4) >= '0' && target.at(4) <= '9') {
                regString += target.at(4);
                restIndex = 5;
            }
            int regB = parseRegister(regString, lineNum, rawLine);
            patchRegB(instr.data(), regB);
            if(target.at(restIndex) != '+' && target.at(restIndex) != '-' && target.at(restIndex) != ']' && target.at(restIndex) != ' ') {
                throw AssemblerError("Invalid target operand for '" + mnemonic + "' instruction", lineNum, rawLine);
            }
            while(target.at(restIndex) == ' ') {
                restIndex++;
            }
            if(target.at(restIndex) == ']') {
                sectionManager.appendBytes(instr);
                return;
            }
            int sign = 1;
            if(target.at(restIndex) == '-') {
                sign = -1;
                restIndex++;
            } else if(target.at(restIndex) == '+') {
                restIndex++;
            }
            while(target.at(restIndex) == ' ') {
                restIndex++;
            }
            std::string operandString = "";
            while(target.at(restIndex) != ']') {
                operandString += target.at(restIndex);
                restIndex++;
            }
            resolveDisplacement(operandString, lineNum, rawLine, sign, instr);
            sectionManager.appendBytes(instr);
            return;
        } catch (const std::out_of_range&) {
            throw AssemblerError("malformed addressing operand: '" + target + "'", lineNum, rawLine);
        }
    }
    patchRegB(instr.data(), 0);
    // also goes through literal pool but st has mem[mem[something]] so it results in one instruction
    // (unlike ld for memdir)
    patchMod(instr.data(), 2);
    patchRegA(instr.data(), 15); // pc (regB is already r0 from above)

    int sectionId = sectionManager.currentSectionId();
    int pc = sectionManager.currentOffset();
    sectionManager.appendBytes(instr);

    std::string poolLabel = requestPoolSlot(target);
    resolvePcRelativeOperand(poolLabel, sectionId, pc, lineNum, rawLine);
}

// csrrd, csrwr
void Assembler::handleCsrInstruction(const std::string& mnemonic, const std::vector<std::string>& operands,
                                      int lineNum, const std::string& rawLine)
{
    if (operands.size() != 2) {
        throw AssemblerError("Instruction '" + mnemonic + "' expects exactly two operands", lineNum, rawLine);
    }
    std::vector<uint8_t> instr = instructionOpcodes[mnemonic];
    if (mnemonic == "csrrd") {
        // gpr[A] <= csr[B] (ld, MOD=0000). Syntax: csrrd %csr, %gpr
        int csrB = parseCsrRegister(operands[0], lineNum, rawLine);
        int regA = parseRegister(operands[1], lineNum, rawLine);
        patchRegA(instr.data(), regA);
        patchRegB(instr.data(), csrB);
        sectionManager.appendBytes(instr);
        return;
    }
    // csrwr: csr[A] <= gpr[B] (ld, MOD=0100). Syntax: csrwr %gpr, %csr
    int regB = parseRegister(operands[0], lineNum, rawLine);
    int csrA = parseCsrRegister(operands[1], lineNum, rawLine);
    patchRegA(instr.data(), csrA);
    patchRegB(instr.data(), regB);
    sectionManager.appendBytes(instr);
}

// classic dispatcher
void Assembler::dispatchInstruction(const std::string& mnemonic, const std::vector<std::string>& operands,
                                     int lineNum, const std::string& rawLine) {
    auto it = kMnemonicShapes.find(mnemonic);
    if (it == kMnemonicShapes.end()) {
        throw AssemblerError("Instruction '" + mnemonic + "' is not yet implemented", lineNum, rawLine);
    }
    switch (it->second) {
        case InstrShape::NoOp:    handleNoOpInstruction(mnemonic, operands, lineNum, rawLine); break;
        case InstrShape::OneOp:   handleOneOpInstruction(mnemonic, operands, lineNum, rawLine); break;
        case InstrShape::TwoOp:   handleTwoOpInstruction(mnemonic, operands, lineNum, rawLine); break;
        case InstrShape::Branch:  handleBranchInstruction(mnemonic, operands, lineNum, rawLine); break;
        case InstrShape::JmpCall: handleJmpCallInstruction(mnemonic, operands, lineNum, rawLine); break;
        case InstrShape::Load:    handleLoadInstruction(mnemonic, operands, lineNum, rawLine); break;
        case InstrShape::Store:   handleStoreInstruction(mnemonic, operands, lineNum, rawLine); break;
        case InstrShape::Csr:     handleCsrInstruction(mnemonic, operands, lineNum, rawLine); break;
    }
}

// cleanup after .end
void Assembler::finalizeAssembling(int lineNum, const std::string& rawLine) {
    // if there are any literals that need to be stored, do that
    flushLiteralPool(lineNum, rawLine);

    auto remaining = backpatch.remainingSymbolNames();
    for (const auto& name : remaining) {
        auto entries = backpatch.resolveAll(name);

        // this is used for regind with displ addressing, is never relocatable
        // (defined by project)
        for (const auto& entry : entries) {
            if (entry.width == PatchWidth::W12_ABS) {
                throw AssemblerError("final value of symbol '" + name + "' is not known at assembly time", lineNum, rawLine);
            }
        }

        // GLOBAL is either global or extern, if it isnt either of those two
        // its surely undefined
        const SymbolTableEntry& sym = symtab.get(name);
        if (sym.bind != SymbolBind::GLOBAL || sym.isDefined) {
            throw AssemblerError("nedefinisan simbol: " + name, lineNum, rawLine);
        }

        // these remaining are all extern so relocation entries are created for linker
        for (const auto& entry : entries) {
            relocateEntry(name, entry);
        }
    }

    finalizeRelocations(lineNum, rawLine);
}

// only runs once
void Assembler::finalizeRelocations(int lineNum, const std::string& rawLine) {
    for (RelocationTableEntry& r : relocs) {
        const SymbolTableEntry& sym = symtab.getByNum(r.symbolId);
        if (sym.bind == SymbolBind::LOCAL) {
            // if a symbol is LOCAL it must be defined, else this is a error
            if (!sym.isDefined) {
                throw AssemblerError("undefined symbol: " + sym.name, lineNum, rawLine);
            }
            const SectionData& sec = sectionManager.getById(sym.sectionId);
            const SymbolTableEntry& sectionSym = symtab.get(sec.name);
            r.symbolId = sectionSym.num;
            r.addend += sym.value;
        }
    }
}

// ==============================================================
// Serialization 
// ==============================================================

void Assembler::writeObjectFile(const std::string& outputPath) const {
    std::ofstream out(outputPath);
    if (!out.is_open()) {
        throw AssemblerError("output file cannot be opened: " + outputPath);
    }

    out << "#SYMTAB\n";
    out << "# num name section bind value defined type\n";
    for (const auto& sym : symtab.allSortedByNum()) {
        const char* typeStr = sym.type == SymbolType::SEC ? "SEC"
                             : sym.type == SymbolType::SYM ? "SYM" : "UND";
        out << sym.num << " " << sym.name << " " << sym.sectionId << " "
            << (sym.bind == SymbolBind::GLOBAL ? "GLOBAL" : "LOCAL") << " "
            << sym.value << " " << (sym.isDefined ? "1" : "0") << " " << typeStr << "\n";
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
        int dataLen = static_cast<int>(sec.data.size());
        for (int i = 0; i < dataLen; i++) {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%02X", sec.data[i]);
            
            if((i+1) % 8 == 0) out << buf << '\n';
            else if((i+1) % 4 == 0) out << buf << "\t\t";
            else out << buf << ' ';
            //out << buf << (((i + 1) % 8 == 0) ? '\n' : ' ');
        }
        out << "\n";
    }
}

struct StringPoolEntry {
    std::string name;
    int offset;
};

void writeWord (int value, std::ofstream& out) {
    uint8_t bytes[4];
    writeU32LE(bytes, value);
    out.write(reinterpret_cast<const char*>(bytes), 4);
}

void Assembler::writeBinaryObjectFile(const std::string& outputPath) const {
    // header consists of: symtable offset (4B) symtable count (4B)
    // section tab offset (4B) section tab count (4B)
    // relocation tab offset (4B) relocation tab count (4B)
    // string pool offset (4B)
    // offset of data memory of each individual section is stored in one
    // row of section table

    std::vector<SymbolTableEntry> symbolEntries = symbolTable().allSortedByNum();
    std::vector<std::string> sectionNames = sections().order();
    std::vector<RelocationTableEntry> reltab = relocations();

    int symtabOffset = HEADER_SIZE;
    int symtabCount = static_cast<int>(symbolEntries.size());
    int sectabOffset = symtabOffset + symtabCount * SYMTAB_SIZE;
    int sectabCount = static_cast<int>(sectionNames.size());
    int reltabOffset = sectabOffset + sectabCount * SECTAB_SIZE;
    int reltabCount = static_cast<int>(reltab.size());
    // first comes raw data, after comes string pool
    int dataRegionOffset = reltabOffset + reltabCount * RELTAB_SIZE;

    // populate string pool (without duplicates)
    int currentStringOffset = 0;
    std::vector<StringPoolEntry> stringPool;
    std::unordered_map<std::string, int> stringContained;
    for (const std::string& name : sectionNames) {
        if (stringContained.find(name) == stringContained.end()) {
            stringContained[name] = stringPool.size();
            stringPool.push_back(StringPoolEntry{name, currentStringOffset});
            currentStringOffset += name.size() + 1;
        }
    }
    for (const SymbolTableEntry& sym : symbolEntries) {
        if (stringContained.find(sym.name) == stringContained.end()) {
            stringContained[sym.name] = stringPool.size();
            stringPool.push_back(StringPoolEntry{sym.name, currentStringOffset});
            currentStringOffset += sym.name.size() + 1;
        }
    }

    std::vector<BinarySymbolTableEntry> binarySymTab;
    for (const SymbolTableEntry& sym : symbolEntries) {
        BinarySymbolTableEntry bste;
        bste.num = sym.num;
        bste.sectionId = sym.sectionId;
        bste.bind = static_cast<int>(sym.bind);
        bste.value = sym.value;
        bste.defined = static_cast<int>(sym.isDefined);
        bste.type = static_cast<int>(sym.type);
        bste.nameOffset = stringPool[stringContained[sym.name]].offset;
        binarySymTab.push_back(bste);
    }

    std::vector<BinarySectionTableEntry> binarySecTab;
    int currentDataOffset = dataRegionOffset;
    for (const std::string& sectionName : sectionNames) {
        const SectionData& sd = sections().get(sectionName);
        BinarySectionTableEntry bste;
        bste.num = sd.num;
        bste.nameOffset = stringPool[stringContained[sd.name]].offset;
        bste.size = sd.data.size();
        bste.dataOffset = currentDataOffset;
        currentDataOffset += bste.size;
        binarySecTab.push_back(bste);
    }
    int stringPoolOffset = currentDataOffset; // right past the last section's raw data

    std::vector<BinaryRelocationTableEntry> binaryRelTab;
    for (const RelocationTableEntry& r : reltab) {
        BinaryRelocationTableEntry brte;
        brte.sectionId = r.sectionId;
        brte.symbolNum = r.symbolId;
        brte.offset = r.offset;
        brte.addend = r.addend;
        brte.relocationType = static_cast<int>(r.type);
        binaryRelTab.push_back(brte);
    }

    // filled string pool and all three relevant tables - now write everything out, in the
    // same order the offsets above were computed in.
    std::ofstream out(outputPath, std::ios::binary);
    if (!out.is_open()) {
        throw AssemblerError("binary output file cannot be opened: " + outputPath);
    }

    writeWord(symtabOffset, out);
    writeWord(symtabCount, out);
    writeWord(sectabOffset, out);
    writeWord(sectabCount, out);
    writeWord(reltabOffset, out);
    writeWord(reltabCount, out);
    writeWord(stringPoolOffset, out);

    for (const auto& bste : binarySymTab) {
        writeWord(bste.num, out);
        writeWord(bste.nameOffset, out);
        writeWord(bste.sectionId, out);
        writeWord(bste.bind, out);
        writeWord(bste.value, out);
        writeWord(bste.defined, out);
        writeWord(bste.type, out);
    }

    for (const auto& bste : binarySecTab) {
        writeWord(bste.num, out);
        writeWord(bste.nameOffset, out);
        writeWord(bste.size, out);
        writeWord(bste.dataOffset, out);
    }

    for (const auto& brte : binaryRelTab) {
        writeWord(brte.sectionId, out);
        writeWord(brte.symbolNum, out);
        writeWord(brte.offset, out);
        writeWord(brte.addend, out);
        writeWord(brte.relocationType, out);
    }

    for (const std::string& sectionName : sectionNames) {
        const SectionData& sd = sections().get(sectionName);
        if (!sd.data.empty()) {
            out.write(reinterpret_cast<const char*>(sd.data.data()), sd.data.size());
        }
    }

    for (const StringPoolEntry& entry : stringPool) {
        out.write(entry.name.c_str(), entry.name.size() + 1); // include the null terminator
    }
}