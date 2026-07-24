#include "linker/placement.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "common/errors.hpp"

namespace {

// addresses are unsigned
uint32_t u32(int value) {
    return static_cast<uint32_t>(value);
}

}

void assignBaseAddresses(AggregatedState& state, const LinkerOptions& opts) {
    if (!opts.hex) {
        for (GlobalSection& section : state.sections) {
            section.baseAddress = 0;
        }
        return;
    }

    int sectionCount = static_cast<int>(state.sections.size());
    std::vector<bool> placed(sectionCount, false);

    for (const PlaceOption& place : opts.placements) {
        auto it = state.sectionIdByName.find(place.sectionName);
        if (it == state.sectionIdByName.end()) {
            throw LinkerError("-place references unknown section '" + place.sectionName + "'");
        }
        int id = it->second;
        state.sections[id].baseAddress = place.address;
        placed[id] = true;
    }

    uint32_t nextFree = 0;
    for (int i = 0; i < sectionCount; i++) {
        if (!placed[i]) continue;
        uint32_t end = u32(state.sections[i].baseAddress)
                       + static_cast<uint32_t>(state.sections[i].data.size());
        if (end > nextFree) nextFree = end;
    }

    // sections are enumerated from 0 onwards
    for (int i = 0; i < sectionCount; i++) {
        if (placed[i]) continue;
        state.sections[i].baseAddress = static_cast<int>(nextFree);
        nextFree += static_cast<uint32_t>(state.sections[i].data.size());
    }

    std::vector<int> order(sectionCount);
    for (int i = 0; i < sectionCount; i++) order[i] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return u32(state.sections[a].baseAddress) < u32(state.sections[b].baseAddress);
    });

    for (int i = 0; i + 1 < sectionCount; i++) {
        const GlobalSection& cur = state.sections[order[i]];
        const GlobalSection& next = state.sections[order[i + 1]];
        uint32_t curEnd = u32(cur.baseAddress) + static_cast<uint32_t>(cur.data.size());
        if (curEnd > u32(next.baseAddress)) {
            throw LinkerError("sections '" + cur.name + "' and '" + next.name + "' overlap");
        }
    }
}
