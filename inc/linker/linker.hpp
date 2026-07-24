#ifndef LINKER_LINKER_HPP
#define LINKER_LINKER_HPP

#include "linker/linker_types.hpp"

// checks multiple global symbols that have the same name
void checkMultipleDefinitions(const AggregatedState& state);

// if any global symbol is undefined it's an error
void checkUnresolved(const AggregatedState& state);

#endif // LINKER_LINKER_HPP
