#ifndef LINKER_OBJECT_READER_HPP
#define LINKER_OBJECT_READER_HPP

#include <string>

#include "linker/linker_types.hpp"

// Reads one binary object file produced by Assembler::writeBinaryObjectFile.
// Throws LinkerError on any I/O or format problem.
ParsedObjectFile readBinaryObjectFile(const std::string& path);

#endif // LINKER_OBJECT_READER_HPP
