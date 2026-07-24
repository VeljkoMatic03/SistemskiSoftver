#ifndef LINKER_PLACEMENT_HPP
#define LINKER_PLACEMENT_HPP

#include "linker/linker_types.hpp"

// sets base address to every section (highly depends on LinkerOptions)
// (on -place arg to be specific)
void assignBaseAddresses(AggregatedState& state, const LinkerOptions& opts);

#endif // LINKER_PLACEMENT_HPP
