#include "linker/hex_writer.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>

#include "common/errors.hpp"

namespace {

// chunk of data - 8 bytes because that was in project spec
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

}

void writeHexImage(const AggregatedState& state, const std::string& outputPath) {

    // sort by base address, ascending, then write chunk by chunk
    // every chunk is flushed as soon as we move to the next
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

            // this is flushing, when we move to the next chunk
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
