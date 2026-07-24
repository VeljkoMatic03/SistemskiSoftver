#ifndef LINKER_CLI_HPP
#define LINKER_CLI_HPP

#include "linker/linker_types.hpp"

// parsing args and making LinkerOptions object out of that
LinkerOptions parseArgs(int argc, char** argv);

#endif // LINKER_CLI_HPP
