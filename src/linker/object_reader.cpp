#include "linker/object_reader.hpp"

#include <fstream>

#include "common/byte_utils.hpp"
#include "common/errors.hpp"

// Record layouts mirror Assembler::writeBinaryObjectFile exactly (see assembler.cpp) - kept
// as local constants here rather than including assembler/assembler.hpp, to keep the linker
// decoupled from the assembler's own headers (the two components only agree on the binary
// format itself, not on each other's internal types).
namespace {

constexpr int kIntSize = 4;
constexpr int kHeaderSize = 7 * kIntSize;
constexpr int kSymbolRecordSize = 7 * kIntSize;   // num, nameOffset, sectionId, bind, value, defined, type
constexpr int kSectionRecordSize = 4 * kIntSize;  // num, nameOffset, size, dataOffset
constexpr int kRelocationRecordSize = 5 * kIntSize; // sectionId, symbolNum, offset, addend, relocationType

int readInt(const std::vector<uint8_t>& buf, int offset) {
    return readU32LE(buf.data() + offset);
}

std::string readCString(const std::vector<uint8_t>& buf, int offset) {
    std::string result;
    int i = offset;
    int size = static_cast<int>(buf.size());
    while (i < size && buf[i] != 0) {
        result += static_cast<char>(buf[i]);
        i++;
    }
    return result;
}

} // namespace

ParsedObjectFile readBinaryObjectFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        throw LinkerError("cannot open input file: " + path);
    }

    in.seekg(0, std::ios::end);
    int fileSize = static_cast<int>(in.tellg());
    in.seekg(0, std::ios::beg);

    if (fileSize < kHeaderSize) {
        throw LinkerError("input file too small to be a valid object file: " + path);
    }

    std::vector<uint8_t> buf(fileSize);
    in.read(reinterpret_cast<char*>(buf.data()), fileSize);

    int symtabOffset = readInt(buf, 0);
    int symtabCount  = readInt(buf, 4);
    int sectabOffset = readInt(buf, 8);
    int sectabCount  = readInt(buf, 12);
    int reltabOffset = readInt(buf, 16);
    int reltabCount  = readInt(buf, 20);
    int strtabOffset = readInt(buf, 24);

    ParsedObjectFile file;
    file.path = path;

    for (int i = 0; i < symtabCount; i++) {
        int off = symtabOffset + i * kSymbolRecordSize;

        SymbolTableEntry sym;
        sym.num = readInt(buf, off);
        int nameOffset = readInt(buf, off + 4);
        sym.sectionId = readInt(buf, off + 8);
        sym.bind = static_cast<SymbolBind>(readInt(buf, off + 12));
        sym.value = readInt(buf, off + 16);
        sym.isDefined = readInt(buf, off + 20) != 0;
        sym.type = static_cast<SymbolType>(readInt(buf, off + 24));
        sym.name = readCString(buf, strtabOffset + nameOffset);

        file.symbolByNum[sym.num] = file.symbols.size();
        file.symbols.push_back(sym);
    }

    for (int i = 0; i < sectabCount; i++) {
        int off = sectabOffset + i * kSectionRecordSize;

        SectionTableEntry sec;
        sec.num = readInt(buf, off);
        int nameOffset = readInt(buf, off + 4);
        int size = readInt(buf, off + 8);
        int dataOffset = readInt(buf, off + 12);
        sec.name = readCString(buf, strtabOffset + nameOffset);

        file.sections.push_back(sec);
        file.sectionData.emplace_back(buf.begin() + dataOffset, buf.begin() + dataOffset + size);
    }

    for (int i = 0; i < reltabCount; i++) {
        int off = reltabOffset + i * kRelocationRecordSize;

        RelocationTableEntry rel;
        rel.sectionId = readInt(buf, off);
        rel.symbolId = readInt(buf, off + 4);
        rel.offset = readInt(buf, off + 8);
        rel.addend = readInt(buf, off + 12);
        rel.type = static_cast<RelocationType>(readInt(buf, off + 16));

        file.relocations.push_back(rel);
    }

    return file;
}
