#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "common/errors.hpp"
#include "linker/aggregator.hpp"
#include "linker/cli.hpp"
#include "linker/linker.hpp"
#include "linker/object_reader.hpp"
#include "linker/placement.hpp"

// Temporary main for this development stage: parses real CLI flags, reads+aggregates every
// input file, checks multiple definitions, and assigns base addresses - then dumps everything
// it has so far (relocations are still unapplied, symbols' finalValue still unset - those come
// in steps 4/6). writeHexImage/writeRelocatableObjectFile/checkUnresolved not implemented yet.
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

void dumpAggregated(const AggregatedState& state) {
    std::cout << "#AGGREGATED\n";

    std::cout << "#GLOBAL_SECTIONS\n";
    std::cout << "# id name size baseAddress\n";
    for (const auto& sec : state.sections) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%08X", static_cast<unsigned>(sec.baseAddress));
        std::cout << sec.id << " " << sec.name << " " << sec.data.size() << " " << buf << "\n";
    }

    std::cout << "#GLOBAL_SYMBOLS\n";
    std::cout << "# name defined section value definedInFiles\n";
    for (const auto& kv : state.symbols) {
        const GlobalSymbol& sym = kv.second;
        std::cout << sym.name << " " << (sym.defined ? "1" : "0") << " "
                  << sym.definingSectionGlobalId << " " << sym.value << " [";
        for (int i = 0; i < static_cast<int>(sym.definedInFiles.size()); i++) {
            if (i > 0) std::cout << ",";
            std::cout << sym.definedInFiles[i];
        }
        std::cout << "]\n";
    }

    std::cout << "#LINKED_RELOCATIONS\n";
    std::cout << "# patchSection patchOffset type target\n";
    for (const auto& lr : state.linkedRelocations) {
        const char* typeStr = lr.type == RelocationType::R_32 ? "R_32" : "R_PC12S";
        std::cout << lr.patchSectionId << " " << lr.patchOffset << " " << typeStr << " ";
        if (lr.target.isSection) {
            std::cout << "SEC(section=" << lr.target.globalSectionId
                       << ", offset=" << lr.target.sectionOffset << ")\n";
        } else {
            std::cout << "GLOBAL(name=" << lr.target.globalName
                       << ", addend=" << lr.addend << ")\n";
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        LinkerOptions opts = parseArgs(argc, argv);

        for (const std::string& path : opts.inputPaths) {
            ParsedObjectFile file = readBinaryObjectFile(path);
            dumpFile(file);
        }

        AggregatedState state = aggregate(opts.inputPaths);
        checkMultipleDefinitions(state);
        assignBaseAddresses(state, opts);
        dumpAggregated(state);
    } catch (const LinkerError& e) {
        std::cerr << "Greska: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
