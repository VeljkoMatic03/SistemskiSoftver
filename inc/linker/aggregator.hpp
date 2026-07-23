#ifndef LINKER_AGGREGATOR_HPP
#define LINKER_AGGREGATOR_HPP

#include <string>
#include <vector>

#include "linker/linker_types.hpp"

// Reads and aggregates every input file, in command-line order, into one merged state:
// concatenates same-named sections across files, merges GLOBAL symbol definitions (shifting
// each one's value by its own section's accumulated growth), and resolves every relocation's
// target - immediately, for a SEC (section) target, or deferred by name, for a real GLOBAL
// symbol whose defining file may not be known yet. Throws LinkerError on any read/format
// problem, or if a relocation references a symbol/section id that doesn't exist in its own file.
AggregatedState aggregate(const std::vector<std::string>& inputPaths);

#endif // LINKER_AGGREGATOR_HPP
