#include "linker/relocator.hpp"

#include "common/byte_utils.hpp"
#include "common/errors.hpp"

void computeFinalValues(AggregatedState& state) {
    for (auto& kv : state.symbols) {
        GlobalSymbol& sym = kv.second;
        if (!sym.defined) continue;
        sym.finalValue = state.sections[sym.definingSectionGlobalId].baseAddress + sym.value;
    }
}

void applyRelocations(AggregatedState& state) {
    auto& sectionsVector = state.sections;
    for(auto& lr : state.linkedRelocations) {
        // relocation type is always R_32, fail if otherwise
        if (lr.type != RelocationType::R_32) {
            throw LinkerError("R_PC12S relocation patching is not implemented");
        }

        int globalSectionId = lr.patchSectionId;
        if (globalSectionId < 0 || globalSectionId >= static_cast<int>(sectionsVector.size())) {
            throw LinkerError("relocation references invalid section id " + std::to_string(globalSectionId));
        }
        auto& sectionToBePatched = sectionsVector[globalSectionId];
        std::vector<uint8_t>& dataToBePatched = sectionToBePatched.data;
        int offsetInData = lr.patchOffset;
        if (offsetInData < 0 || offsetInData + 4 > static_cast<int>(dataToBePatched.size())) {
            throw LinkerError("relocation patch offset " + std::to_string(offsetInData)
                               + " is out of bounds for section '" + sectionToBePatched.name
                               + "' (size " + std::to_string(dataToBePatched.size()) + ")");
        }

        // getting target value
        // if relocation record is for local symbol
        if(lr.target.isSection) {
            // fetch raw data from section on offset
            auto& sourceDataSection = sectionsVector[lr.target.globalSectionId];
            int sourceDataOffset = lr.target.sectionOffset;
            int baseAddressOfPatch = sourceDataSection.baseAddress;

            int patch = baseAddressOfPatch + sourceDataOffset;
            writeU32LE(dataToBePatched.data() + offsetInData, patch);

        }
        // if relocation record is for global symbol
        else {
            auto globalSymIter = state.symbols.find(lr.target.globalName);
            if(globalSymIter == state.symbols.end()) {
                throw LinkerError("relocation references undefined global symbol '"
                                   + lr.target.globalName + "'");
            }
            auto& globalSym = globalSymIter->second;
            int patch = globalSym.finalValue + lr.addend;
            writeU32LE(dataToBePatched.data() + offsetInData, patch);
        }
    }
}
