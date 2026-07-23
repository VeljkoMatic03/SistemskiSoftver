#ifndef LINKER_LINKER_HPP
#define LINKER_LINKER_HPP

#include "linker/linker_types.hpp"

// Errors if any real GLOBAL symbol is defined in more than one input file. Unconditional -
// runs the same way for -hex and -relocatable. Never needs to filter SEC/LOCAL entries out:
// by construction (see aggregator.cpp), state.symbols only ever holds real user GLOBAL symbols,
// so every entry here is already a legitimate candidate for this check.
void checkMultipleDefinitions(const AggregatedState& state);

// Errors if any real GLOBAL symbol referenced somewhere is never defined in any input file.
// -hex only - under -relocatable an unresolved symbol is legal, it's simply carried forward
// as a fresh UND entry for a later link stage to resolve (see writeRelocatableObjectFile).
void checkUnresolved(const AggregatedState& state);

#endif // LINKER_LINKER_HPP
