#include <cstdio>
#include <iostream>
#include <string>

#include "common/errors.hpp"
#include "emulator/hex_loader.hpp"
#include "emulator/terminal.hpp"
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
        Terminal terminal;

        while (true) {
            cpu.readInstruction();
            cpu.decodeInstruction();
            CPUState outcome = cpu.executeInstruction();

            if (outcome == CPUState::HALTED) {
                break;
            }
            if (outcome == CPUState::ILLEGAL) {
                // Illegal instruction is unconditional - never gated by the mask bits,
                // unlike timer/terminal below.
                cpu.enterInterrupt(1);
                continue;
            }
            // Interrupts are only ever serviced between instructions, never mid-instruction -
            // this check runs once per loop iteration, after the instruction that just
            // executed has fully completed. `int` itself (cause=4) is synchronous and handled
            // entirely inside executeInstruction(), so it never appears here.
            if (terminal.hasPending() && !cpu.isGloballyMasked() && !cpu.isTerminalMasked()) {
                cpu.setTerminalInput(terminal.takePending());
                cpu.enterInterrupt(3);
            }
        }

        printFinalState(cpu);
    } catch (const EmulatorError& e) {
        std::cerr << "Greska: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
