#ifndef LINKER_PLACEMENT_HPP
#define LINKER_PLACEMENT_HPP

#include "linker/linker_types.hpp"

// Fills in state.sections[*].baseAddress (step 3).
// -relocatable: every section starts at 0; -place is silently ignored (per spec, not an error).
// -hex: -place'd sections get their pinned address; remaining sections default-place back to
// back, starting right after the highest placed section's end, walked in GlobalSection::id
// order (= first-appearance order across all input files). Throws LinkerError if a -place
// names an unknown section, or if any two sections end up overlapping.
void assignBaseAddresses(AggregatedState& state, const LinkerOptions& opts);

#endif // LINKER_PLACEMENT_HPP
