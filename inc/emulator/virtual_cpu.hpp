#ifndef EMULATOR_VIRTUAL_CPU_HPP
#define EMULATOR_VIRTUAL_CPU_HPP

#include <cstdint>
#include <unordered_map>

struct InstructionFormat {
    uint8_t opcode;
    uint8_t mode;
    uint8_t regA;
    uint8_t regB;
    uint8_t regC;
    int32_t displ; // already sign-extended from the raw 12-bit field by decodeInstruction
};

enum class CPUState { CONTINUE, HALTED, ILLEGAL };

class VirtualCPU {
public:
    VirtualCPU();

    uint32_t getGpr(int index) const;
    void setGpr(int index, uint32_t value); // no-op for index 0 - r0 is hardwired to zero

    uint32_t getCsr(int index) const;
    void setCsr(int index, uint32_t value); // index: 0=status, 1=handler, 2=cause

    // Word-granular access - the ISA has no byte-level load/store, so this is the only
    // access width the interpreter ever needs. Dispatches to the memory-mapped registers
    // for addr >= MMIO_BASE, otherwise to the sparse byte map.
    uint32_t readWord(uint32_t addr) const;
    void writeWord(uint32_t addr, uint32_t value);

    // Raw byte access bypassing MMIO entirely - for the hex-image loader only, which must
    // never trigger a peripheral side effect (e.g. printing) while populating initial memory.
    uint8_t readRawByte(uint32_t addr) const;
    void writeRawByte(uint32_t addr, uint8_t value);

    void readInstruction();
    void decodeInstruction();
    CPUState executeInstruction();

    // Shared by every interrupt cause - int calls this itself (synchronous); the main loop
    // calls it directly for illegal (1), timer (2), and terminal (3), since those are always
    // raised from outside a single instruction's own execution. Push status, then pc, set
    // cause, globally mask, jump to handler - per the spec's general entry rule (page 13).
    // NOTE: the spec's own int-instruction pseudocode instead says "status<=status&(~0x1)",
    // which clears the Tr (timer-mask) bit rather than setting I (global mask) - contradicting
    // the general rule for what's supposed to be one instance of it. Treated as a spec
    // inconsistency; every cause uses the general rule here.
    void enterInterrupt(uint32_t causeValue);

private:
    static constexpr uint32_t MMIO_BASE = 0xFFFFFF00u;
    static constexpr uint32_t STATUS_I_BIT = 0x4; // global interrupt mask bit, see enterInterrupt

    uint32_t readMmio(uint32_t addr) const;
    void writeMmio(uint32_t addr, uint32_t value);

    // sp <= sp - 4; mem32[sp] <= value - the CPU's own implicit push, used by call/int
    // (a user-issued "push %gpr" is a plain OC=8/MOD=1 instruction and needs none of this).
    void pushWord(uint32_t value);

    CPUState executeHalt();
    CPUState executeSoftwareInterrupt();
    CPUState executeCall();
    CPUState executeJump();
    CPUState executeXchg();
    CPUState executeArithmetic();
    CPUState executeLogic();
    CPUState executeShift();
    CPUState executeStore();
    CPUState executeLoad(); // OC=9: both gpr-destination and csr-destination MOD forms

    uint32_t registerFile[16];
    uint32_t& pc = registerFile[15];
    uint32_t& sp = registerFile[14];
    uint32_t controlRegisters[3];
    uint32_t& status = controlRegisters[0];
    uint32_t& handler = controlRegisters[1];
    uint32_t& cause = controlRegisters[2];

    uint32_t ir;
    InstructionFormat instruction;
    std::unordered_map<uint32_t, uint8_t> memory;

    // term_in/tim_cfg are inert storage until the terminal/timer interrupt phase wires them
    // to something that can actually raise a pending interrupt. term_out prints immediately
    // on write since that side effect isn't interrupt-dependent at all.
    uint32_t termOut = 0;
    uint32_t termIn = 0;
    uint32_t timCfg = 0;
};

#endif // EMULATOR_VIRTUAL_CPU_HPP
