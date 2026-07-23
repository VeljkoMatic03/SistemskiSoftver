#ifndef LINKER_RELOCATOR_HPP
#define LINKER_RELOCATOR_HPP

#include "linker/linker_types.hpp"

// Computes every real GLOBAL symbol's absolute address (step 4, -hex only): finalValue =
// baseAddress of the symbol's defining section + its already section-shifted value. Only
// touches symbols with defined == true - call checkUnresolved first so an undefined symbol's
// untouched finalValue (default 0) is never mistakenly read as meaningful.
void computeFinalValues(AggregatedState& state);

void applyRelocations(AggregatedState& state);

#endif // LINKER_RELOCATOR_HPP
