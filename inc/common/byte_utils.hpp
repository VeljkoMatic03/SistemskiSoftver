#ifndef COMMON_BYTE_UTILS_HPP
#define COMMON_BYTE_UTILS_HPP

#include <cstdint>
#include <cstddef>
#include <vector>

// writes a 32-bit value in little-endian order at the given location (4 bytes)
inline void writeU32LE(uint8_t* dst, int value) {
    dst[0] = static_cast<uint8_t>(value & 0xFF);
    dst[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    dst[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    dst[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

inline int readU32LE(const uint8_t* src) {
    return src[0] | (src[1] << 8) | (src[2] << 16) | (src[3] << 24);
}

// writes a 12-bit signed displacement into an instruction field.
// instrBytes must point to the first of the instruction's 4 bytes.
// Format:
//   byte III (index 2): top 4 bits = RegC (untouched), bottom 4 bits = Disp[11:8]
//   byte IV  (index 3): all 8 bits = Disp[7:0]
// returns false if the value doesn't fit into the 12-bit signed range [-2048, 2047].
inline bool encodeDisp12(uint8_t* instrBytes, int value) {
    if (value < -2048 || value > 2047) {
        return false;
    }
    uint16_t disp12 = static_cast<uint16_t>(value) & 0x0FFF;
    instrBytes[2] = static_cast<uint8_t>((instrBytes[2] & 0xF0) | ((disp12 >> 8) & 0x0F));
    instrBytes[3] = static_cast<uint8_t>(disp12 & 0xFF);
    return true;
}

// reads a 12-bit signed displacement from an instruction field (useful for debug/printing).
inline int decodeDisp12(const uint8_t* instrBytes) {
    uint16_t disp12 = (instrBytes[2] & 0x0F) << 8 | instrBytes[3];
    // sign-extend from 12 bits to 32 bits
    if (disp12 & 0x0800) {
        return disp12 - 0x1000;
    }
    return disp12;
}

#endif // COMMON_BYTE_UTILS_HPP
