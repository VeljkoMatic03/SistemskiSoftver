#ifndef LINKER_AGGREGATOR_HPP
#define LINKER_AGGREGATOR_HPP

#include <string>
#include <vector>

#include "linker/linker_types.hpp"

// reads every .o.bin file and combines them into one singular state
AggregatedState aggregate(const std::vector<std::string>& inputPaths);

#endif // LINKER_AGGREGATOR_HPP
