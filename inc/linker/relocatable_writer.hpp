#ifndef LINKER_RELOCATABLE_WRITER_HPP
#define LINKER_RELOCATABLE_WRITER_HPP

#include <string>

#include "linker/linker_types.hpp"

// .o.bin file used to feed next iteration of linker
void writeRelocatableObjectFile(const AggregatedState& state, const std::string& outputPath);

// .o file that has dumps readable to humans
void writeRelocatableObjectFileText(const AggregatedState& state, const std::string& outputPath);

#endif // LINKER_RELOCATABLE_WRITER_HPP
