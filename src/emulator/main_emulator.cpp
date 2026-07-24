#include <cstdio>
#include <iostream>
#include <string>

#include "common/errors.hpp"
#include "emulator/hex_loader.hpp"
#include "emulator/virtual_cpu.hpp"

namespace {

void printFinalState(const VirtualCPU& cpu) {
    std::cout << "-----------------------------------------------------------------\n";
    std::cout << "Emulated processor executed halt instruction\n";
    std::cout << "Emulated processor state:\n";
    char buf[16];
    for (int i = 0; i < 16; i++) {
        std::snprintf(buf, sizeof(buf), "r%d=0x%08X", i, cpu.getGpr(i));
        std::cout << buf << (((i + 1) % 4 == 0) ? '\n' : ' ');
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "upotreba: emulator <naziv_ulazne_datoteke>\n";
        return 1;
    }
    std::string inputPath = argv[1];

    try {
        VirtualCPU cpu;
        loadHexImage(cpu, inputPath);

        while (true) {
            cpu.readInstruction();
            cpu.decodeInstruction();
            CPUState outcome = cpu.executeInstruction();

            if (outcome == CPUState::HALTED) {
                break;
            }
            if (outcome == CPUState::ILLEGAL) {
                cpu.enterInterrupt(1);
            }
        }

        printFinalState(cpu);
    } catch (const EmulatorError& e) {
        std::cerr << "Greska: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
