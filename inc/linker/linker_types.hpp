#ifndef LINKER_LINKER_TYPES_HPP
#define LINKER_LINKER_TYPES_HPP

#include <string>
#include <unordered_map>
#include <vector>

#include "utils/structs.hpp"

// result of parsing one .o file (everything in one place)
struct ParsedObjectFile {
    std::string path;
    std::vector<SymbolTableEntry> symbols;
    std::unordered_map<int, int> symbolByNum;      // symbol num -> index into symbols
                                                     // (num is NOT contiguous/index-aligned)
    std::vector<SectionTableEntry> sections;         // name+num, file order
    std::vector<std::vector<uint8_t>> sectionData;    // parallel to sections
    std::vector<RelocationTableEntry> relocations;
};

// every section is global, just an upgrade on normal section struct
struct GlobalSection {
    std::string name;
    int id = -1;                  // order of first appearance across all input files
    std::vector<uint8_t> data;      // concatenation of every contributing file's bytes, in order
    int baseAddress = 0;             // filled in during placement
};

// only for global symbols (sections don't count)
struct GlobalSymbol {
    std::string name;
    bool defined = false;
    int definingSectionGlobalId = -1;
    int value = 0;           // shifted offset within that global section, valid only if defined
    int finalValue = 0;       // filled in during step:computeFinalValues, -hex only
    std::vector<std::string> definedInFiles; // for the multiply-defined error message
};

// (fileIndex-local) section id -> its global identity + the shift this file's own bytes
// incurred by landing after whatever was already in that global section
struct SectionTranslation {
    int globalSectionId = -1;
    int shiftAmount = 0;
};

// A relocation's target, resolved (SEC) or deferred (real GLOBAL symbol) at aggregation time.
struct RelocationTarget {
    bool isSection = false;
    int globalSectionId = -1;   // valid iff isSection
    int sectionOffset = 0;       // valid iff isSection - shift + addend already folded in
    std::string globalName;       // valid iff !isSection
};

struct LinkedRelocation {
    int patchSectionId = -1;   // global section id containing the instruction/.word being patched
    int patchOffset = 0;        // offset within that section (already shifted)
    RelocationType type = RelocationType::R_32;
    int addend = 0;              // only meaningful when !target.isSection (SEC case already
                                   // folded its addend into target.sectionOffset)
    RelocationTarget target;
};

struct PlaceOption {
    std::string sectionName;
    int address = 0;
};

struct LinkerOptions {
    std::vector<std::string> inputPaths;
    std::string outputPath;
    bool relocatable = false;
    bool hex = false;
    std::vector<PlaceOption> placements;
};

// everything produced by aggregating every input file, in command-line order
struct AggregatedState {
    std::vector<GlobalSection> sections;
    std::unordered_map<std::string, int> sectionIdByName;
    std::unordered_map<std::string, GlobalSymbol> symbols;  // real user global symbols only
    std::vector<LinkedRelocation> linkedRelocations;
};

#endif // LINKER_LINKER_TYPES_HPP
