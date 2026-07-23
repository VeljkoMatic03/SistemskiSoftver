#ifndef LINKER_LINKER_TYPES_HPP
#define LINKER_LINKER_TYPES_HPP

#include <string>
#include <unordered_map>
#include <vector>

#include "utils/structs.hpp"

// One fully-parsed input .o (binary) file, before any aggregation/linking - exactly what
// readBinaryObjectFile produces. Everything here is still in this file's own local terms
// (symbol nums, section ids) - nothing has been merged with any other input file yet.
struct ParsedObjectFile {
    std::string path;
    std::vector<SymbolTableEntry> symbols;
    std::unordered_map<int, int> symbolByNum;      // symbol num -> index into symbols
                                                     // (num is NOT contiguous/index-aligned)
    std::vector<SectionTableEntry> sections;         // name+num, file order
    std::vector<std::vector<uint8_t>> sectionData;    // parallel to sections
    std::vector<RelocationTableEntry> relocations;
};

// A section in the linker's merged, cross-file address space - same-named sections from
// different input files are concatenated into one of these, in command-line order.
struct GlobalSection {
    std::string name;
    int id = -1;                  // order of first appearance across all input files
    std::vector<uint8_t> data;      // concatenation of every contributing file's bytes, in order
    int baseAddress = 0;             // filled in during placement (step 3)
};

// A real user GLOBAL symbol (type SYM or UND, never SEC - see object_reader.cpp/aggregator.cpp
// for why SEC entries never reach this map at all).
struct GlobalSymbol {
    std::string name;
    bool defined = false;
    int definingSectionGlobalId = -1;
    int value = 0;           // shifted offset within that global section, valid iff defined
    int finalValue = 0;       // filled in during step 4 (computeFinalValues), -hex only
    std::vector<std::string> definedInFiles; // for the multiply-defined error message
};

// (fileIndex-local) section id -> its global identity + the shift THIS file's own bytes
// incurred by landing after whatever was already in that global section.
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

// Everything produced by aggregating every input file, in command-line order (step 1).
struct AggregatedState {
    std::vector<GlobalSection> sections;
    std::unordered_map<std::string, int> sectionIdByName;
    std::unordered_map<std::string, GlobalSymbol> symbols;  // real user GLOBAL symbols only
    std::vector<LinkedRelocation> linkedRelocations;
};

#endif // LINKER_LINKER_TYPES_HPP
