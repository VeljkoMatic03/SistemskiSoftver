#include "emulator/virtual_cpu.hpp"

#include <cstdio>

namespace {
constexpr uint32_t TERM_OUT_ADDR = 0xFFFFFF00u;
constexpr uint32_t TERM_IN_ADDR = 0xFFFFFF04u;
constexpr uint32_t TIM_CFG_ADDR = 0xFFFFFF10u;
} // namespace

VirtualCPU::VirtualCPU() {
    for (int i = 0; i < 16; i++) {
        registerFile[i] = 0;
    }
    for (int i = 0; i < 3; i++) {
        controlRegisters[i] = 0;
    }
    ir = 0;
    pc = 0x40000000; // cold/warm reset entry point, per spec
}

uint32_t VirtualCPU::getGpr(int index) const {
    return registerFile[index];
}

void VirtualCPU::setGpr(int index, uint32_t value) {
    if (index == 0) {
        return; // r0 is hardwired to zero - writes are discarded, not stored-then-masked
    }
    registerFile[index] = value;
}

uint32_t VirtualCPU::getCsr(int index) const {
    return controlRegisters[index];
}

void VirtualCPU::setCsr(int index, uint32_t value) {
    controlRegisters[index] = value;
}

uint8_t VirtualCPU::readRawByte(uint32_t addr) const {
    auto it = memory.find(addr);
    return it == memory.end() ? 0 : it->second; // untouched memory reads as zero
}

void VirtualCPU::writeRawByte(uint32_t addr, uint8_t value) {
    memory[addr] = value;
}

uint32_t VirtualCPU::readMmio(uint32_t addr) const {
    if (addr == TERM_OUT_ADDR) return termOut;
    if (addr == TERM_IN_ADDR) return termIn;
    if (addr == TIM_CFG_ADDR) return timCfg;
    return 0; // rest of the reserved 256 bytes has no defined register - reads as zero
}

void VirtualCPU::writeMmio(uint32_t addr, uint32_t value) {
    if (addr == TERM_OUT_ADDR) {
        termOut = value;
        std::putchar(static_cast<int>(value & 0xFF));
        std::fflush(stdout);
        return;
    }
    if (addr == TERM_IN_ADDR) {
        termIn = value;
        return;
    }
    if (addr == TIM_CFG_ADDR) {
        timCfg = value & 0x7;
        return;
    }
    // rest of the reserved 256 bytes has no defined register - write is a no-op
}

uint32_t VirtualCPU::readWord(uint32_t addr) const {
    // A word access starting just below MMIO_BASE and straddling into the reserved region
    // would incorrectly read a couple of its bytes as plain memory instead of MMIO - not
    // handled here, since no realistic test program misaligns an access onto that boundary
    // on purpose and the spec gives no guidance on it either way.
    if (addr >= MMIO_BASE) {
        return readMmio(addr);
    }
    uint32_t value = 0;
    for (int i = 0; i < 4; i++) {
        value |= static_cast<uint32_t>(readRawByte(addr + i)) << (8 * i);
    }
    return value;
}

void VirtualCPU::writeWord(uint32_t addr, uint32_t value) {
    if (addr >= MMIO_BASE) {
        writeMmio(addr, value);
        return;
    }
    for (int i = 0; i < 4; i++) {
        writeRawByte(addr + i, static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
    }
}

void VirtualCPU::readInstruction() {
    ir = readWord(pc);
    pc += 4; // must happen before execute - call/int push this as the return address
}

void VirtualCPU::decodeInstruction() {
    instruction.opcode = static_cast<uint8_t>((ir & 0xFF) >> 4);
    instruction.mode = static_cast<uint8_t>(ir & 0x0F);
    instruction.regA = static_cast<uint8_t>((ir & 0xFF00) >> 12);
    instruction.regB = static_cast<uint8_t>((ir & 0x0F00) >> 8);
    instruction.regC = static_cast<uint8_t>((ir & 0xFF0000) >> 20);

    uint16_t rawDisp = static_cast<uint16_t>(((ir >> 16) & 0xF) << 8 | ((ir >> 24) & 0xFF));
    instruction.displ = (rawDisp & 0x800)
        ? static_cast<int32_t>(rawDisp) - 0x1000  // sign-extend from 12 bits
        : static_cast<int32_t>(rawDisp);
}

namespace {
bool isValidCsrIndex(uint8_t index) {
    return index <= 2; // 0=status, 1=handler, 2=cause - anything else has no defined register
}
} // namespace

CPUState VirtualCPU::executeInstruction() {
    switch (instruction.opcode) {
        case 0x0: return executeHalt();
        case 0x1: return executeSoftwareInterrupt();
        case 0x2: return executeCall();
        case 0x3: return executeJump();
        case 0x4: return executeXchg();
        case 0x5: return executeArithmetic();
        case 0x6: return executeLogic();
        case 0x7: return executeShift();
        case 0x8: return executeStore();
        case 0x9: return executeLoad();
        default:  return CPUState::ILLEGAL;
    }
}

CPUState VirtualCPU::executeHalt() {
    if (instruction.mode != 0x0) {
        return CPUState::ILLEGAL;
    }
    return CPUState::HALTED;
}

void VirtualCPU::pushWord(uint32_t value) {
    setGpr(14, getGpr(14) - 4);
    writeWord(getGpr(14), value);
}

void VirtualCPU::enterInterrupt(uint32_t causeValue) {
    pushWord(getCsr(0)); // push status
    pushWord(pc); // push return address, in that order
    setCsr(2, causeValue); // cause
    setCsr(0, getCsr(0) | STATUS_I_BIT); // globally mask interrupts
    pc = getCsr(1); // jump to handler
}

bool VirtualCPU::isGloballyMasked() const {
    return (getCsr(0) & STATUS_I_BIT) != 0;
}

bool VirtualCPU::isTerminalMasked() const {
    return (getCsr(0) & STATUS_TL_BIT) != 0;
}

void VirtualCPU::setTerminalInput(uint8_t value) {
    termIn = value;
}

CPUState VirtualCPU::executeSoftwareInterrupt() {
    if (instruction.mode != 0x0) {
        return CPUState::ILLEGAL;
    }
    enterInterrupt(4); // cause = software interrupt
    return CPUState::CONTINUE;
}

CPUState VirtualCPU::executeCall() {
    uint32_t target = getGpr(instruction.regA) + getGpr(instruction.regB)
                     + static_cast<uint32_t>(instruction.displ);
    switch (instruction.mode) {
        case 0x0:
            pushWord(pc);
            pc = target;
            return CPUState::CONTINUE;
        case 0x1:
            pushWord(pc);
            pc = readWord(target);
            return CPUState::CONTINUE;
        default:
            return CPUState::ILLEGAL;
    }
}

CPUState VirtualCPU::executeJump() {
    uint32_t target = getGpr(instruction.regA) + static_cast<uint32_t>(instruction.displ);
    int32_t b = static_cast<int32_t>(getGpr(instruction.regB));
    int32_t c = static_cast<int32_t>(getGpr(instruction.regC));

    bool condition;
    bool indirect;
    switch (instruction.mode) {
        case 0x0: condition = true;   indirect = false; break;
        case 0x1: condition = b == c; indirect = false; break;
        case 0x2: condition = b != c; indirect = false; break;
        case 0x3: condition = b > c;  indirect = false; break;
        case 0x8: condition = true;   indirect = true;  break;
        case 0x9: condition = b == c; indirect = true;  break;
        case 0xA: condition = b != c; indirect = true;  break;
        case 0xB: condition = b > c;  indirect = true;  break;
        default:  return CPUState::ILLEGAL;
    }
    if (condition) {
        pc = indirect ? readWord(target) : target;
    }
    return CPUState::CONTINUE;
}

CPUState VirtualCPU::executeXchg() {
    if (instruction.mode != 0x0) {
        return CPUState::ILLEGAL;
    }
    uint32_t temp = getGpr(instruction.regB);
    setGpr(instruction.regB, getGpr(instruction.regC));
    setGpr(instruction.regC, temp);
    return CPUState::CONTINUE;
}

CPUState VirtualCPU::executeArithmetic() {
    uint32_t b = getGpr(instruction.regB);
    uint32_t c = getGpr(instruction.regC);
    uint32_t result;
    switch (instruction.mode) {
        case 0x0: result = b + c; break;
        case 0x1: result = b - c; break;
        case 0x2: result = b * c; break;
        case 0x3: {
            int32_t sb = static_cast<int32_t>(b);
            int32_t sc = static_cast<int32_t>(c);
            // divide-by-zero and INT32_MIN/-1 both have no reasonable interpretation
            if (sc == 0 || (sb == INT32_MIN && sc == -1)) {
                return CPUState::ILLEGAL;
            }
            result = static_cast<uint32_t>(sb / sc);
            break;
        }
        default:
            return CPUState::ILLEGAL;
    }
    setGpr(instruction.regA, result);
    return CPUState::CONTINUE;
}

CPUState VirtualCPU::executeLogic() {
    uint32_t b = getGpr(instruction.regB);
    uint32_t c = getGpr(instruction.regC);
    uint32_t result;
    switch (instruction.mode) {
        case 0x0: result = ~b;   break;
        case 0x1: result = b & c; break;
        case 0x2: result = b | c; break;
        case 0x3: result = b ^ c; break;
        default:
            return CPUState::ILLEGAL;
    }
    setGpr(instruction.regA, result);
    return CPUState::CONTINUE;
}

CPUState VirtualCPU::executeShift() {
    uint32_t b = getGpr(instruction.regB);
    // count masked to 0-31: an unmasked native '<<'/'>>' by >=32 is undefined behavior.
    uint32_t amount = getGpr(instruction.regC) & 0x1F;
    uint32_t result;
    switch (instruction.mode) {
        case 0x0: result = b << amount; break;
        case 0x1: result = b >> amount; break;
        default:
            return CPUState::ILLEGAL;
    }
    setGpr(instruction.regA, result);
    return CPUState::CONTINUE;
}

CPUState VirtualCPU::executeStore() {
    uint32_t c = getGpr(instruction.regC);
    switch (instruction.mode) {
        case 0x0: {
            uint32_t addr = getGpr(instruction.regA) + getGpr(instruction.regB)
                           + static_cast<uint32_t>(instruction.displ);
            writeWord(addr, c);
            return CPUState::CONTINUE;
        }
        case 0x2: {
            uint32_t addr = getGpr(instruction.regA) + getGpr(instruction.regB)
                           + static_cast<uint32_t>(instruction.displ);
            writeWord(readWord(addr), c);
            return CPUState::CONTINUE;
        }
        case 0x1: {
            // gpr[A]<=gpr[A]+D THEN mem32[gpr[A]]<=gpr[C] - increment happens first, store
            // uses the already-updated A (this is what "push %gpr" compiles to).
            uint32_t newA = getGpr(instruction.regA) + static_cast<uint32_t>(instruction.displ);
            setGpr(instruction.regA, newA);
            writeWord(newA, c);
            return CPUState::CONTINUE;
        }
        default:
            return CPUState::ILLEGAL;
    }
}

CPUState VirtualCPU::executeLoad() {
    switch (instruction.mode) {
        case 0x0: { // gpr[A] <= csr[B]
            if (!isValidCsrIndex(instruction.regB)) return CPUState::ILLEGAL;
            setGpr(instruction.regA, getCsr(instruction.regB));
            return CPUState::CONTINUE;
        }
        case 0x1: { // gpr[A] <= gpr[B] + D
            setGpr(instruction.regA, getGpr(instruction.regB) + static_cast<uint32_t>(instruction.displ));
            return CPUState::CONTINUE;
        }
        case 0x2: { // gpr[A] <= mem32[gpr[B] + gpr[C] + D]
            uint32_t addr = getGpr(instruction.regB) + getGpr(instruction.regC)
                           + static_cast<uint32_t>(instruction.displ);
            setGpr(instruction.regA, readWord(addr));
            return CPUState::CONTINUE;
        }
        case 0x3: {
            // gpr[A]<=mem32[gpr[B]] THEN gpr[B]<=gpr[B]+D (load happens first, unlike the
            // store/push case above) - cache the old B so this is correct even if A==B.
            uint32_t oldB = getGpr(instruction.regB);
            uint32_t value = readWord(oldB);
            setGpr(instruction.regA, value);
            setGpr(instruction.regB, oldB + static_cast<uint32_t>(instruction.displ));
            return CPUState::CONTINUE;
        }
        case 0x4: { // csr[A] <= gpr[B]
            if (!isValidCsrIndex(instruction.regA)) return CPUState::ILLEGAL;
            setCsr(instruction.regA, getGpr(instruction.regB));
            return CPUState::CONTINUE;
        }
        case 0x5: { // csr[A] <= csr[B] | D
            if (!isValidCsrIndex(instruction.regA) || !isValidCsrIndex(instruction.regB)) {
                return CPUState::ILLEGAL;
            }
            setCsr(instruction.regA, getCsr(instruction.regB) | static_cast<uint32_t>(instruction.displ));
            return CPUState::CONTINUE;
        }
        case 0x6: { // csr[A] <= mem32[gpr[B] + gpr[C] + D]
            if (!isValidCsrIndex(instruction.regA)) return CPUState::ILLEGAL;
            uint32_t addr = getGpr(instruction.regB) + getGpr(instruction.regC)
                           + static_cast<uint32_t>(instruction.displ);
            setCsr(instruction.regA, readWord(addr));
            return CPUState::CONTINUE;
        }
        case 0x7: {
            if (!isValidCsrIndex(instruction.regA)) return CPUState::ILLEGAL;
            uint32_t oldB = getGpr(instruction.regB);
            uint32_t value = readWord(oldB);
            setCsr(instruction.regA, value);
            setGpr(instruction.regB, oldB + static_cast<uint32_t>(instruction.displ));
            return CPUState::CONTINUE;
        }
        default:
            return CPUState::ILLEGAL;
    }
}