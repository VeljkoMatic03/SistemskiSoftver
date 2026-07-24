#ifndef EMULATOR_HEX_LOADER_HPP
#define EMULATOR_HEX_LOADER_HPP

#include <string>

#include "emulator/virtual_cpu.hpp"

// Parses a linker -hex output file ("<address>: <byte> <byte> ...", one or more lines,
// gaps between lines allowed) and writes its bytes directly into cpu's memory via
// writeRawByte - never through writeWord/MMIO, so loading never triggers a peripheral
// side effect (e.g. printing) before emulation conceptually starts.
void loadHexImage(VirtualCPU& cpu, const std::string& path);

#endif // EMULATOR_HEX_LOADER_HPP
