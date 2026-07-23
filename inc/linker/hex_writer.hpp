#ifndef LINKER_HEX_WRITER_HPP
#define LINKER_HEX_WRITER_HPP

#include <string>

#include "linker/linker_types.hpp"

// Writes AggregatedState's final, patched section bytes out as a -hex image (step 7b, level A):
// one line per 8-byte-aligned address chunk that contains at least one written byte -
// "%08X: " followed by 8 space-separated %02X bytes. Chunks with zero written bytes produce
// no output line at all (gaps between sections are simply skipped); an untouched byte inside
// an otherwise-touched chunk is filled with 0x00. Must run after assignBaseAddresses (needs
// every section's baseAddress) and applyRelocations (needs the final, patched bytes).
void writeHexImage(const AggregatedState& state, const std::string& outputPath);

#endif // LINKER_HEX_WRITER_HPP
