#include "linker/aggregator.hpp"

#include <unordered_map>

#include "common/errors.hpp"
#include "linker/object_reader.hpp"

namespace {

// makes AggregatedState out of already read files
void absorbFile(const ParsedObjectFile& file, AggregatedState& state) {
    std::unordered_map<int, SectionTranslation> translation; // local section num -> translation

    for (int i = 0; i < file.sections.size(); i++) {
        const SectionTableEntry& sec = file.sections[i];
        const std::vector<uint8_t>& data = file.sectionData[i];

        int globalSectionId;
        auto it = state.sectionIdByName.find(sec.name);
        if (it == state.sectionIdByName.end()) {
            globalSectionId = state.sections.size();
            state.sectionIdByName[sec.name] = globalSectionId;
            GlobalSection newSection;
            newSection.name = sec.name;
            newSection.id = globalSectionId;
            state.sections.push_back(newSection);
        } else {
            globalSectionId = it->second;
        }

        GlobalSection& globalSection = state.sections[globalSectionId];
        int shiftAmount = globalSection.data.size();
        globalSection.data.insert(globalSection.data.end(), data.begin(), data.end());

        translation[sec.num] = SectionTranslation{globalSectionId, shiftAmount};
    }

    // basically populate table of global symbols
    for (const SymbolTableEntry& sym : file.symbols) {
        if (sym.type == SymbolType::SEC) continue;
        if (sym.bind != SymbolBind::GLOBAL) continue;

        GlobalSymbol& globalSymbol = state.symbols[sym.name];
        globalSymbol.name = sym.name;

        if (sym.isDefined) {
            auto translationIter = translation.find(sym.sectionId);
            if (translationIter == translation.end()) {
                throw LinkerError("symbol '" + sym.name + "' in '" + file.path
                                   + "' references an unknown section id "
                                   + std::to_string(sym.sectionId));
            }
            globalSymbol.defined = true;
            globalSymbol.definingSectionGlobalId = translationIter->second.globalSectionId;
            globalSymbol.value = sym.value + translationIter->second.shiftAmount;
            globalSymbol.definedInFiles.push_back(file.path);
        }
    }

    // for relocations this is where the ultimate offset is computed
    for (const RelocationTableEntry& r : file.relocations) {
        auto symbolIter = file.symbolByNum.find(r.symbolId);
        if (symbolIter == file.symbolByNum.end()) {
            throw LinkerError("relocation in '" + file.path + "' references unknown symbol id "
                               + std::to_string(r.symbolId));
        }
        const SymbolTableEntry& sym = file.symbols[symbolIter->second];

        auto translationPatchIter = translation.find(r.sectionId);
        if (translationPatchIter == translation.end()) {
            throw LinkerError("relocation in '" + file.path + "' references unknown section id "
                               + std::to_string(r.sectionId));
        }

        LinkedRelocation lr;
        lr.patchSectionId = translationPatchIter->second.globalSectionId;
        lr.patchOffset = r.offset + translationPatchIter->second.shiftAmount;
        lr.type = r.type;

        if (sym.type == SymbolType::SEC) {
            auto targetIt = translation.find(sym.sectionId);
            if (targetIt == translation.end()) {
                throw LinkerError("relocation in '" + file.path
                                   + "' targets an unknown section id "
                                   + std::to_string(sym.sectionId));
            }
            lr.target.isSection = true;
            lr.target.globalSectionId = targetIt->second.globalSectionId;
            lr.target.sectionOffset = targetIt->second.shiftAmount + r.addend;
        } else {
            lr.target.isSection = false;
            lr.target.globalName = sym.name;
            lr.addend = r.addend;
        }

        state.linkedRelocations.push_back(lr);
    }
}

} 

AggregatedState aggregate(const std::vector<std::string>& inputPaths) {
    AggregatedState state;
    for (const std::string& path : inputPaths) {
        ParsedObjectFile file = readBinaryObjectFile(path);
        absorbFile(file, state);
    }
    return state;
}
