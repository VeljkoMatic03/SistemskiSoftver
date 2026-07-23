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
        // The assembler never emits anything but R_32 (see resolvePcRelativeOperand/relocateEntry -
        // every PC-relative target is now routed through the literal pool as a plain absolute
        // word). R_PC12S patching is deliberately not implemented - fail loudly instead of
        // silently mispatching if one ever shows up.
        if (lr.type != RelocationType::R_32) {
            throw LinkerError("R_PC12S relocation patching is not implemented");
        }

        int globalSectionId = lr.patchSectionId;
        auto& sectionToBePatched = sectionsVector[globalSectionId];
        std::vector<uint8_t>& dataToBePatched = sectionToBePatched.data;
        int offsetInData = lr.patchOffset;

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
