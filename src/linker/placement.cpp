#include "linker/placement.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "common/errors.hpp"

namespace {

// Addresses are stored as `int` (project convention) but represent full 32-bit values - e.g.
// the spec's own worked example places a section at 0xC0000000, which is negative as a signed
// int32. Every comparison/arithmetic op on an address must go through this cast, or ordering
// and overlap checks silently break above the 0x80000000 boundary.
uint32_t u32(int value) {
    return static_cast<uint32_t>(value);
}

} // namespace

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

    // state.sections is indexed by GlobalSection::id (assigned = size() at creation time), so
    // walking the vector in order IS walking in first-appearance order - no extra sort needed.
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
