#ifndef EMULATOR_HEX_LOADER_HPP
#define EMULATOR_HEX_LOADER_HPP

#include <string>

#include "emulator/virtual_cpu.hpp"

// parses .hex file and loads everything in emulated memory
void loadHexImage(VirtualCPU& cpu, const std::string& path);

#endif // EMULATOR_HEX_LOADER_HPP
