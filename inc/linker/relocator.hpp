#ifndef LINKER_RELOCATOR_HPP
#define LINKER_RELOCATOR_HPP

#include "linker/linker_types.hpp"

// used only with -hex arg, computes final value which is baseAddress combined with it's previous value
void computeFinalValues(AggregatedState& state);

void applyRelocations(AggregatedState& state);

#endif // LINKER_RELOCATOR_HPP
