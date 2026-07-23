#include "linker/relocatable_writer.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/byte_utils.hpp"
#include "common/errors.hpp"
#include "utils/structs.hpp"

// Record layouts mirror Assembler::writeBinaryObjectFile exactly (see assembler.cpp) - kept as
// local types here rather than including assembler/assembler.hpp, to keep the linker decoupled
// from the assembler's own headers (the two components only agree on the binary format itself).
namespace {

struct BinarySymbolTableEntry {
    int num;
    int nameOffset;
    int sectionId;
    int bind;
    int value;
    int defined;
    int type;
};

struct BinarySectionTableEntry {
    int num;
    int nameOffset;
    int size;
    int dataOffset;
};

struct BinaryRelocationTableEntry {
    int sectionId;
    int symbolNum;
    int offset;
    int addend;
    int relocationType;
};

struct StringPoolEntry {
    std::string name;
    int offset;
};

constexpr int HEADER_SIZE = 7 * sizeof(int);
constexpr int SYMTAB_SIZE = sizeof(BinarySymbolTableEntry);
constexpr int SECTAB_SIZE = sizeof(BinarySectionTableEntry);
constexpr int RELTAB_SIZE = sizeof(BinaryRelocationTableEntry);

void writeWord(int value, std::ofstream& out) {
    uint8_t bytes[4];
    writeU32LE(bytes, value);
    out.write(reinterpret_cast<const char*>(bytes), 4);
}

} // namespace

void writeRelocatableObjectFile(const AggregatedState& state, const std::string& outputPath) {
    // header
    int symtabOffset = HEADER_SIZE;
    int symtabCount = static_cast<int>(state.sections.size() + state.symbols.size());
    int sectabOffset = symtabOffset + symtabCount * SYMTAB_SIZE;
    int sectabCount = static_cast<int>(state.sections.size());
    int reltabOffset = sectabOffset + sectabCount * SECTAB_SIZE;
    int reltabCount = static_cast<int>(state.linkedRelocations.size());

    int dataRegionOffset = reltabOffset + reltabCount * RELTAB_SIZE;

    int currentStringOffset = 0;
    std::vector<StringPoolEntry> stringPool;
    std::unordered_map<std::string, int> stringContained;
    for (auto& section : state.sections) {
        std::string name = section.name;
        if (stringContained.find(name) == stringContained.end()) {
            stringContained[name] = stringPool.size();
            stringPool.push_back(StringPoolEntry{name, currentStringOffset});
            currentStringOffset += name.size() + 1;
        }
    }
    for (const auto& kv : state.symbols) {
        const GlobalSymbol& sym = kv.second;
        if (stringContained.find(sym.name) == stringContained.end()) {
            stringContained[sym.name] = stringPool.size();
            stringPool.push_back(StringPoolEntry{sym.name, currentStringOffset});
            currentStringOffset += sym.name.size() + 1;
        }
    }

    // symtab
    int numSections = static_cast<int>(state.sections.size());
    std::vector<BinarySymbolTableEntry> binarySymTab;
    for (const auto& section : state.sections) {
        BinarySymbolTableEntry bste;
        bste.num = section.id;
        bste.nameOffset = stringPool[stringContained[section.name]].offset;
        bste.sectionId = section.id;
        bste.bind = static_cast<int>(SymbolBind::GLOBAL);
        bste.value = 0;
        bste.defined = 1;
        bste.type = static_cast<int>(SymbolType::SEC);
        binarySymTab.push_back(bste);
    }

    // real GLOBAL symbols (defined or still-undefined - -relocatable never requires full
    // resolution), numbered right after the sections. symbolNumByName records each one's
    // assigned num for the reltab pass below, since GlobalSymbol carries no num of its own.
    std::unordered_map<std::string, int> symbolNumByName;
    int runningIndex = 0;
    for (const auto& kv : state.symbols) {
        const GlobalSymbol& sym = kv.second;

        BinarySymbolTableEntry bste;
        bste.num = numSections + runningIndex;
        bste.nameOffset = stringPool[stringContained[sym.name]].offset;
        bste.sectionId = sym.defined ? sym.definingSectionGlobalId : -1;
        bste.bind = static_cast<int>(SymbolBind::GLOBAL);
        bste.value = sym.defined ? sym.value : 0;
        bste.defined = sym.defined ? 1 : 0;
        bste.type = static_cast<int>(sym.defined ? SymbolType::SYM : SymbolType::UND);
        binarySymTab.push_back(bste);

        symbolNumByName[sym.name] = bste.num;
        runningIndex++;
    }

    // sectab
    std::vector<BinarySectionTableEntry> binarySecTab;
    int currentDataOffset = dataRegionOffset;
    for (const auto& section : state.sections) {
        BinarySectionTableEntry bste;
        bste.num = section.id;
        bste.nameOffset = stringPool[stringContained[section.name]].offset;
        bste.size = static_cast<int>(section.data.size());
        bste.dataOffset = currentDataOffset;
        currentDataOffset += bste.size;
        binarySecTab.push_back(bste);
    }
    int stringPoolOffset = currentDataOffset; // right past the last section's raw data

    // reltab
    std::vector<BinaryRelocationTableEntry> binaryRelTab;
    for (const LinkedRelocation& lr : state.linkedRelocations) {
        BinaryRelocationTableEntry brte;
        brte.sectionId = lr.patchSectionId;
        brte.offset = lr.patchOffset;
        brte.relocationType = static_cast<int>(lr.type);

        if (lr.target.isSection) {
            brte.symbolNum = lr.target.globalSectionId;
            brte.addend = lr.target.sectionOffset;
        } else {
            brte.symbolNum = symbolNumByName.at(lr.target.globalName);
            brte.addend = lr.addend;
        }

        binaryRelTab.push_back(brte);
    }
    
    // raw data and output to file
    std::ofstream out(outputPath, std::ios::binary);
    if (!out.is_open()) {
        throw LinkerError("relocatable output file cannot be opened: " + outputPath);
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

    for (const auto& section : state.sections) {
        if (!section.data.empty()) {
            out.write(reinterpret_cast<const char*>(section.data.data()), section.data.size());
        }
    }

    for (const StringPoolEntry& entry : stringPool) {
        out.write(entry.name.c_str(), entry.name.size() + 1); // include the null terminator
    }
}

// Same #SYMTAB/#SECTIONS/#RELOCATIONS/#DATA shape as Assembler::writeObjectFile - rebuilds the
// same joint symtab (SEC entries first, then real GLOBAL symbols) as writeRelocatableObjectFile
// above, just formatted as text instead of packed into binary records.
void writeRelocatableObjectFileText(const AggregatedState& state, const std::string& outputPath) {
    std::ofstream out(outputPath);
    if (!out.is_open()) {
        throw LinkerError("relocatable output file cannot be opened: " + outputPath);
    }

    int numSections = static_cast<int>(state.sections.size());

    out << "#SYMTAB\n";
    out << "# num name section bind value defined type\n";
    for (const auto& section : state.sections) {
        out << section.id << " " << section.name << " " << section.id << " "
            << "GLOBAL" << " " << 0 << " " << 1 << " " << "SEC" << "\n";
    }

    std::unordered_map<std::string, int> symbolNumByName;
    int runningIndex = 0;
    for (const auto& kv : state.symbols) {
        const GlobalSymbol& sym = kv.second;
        int num = numSections + runningIndex;
        int sectionId = sym.defined ? sym.definingSectionGlobalId : -1;
        int value = sym.defined ? sym.value : 0;
        const char* typeStr = sym.defined ? "SYM" : "UND";

        out << num << " " << sym.name << " " << sectionId << " "
            << "GLOBAL" << " " << value << " " << (sym.defined ? "1" : "0")
            << " " << typeStr << "\n";

        symbolNumByName[sym.name] = num;
        runningIndex++;
    }

    out << "#SECTIONS\n";
    out << "# num name size\n";
    for (const auto& section : state.sections) {
        out << section.id << " " << section.name << " " << section.data.size() << "\n";
    }

    out << "#RELOCATIONS\n";
    out << "# section offset type symbol addend\n";
    for (const auto& lr : state.linkedRelocations) {
        int symbolNum;
        int addend;
        if (lr.target.isSection) {
            symbolNum = lr.target.globalSectionId;
            addend = lr.target.sectionOffset;
        } else {
            symbolNum = symbolNumByName.at(lr.target.globalName);
            addend = lr.addend;
        }

        out << lr.patchSectionId << " " << lr.patchOffset << " "
            << (lr.type == RelocationType::R_32 ? "R_32" : "R_PC12S") << " "
            << symbolNum << " " << addend << "\n";
    }

    out << "#DATA\n";
    for (const auto& section : state.sections) {
        out << "#" << section.name << "\n";
        int dataLen = static_cast<int>(section.data.size());
        for (int i = 0; i < dataLen; i++) {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%02X", section.data[i]);
            if ((i + 1) % 8 == 0) out << buf << '\n';
            else if ((i + 1) % 4 == 0) out << buf << "\t\t";
            else out << buf << ' ';
        }
        out << "\n";
    }
}
