#ifndef LINKER_CLI_HPP
#define LINKER_CLI_HPP

#include "linker/linker_types.hpp"

// Parses argv into a LinkerOptions. Throws LinkerError on any usage problem (missing -o,
// neither/both of -hex/-relocatable given, malformed -place=name@address, no input files).
// -place is accepted and stored even under -relocatable - per spec it's silently ignored in
// that mode later on, not rejected here.
LinkerOptions parseArgs(int argc, char** argv);

#endif // LINKER_CLI_HPP
