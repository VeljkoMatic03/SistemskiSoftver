#ifndef LINKER_RELOCATABLE_WRITER_HPP
#define LINKER_RELOCATABLE_WRITER_HPP

#include <string>

#include "linker/linker_types.hpp"

// Writes AggregatedState out as a fresh relocatable object file (step 7a, -relocatable, level
// B). Every GlobalSection becomes a symtab SEC entry (num = section.id, no lookup needed since
// GlobalSection::id is already dense 0..numSections-1) plus a sectab record; every state.symbols
// entry becomes a plain GLOBAL SYM/UND entry (num = numSections + a running index). Per-file
// LOCAL-bind symbols are NOT carried forward - nothing ever looks them up again, since
// Assembler::finalizeRelocations already rewrote every relocation to target a SEC entry instead,
// at assembly time. Every LinkedRelocation is translated back into a RelocationTableEntry,
// unresolved either way - relocations are never applied under -relocatable.
void writeRelocatableObjectFile(const AggregatedState& state, const std::string& outputPath);

// Human-readable counterpart to writeRelocatableObjectFile, same #SYMTAB/#SECTIONS/
// #RELOCATIONS/#DATA shape as Assembler::writeObjectFile - mirrors the assembler's own
// convention of writing both a text file (bare -o path) and a binary file (-o path + ".bin").
void writeRelocatableObjectFileText(const AggregatedState& state, const std::string& outputPath);

#endif // LINKER_RELOCATABLE_WRITER_HPP
