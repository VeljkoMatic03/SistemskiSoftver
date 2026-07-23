#include "linker/hex_writer.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>

#include "common/errors.hpp"

namespace {

// One output line's worth of bytes: the 8-byte-aligned address range it covers, and the
// 8 bytes themselves (zero-initialized, so any byte never explicitly written defaults to
// 0x00 - exactly the "fill untouched bytes in a touched chunk with 0x00" behavior).
struct DataChunk {
    uint32_t address = 0;
    uint8_t bytes[8] = {};
    bool hasContent = false;
};

void writeChunk(const DataChunk& chunk, std::ofstream& out) {
    char addrBuf[9];
    std::snprintf(addrBuf, sizeof(addrBuf), "%08X", chunk.address);
    out << addrBuf << ":";

    for (int i = 0; i < 8; i++) {
        char byteBuf[3];
        std::snprintf(byteBuf, sizeof(byteBuf), "%02X", chunk.bytes[i]);
        out << " " << byteBuf;
    }
    out << "\n";
}

} // namespace

void writeHexImage(const AggregatedState& state, const std::string& outputPath) {
    // Sort sections by absolute base address (ascending) so every written byte, across every
    // section, gets visited in strictly non-decreasing address order in a single pass. That
    // lets us build one DataChunk at a time with a small rolling buffer instead of holding
    // every chunk in memory at once - sections are guaranteed non-overlapping (already checked
    // in assignBaseAddresses), so once we move past a chunk we never need to revisit it.
    // uint32_t (not int) for the comparison since an address like 0xC0000000 is negative as a
    // signed int - same concern as placement.cpp's overlap check.
    std::vector<int> order(state.sections.size());
    for (int i = 0; i < static_cast<int>(order.size()); i++) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&state](int a, int b) {
        return static_cast<uint32_t>(state.sections[a].baseAddress)
             < static_cast<uint32_t>(state.sections[b].baseAddress);
    });

    std::ofstream out(outputPath);
    if (!out.is_open()) {
        throw LinkerError("hex output file cannot be opened: " + outputPath);
    }

    DataChunk chunk;
    for (int sectionIndex : order) {
        const GlobalSection& section = state.sections[sectionIndex];
        uint32_t base = static_cast<uint32_t>(section.baseAddress);
        int size = static_cast<int>(section.data.size());

        for (int i = 0; i < size; i++) {
            uint32_t addr = base + static_cast<uint32_t>(i);
            uint32_t chunkAddr = addr & ~static_cast<uint32_t>(7);

            // Moved into a new chunk - flush the one we were building (if it has anything)
            // and start fresh. Two adjacent sections sharing one chunk (e.g. section A ends
            // mid-chunk and section B starts immediately after) is handled correctly here:
            // chunkAddr stays the same across that boundary, so we just keep filling the
            // SAME chunk rather than flushing - no special-casing needed for that case.
            if (chunk.hasContent && chunkAddr != chunk.address) {
                writeChunk(chunk, out);
                chunk = DataChunk{};
            }

            chunk.address = chunkAddr;
            chunk.bytes[addr & 7u] = section.data[i];
            chunk.hasContent = true;
        }
    }

    if (chunk.hasContent) {
        writeChunk(chunk, out);
    }
}
