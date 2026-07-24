#ifndef LINKER_HEX_WRITER_HPP
#define LINKER_HEX_WRITER_HPP

#include <string>

#include "linker/linker_types.hpp"

// writes hex image in chunks of 8 bytes
void writeHexImage(const AggregatedState& state, const std::string& outputPath);

#endif // LINKER_HEX_WRITER_HPP
