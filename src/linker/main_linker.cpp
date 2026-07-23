#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "common/errors.hpp"
#include "linker/object_reader.hpp"

// Temporary main for this development stage: reads every input file and dumps everything it
// parsed, in the same shape as Assembler::writeObjectFile's text output (plus the new symbol
// `type` column), so the reader's output can be eyeballed directly against the original .o
// text file. Aggregation/placement/relocation/CLI (-o/-hex/-relocatable/-place) come later -
// this only exercises readBinaryObjectFile.
namespace {

void dumpFile(const ParsedObjectFile& file) {
    std::cout << "#FILE " << file.path << "\n";

    std::cout << "#SYMTAB\n";
    std::cout << "# num name section bind value defined type\n";
    for (const auto& sym : file.symbols) {
        const char* bindStr = sym.bind == SymbolBind::GLOBAL ? "GLOBAL" : "LOCAL";
        const char* typeStr = sym.type == SymbolType::SEC ? "SEC"
                             : sym.type == SymbolType::SYM ? "SYM" : "UND";
        std::cout << sym.num << " " << sym.name << " " << sym.sectionId << " "
                  << bindStr << " " << sym.value << " " << (sym.isDefined ? "1" : "0")
                  << " " << typeStr << "\n";
    }

    std::cout << "#SECTIONS\n";
    std::cout << "# num name size\n";
    for (int i = 0; i < static_cast<int>(file.sections.size()); i++) {
        const SectionTableEntry& sec = file.sections[i];
        std::cout << sec.num << " " << sec.name << " " << file.sectionData[i].size() << "\n";
    }

    std::cout << "#RELOCATIONS\n";
    std::cout << "# section offset type symbol addend\n";
    for (const auto& r : file.relocations) {
        const char* typeStr = r.type == RelocationType::R_32 ? "R_32" : "R_PC12S";
        std::cout << r.sectionId << " " << r.offset << " " << typeStr << " "
                  << r.symbolId << " " << r.addend << "\n";
    }

    std::cout << "#DATA\n";
    for (int i = 0; i < static_cast<int>(file.sections.size()); i++) {
        const SectionTableEntry& sec = file.sections[i];
        const std::vector<uint8_t>& data = file.sectionData[i];
        std::cout << "#" << sec.name << "\n";
        int dataLen = static_cast<int>(data.size());
        for (int j = 0; j < dataLen; j++) {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%02X", data[j]);
            if ((j + 1) % 8 == 0) std::cout << buf << '\n';
            else if ((j + 1) % 4 == 0) std::cout << buf << "\t\t";
            else std::cout << buf << ' ';
        }
        std::cout << "\n";
    }
    std::cout << "\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "upotreba: linker <ulazna_binarna_datoteka>...\n";
        return 1;
    }

    try {
        for (int i = 1; i < argc; i++) {
            ParsedObjectFile file = readBinaryObjectFile(argv[i]);
            dumpFile(file);
        }
    } catch (const LinkerError& e) {
        std::cerr << "Greska: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
