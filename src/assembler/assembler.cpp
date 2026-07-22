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
        if (entry.width == PatchWidth::W12_ABS) {
            // Never PC-relative, never relocated (see PatchWidth::W12_ABS) - just this
            // symbol's own resolved value, regardless of which section it lives in.
            uint8_t* target = sectionManager.rawPointerAt(entry.sectionId, entry.patchOffset);
            int disp = entry.addend * offset; // addend holds the sign; offset = this symbol's own value
            if (!encodeDisp12(target, disp)) {
                throw AssemblerError("value of symbol '" + name + "' doesn't fit in 12 bits", lineNum, rawLine);
            }
            continue;
        }

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
    // Flush into the section we're LEAVING, before switching - pool entries always need
    // to land in the same section as the instructions that reference them (a no-op if
    // nothing requested a pool slot since the last flush, including the very first
    // .section in a file, when pendingPool is necessarily still empty).
    flushLiteralPool(lineNum, rawLine);
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
// Instructions
// ============================================================

// One tag per instruction encoding shape - dispatchInstruction maps a mnemonic to its
// shape, then calls the one Assembler method that knows how to encode that shape.
enum class InstrShape {
    NoOp, OneOp, TwoOp, Branch, JmpCall, Load, Store, Csr
};

const std::unordered_map<std::string, InstrShape> kMnemonicShapes = {
    {"halt", InstrShape::NoOp},
    {"int", InstrShape::NoOp},
    // iret/ret intentionally omitted: no opcode bytes decided yet (see instructionOpcodes
    // below) and handleNoOpInstruction has no fallback for a missing template - leaving
    // them out of this map means they correctly fall through to "not yet implemented"
    // instead of silently appending zero bytes for a mnemonic instructionOpcodes doesn't have.
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
    {"int", {0x01, 0x00, 0x00, 0x00}},
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
    {"beq", {0x31, 0x00, 0x00, 0x00}},
    {"bne", {0x32, 0x00, 0x00, 0x00}},
    {"bgt", {0x33, 0x00, 0x00, 0x00}},
    {"call", {0x20, 0x00, 0x00, 0x00}},
    {"ld", {0x91, 0x00, 0x00, 0x00}},
    {"st", {0x80, 0x00, 0x00, 0x00}},
    {"csrrd", {0x90, 0x00, 0x00, 0x00}}, // csrrd/csrwr are MOD variants of ld (OC=9), not their own OC
    {"csrwr", {0x94, 0x00, 0x00, 0x00}},
    {"iret", {0x93, 0xFE, 0x00, 0x04}},
    {"iret2", {0x97, 0x0E, 0x00, 0x04}},
    {"ret", {0x93, 0xFE, 0x00, 0x04}}, // just pop pc
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
    if (operand.size() < 2) {
        throw AssemblerError("Invalid register format: " + operand, lineNum, rawLine);
    }
    if (operand[1] != 'r') {
        throw AssemblerError("Expected register format '%rN', got: " + operand, lineNum, rawLine);
    }
    try {
        int regNum = std::stoi(operand.substr(2));
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
    // Is the operand a literal (number) or a symbol?
    bool isNumber = !operand.empty() &&
                    ((operand[0] >= '0' && operand[0] <= '9') ||
                     (operand[0] == '-' && operand.size() > 1));
    if(isNumber) {
        int value = std::stoi(operand, nullptr, 0);
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

    // Not defined yet - defer via the existing backpatch mechanism. handleLabel will
    // resolve it directly (same formula, sign * value) the moment the label is defined;
    // if it's still unresolved at .end/EOF, finalizeAssembling reports it as an error -
    // never as a relocation, since a plain (non-PC-relative) 12-bit value can't survive
    // being handed off to the linker (see PatchWidth::W12_ABS).
    symtab.getOrCreate(operand); // guarantee a table entry exists so later resolution can find it
    BackpatchEntry entry;
    entry.sectionId = sectionManager.currentSectionId();
    entry.patchOffset = sectionManager.currentOffset(); // where THIS instruction will land once appended
    entry.width = PatchWidth::W12_ABS;
    entry.addend = sign; // sign multiplier (+1/-1), applied as sign * sym.value on resolve
    backpatch.add(operand, entry);
}

void Assembler::resolvePcRelativeOperand(const std::string& operand, int sectionId, int pc,
                                          int lineNum, const std::string& rawLine) {
    // Is the operand a literal (number) or a symbol?
    bool isNumber = !operand.empty() &&
                    ((operand[0] >= '0' && operand[0] <= '9') ||
                     (operand[0] == '-' && operand.size() > 1));

    if (isNumber) {
        // Fully known right now: no symbol involved, and "pc" is this instruction's own
        // offset, which we already have. No relocation/backpatch needed at all.
        int value = std::stoi(operand, nullptr, 0);
        int disp = value - pc;
        uint8_t* patchAt = sectionManager.rawPointerAt(sectionId, pc);
        if (!encodeDisp12(patchAt, disp)) {
            throw AssemblerError("displacement to '" + operand + "' doesn't fit in 12 bits", lineNum, rawLine);
        }
        return;
    }

    // Symbol operand - mirrors the same three-way split handleLabel already applies when
    // a label gets defined, just evaluated here at reference time instead of definition time.
    if (symtab.exists(operand) && symtab.get(operand).isDefined) {
        const SymbolTableEntry& sym = symtab.get(operand);
        if (sym.sectionId == sectionId) {
            // Same section as this instruction: displacement is fully known now.
            int disp = sym.value - pc;
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

    // Not defined yet (forward reference, or not seen at all) - defer until the symbol is
    // defined (handleLabel will resolve it) or, if it's .extern, until EOF (finalizeAssembling).
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
    // Copy first: handleLabel()/handleWord() below can themselves append bytes and move
    // section state around, and neither of them touches pendingPool, but iterating a
    // member we're about to clear is asking for trouble if that ever changes.
    std::vector<std::pair<std::string, std::string>> pool = std::move(pendingPool);
    pendingPool.clear();

    for (const auto& poolEntry : pool) {
        const std::string& label = poolEntry.first;
        const std::string& value = poolEntry.second;

        // Defines the synthetic label at the current end-of-section offset - this is
        // exactly the same bookkeeping a real user-written label goes through, which is
        // what resolves the backpatch entry that resolvePcRelativeOperand created back
        // when this pool slot was requested (see routeThroughPool).
        handleLabel(label, lineNum, rawLine);

        // Reuses .word's own logic verbatim: a numeric value is written directly, a
        // symbol always gets a proper R_32 relocation (handles extern/cross-section).
        handleWord({value}, lineNum, rawLine);
    }
}

void Assembler::resolveWideOperand(const std::string& operand, bool memoryDirect, int lineNum,
                                    const std::string& rawLine, std::vector<uint8_t>& instr) {
    // Is the operand a literal (number) or a symbol?
    bool isNumber = !operand.empty() &&
                    ((operand[0] >= '0' && operand[0] <= '9') ||
                     (operand[0] == '-' && operand.size() > 1));

    if (isNumber) {
        int value = std::stoi(operand, nullptr, 0);
        if (encodeDisp12(instr.data(), value)) {
            sectionManager.appendBytes(instr);
            return;
        }
        routeThroughPool(operand, memoryDirect, instr, lineNum, rawLine);
        return;
    }

    // Symbol: ALWAYS goes through the pool, unconditionally - there is no direct/fast
    // path here at all. sym.value is only ever a section-relative offset computed before
    // the linker has placed that section anywhere; the true value this addressing mode
    // needs is sectionBase + sym.value, and sectionBase is never known to the assembler,
    // for any symbol, local or extern. Encoding sym.value directly would be wrong even
    // when it happens to fit in 12 bits - this isn't a range problem, the quantity itself
    // is the wrong one. (Contrast [reg + symbol]'s resolveDisplacement, where the symbol
    // genuinely means its own offset, not an address - a different addressing mode with a
    // different meaning for "symbol".)
    routeThroughPool(operand, memoryDirect, instr, lineNum, rawLine);
}

void Assembler::routeThroughPool(const std::string& operand, bool memoryDirect, std::vector<uint8_t>& instr,
                                  int lineNum, const std::string& rawLine) {
    // Rewrite to load through a nearby pool word: gpr[A] <= mem32[pc + offsetToPoolEntry]
    // (MOD=2, regB=pc, regC=r0 so that term vanishes). regA (the destination) was already
    // patched by the caller before resolveWideOperand was called.
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

    // Bare symbol/literal (memory-direct): the pool read only recovered the resolved
    // address into regA, not what's stored there - dereference it with a second
    // instruction. Same OC (ld) as the first, MOD=2, regA=regB=regA (base is now the
    // value we just loaded), regC=r0, Disp=0 (no further offset needed).
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
        // gpr[A] <= ~gpr[B] (OC=0110, MOD=0000). Assembly-level "not %gpr" has only one
        // operand but reads and writes the same register, so regA and regB both need it.
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

    resolvePcRelativeOperand(target, sectionId, pc, lineNum, rawLine);
}

// call, jmp
void Assembler::handleJmpCallInstruction(const std::string& mnemonic, const std::vector<std::string>& operands,
                                          int lineNum, const std::string& rawLine)
{
    if (operands.size() != 1) {
        throw AssemblerError("Invalid number of operands for '" + mnemonic + "' instruction", lineNum, rawLine);
    }
    const int kPcRegister = 15;    // %pc is register 15 - same base register branch uses
    const int kUnusedRegister = 0; // unconditional jump: no compare operand, regB is don't-care
    const std::string& target = operands[0];

    std::vector<uint8_t> instr = instructionOpcodes[mnemonic];
    patchRegA(instr.data(), kPcRegister);
    patchRegB(instr.data(), kUnusedRegister);

    int sectionId = sectionManager.currentSectionId();
    int pc = sectionManager.currentOffset();
    sectionManager.appendBytes(instr);

    resolvePcRelativeOperand(target, sectionId, pc, lineNum, rawLine);
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
        patchMod(instr.data(), 2);
        std::string regString = "";
        regString += target[1];
        regString += target[2];
        regString += target[3];
        int restIndex = 4;
        if(target[4] >= '0' && target[4] <= '9') {
            regString += target[4];
            restIndex = 5;
        }
        int regB = parseRegister(regString, lineNum, rawLine);
        patchRegB(instr.data(), regB);
        if(target[restIndex] != '+' && target[restIndex] != '-' && target[restIndex] != ']' && target[restIndex] != ' ') {
            throw AssemblerError("Invalid target operand for '" + mnemonic + "' instruction", lineNum, rawLine);
        }
        while(target[restIndex] == ' ') {
            restIndex++;
        }
        if(target[restIndex] == ']') {
            sectionManager.appendBytes(instr);
            return;
        }
        int sign = 1;
        if(target[restIndex] == '-') {
            sign = -1;
            restIndex++;
        } else if(target[restIndex] == '+') {
            restIndex++;
        }
        while(target[restIndex] == ' ') {
            restIndex++;
        }
        std::string operandString = "";
        while(target[restIndex] != ']') {
            operandString += target[restIndex];
            restIndex++;
        }
        resolveDisplacement(operandString, lineNum, rawLine, sign, instr);
        sectionManager.appendBytes(instr);
        return;
    }
    // $literal / $symbol -> immediate: gpr[A] <= D (MOD stays at the template default, 1;
    // regB=r0 so gpr[A]<=gpr[B]+D collapses to gpr[A]<=D)
    if(target[0] == '$') {
        std::string operandString = target.substr(1);
        patchRegB(instr.data(), 0);
        resolveWideOperand(operandString, /*memoryDirect=*/false, lineNum, rawLine, instr);
        return;
    }
    // bare literal / symbol -> memory-direct: gpr[A] <= mem32[D] (MOD=2, regB=regC=r0 so
    // gpr[A]<=mem32[gpr[B]+gpr[C]+D] collapses to gpr[A]<=mem32[D])
    std::string operandString = target;
    patchRegB(instr.data(), 0);
    patchMod(instr.data(), 2);
    resolveWideOperand(operandString, /*memoryDirect=*/true, lineNum, rawLine, instr);
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
        patchMod(instr.data(), 0);
        std::string regString = "";
        regString += target[1];
        regString += target[2];
        regString += target[3];
        int restIndex = 4;
        if(target[4] >= '0' && target[4] <= '9') {
            regString += target[4];
            restIndex = 5;
        }
        int regB = parseRegister(regString, lineNum, rawLine);
        patchRegB(instr.data(), regB);
        if(target[restIndex] != '+' && target[restIndex] != '-' && target[restIndex] != ']' && target[restIndex] != ' ') {
            throw AssemblerError("Invalid target operand for '" + mnemonic + "' instruction", lineNum, rawLine);
        }
        while(target[restIndex] == ' ') {
            restIndex++;
        }
        if(target[restIndex] == ']') {
            sectionManager.appendBytes(instr);
            return;
        }
        int sign = 1;
        if(target[restIndex] == '-') {
            sign = -1;
            restIndex++;
        } else if(target[restIndex] == '+') {
            restIndex++;
        }
        while(target[restIndex] == ' ') {
            restIndex++;
        }
        std::string operandString = "";
        while(target[restIndex] != ']') {
            operandString += target[restIndex];
            restIndex++;
        }
        resolveDisplacement(operandString, lineNum, rawLine, sign, instr);
        sectionManager.appendBytes(instr);
        return;
    }
    patchRegB(instr.data(), 0);
    // check if operand is a literal smaller than 12 bits
    bool isNumber = !target.empty() &&
                    ((target[0] >= '0' && target[0] <= '9') ||
                     (target[0] == '-' && target.size() > 1));
    if(isNumber) {
        int value = std::stoi(target, nullptr, 0);
        if(value >= -2048 && value <= 2047) {
            patchMod(instr.data(), 0);
            encodeDisp12(instr.data(), value);
            sectionManager.appendBytes(instr);
            return;
        }
    }
    // Too big for a direct literal, or a symbol (symbols always go through the pool here,
    // unconditionally - see resolveWideOperand: a symbol's own offset is never the same
    // thing as its true final address, no matter how small). Unlike ld, st's own MOD=0010
    // is a NATIVE double dereference (mem32[mem32[gpr[A]+gpr[B]+D]]<=gpr[C]), so this needs
    // only ONE instruction - and regC (already regSrc, set above) must stay untouched,
    // since it's the value being written, not scratch space for the address lookup.
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

// ============================================================
// Finalization
// ============================================================

void Assembler::finalizeAssembling(int lineNum, const std::string& rawLine) {
    // Resolve any still-pending pool references (defines their synthetic labels, which
    // resolves the backpatch entries resolvePcRelativeOperand created for them) before
    // scanning for leftover unresolved symbols below - otherwise a pool label would look
    // like an ordinary undefined LOCAL symbol and get reported as an error.
    flushLiteralPool(lineNum, rawLine);

    auto remaining = backpatch.remainingSymbolNames();
    for (const auto& name : remaining) {
        auto entries = backpatch.resolveAll(name);

        // W12_ABS entries are never relocatable - if one is still here, the symbol's final
        // value was never known by the time assembly finished, which the spec requires to
        // be a hard error, regardless of .extern/.global.
        for (const auto& entry : entries) {
            if (entry.width == PatchWidth::W12_ABS) {
                throw AssemblerError("final value of symbol '" + name + "' is not known at assembly time", lineNum, rawLine);
            }
        }

        const SymbolTableEntry& sym = symtab.get(name);
        if (sym.bind != SymbolBind::GLOBAL || sym.isDefined) {
            throw AssemblerError("nedefinisan simbol: " + name, lineNum, rawLine);
        }
        // symbol is .extern and still undefined here - every remaining (PC-relative or
        // absolute-word) reference to it must be resolved by the linker instead.
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
        throw AssemblerError("output file cannot be opened: " + outputPath);
    }

    out << "#SYMTAB\n";
    out << "#\tnum\tname\t\tsection\tbind\t\tvalue\t\t\tdefined\n";
    for (const auto& sym : symtab.allSortedByNum()) {
        out << sym.num << " " << sym.name << " " << sym.sectionId << " "
            << (sym.bind == SymbolBind::GLOBAL ? "GLOBAL" : "LOCAL") << " "
            << sym.value << " " << (sym.isDefined ? "1" : "0") << "\n";
    }

    out << "#SECTIONS\n";
    out << "#\tnum\tname\t\tsize\n";
    for (const auto& secName : sectionManager.order()) {
        const SectionData& sec = sectionManager.get(secName);
        out << sec.num << " " << sec.name << " " << sec.data.size() << "\n";
    }

    out << "#RELOCATIONS\n";
    out << "#\tsection\toffset\ttype\t\tsymbol\taddend\n";
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
