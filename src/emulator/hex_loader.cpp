#include "emulator/hex_loader.hpp"

#include <fstream>

#include "common/errors.hpp"

namespace {

bool isSpaceChar(char c) {
    return c == ' ' || c == '\t';
}

void skipSpaces(const std::string& line, int& pos) {
    int len = static_cast<int>(line.size());
    while (pos < len && isSpaceChar(line[pos])) {
        pos++;
    }
}

// reads the next whitespace-delimited token starting at pos, advances pos past it
std::string nextToken(const std::string& line, int& pos) {
    int len = static_cast<int>(line.size());
    int start = pos;
    while (pos < len && !isSpaceChar(line[pos])) {
        pos++;
    }
    return line.substr(start, pos - start);
}

// Tolerant on purpose (variable digit count/case both accepted via base-16 std::stoul) -
// this only ever needs to read our own linker's output, but being forgiving costs nothing.
uint32_t parseHexToken(const std::string& token, int lineNum, const std::string& rawLine) {
    try {
        return static_cast<uint32_t>(std::stoul(token, nullptr, 16));
    } catch (const std::exception&) {
        throw EmulatorError("malformed hex token '" + token + "' on line "
                             + std::to_string(lineNum) + ": " + rawLine);
    }
}

} // namespace

void loadHexImage(VirtualCPU& cpu, const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        throw EmulatorError("input file cannot be opened: " + path);
    }

    std::string rawLine;
    int lineNum = 0;
    while (std::getline(in, rawLine)) {
        lineNum++;
        std::string line = rawLine;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back(); // tolerate CRLF line endings
        }

        int pos = 0;
        skipSpaces(line, pos);
        if (pos >= static_cast<int>(line.size())) {
            continue; // blank line
        }

        std::string addrToken = nextToken(line, pos);
        if (addrToken.empty() || addrToken.back() != ':') {
            throw EmulatorError("expected '<address>:' at start of line "
                                 + std::to_string(lineNum) + ": " + rawLine);
        }
        addrToken.pop_back(); // drop the trailing ':'
        uint32_t address = parseHexToken(addrToken, lineNum, rawLine);

        uint32_t offset = 0;
        while (true) {
            skipSpaces(line, pos);
            if (pos >= static_cast<int>(line.size())) {
                break;
            }
            std::string byteToken = nextToken(line, pos);
            uint32_t byteValue = parseHexToken(byteToken, lineNum, rawLine);
            if (byteValue > 0xFF) {
                throw EmulatorError("byte value out of range on line "
                                     + std::to_string(lineNum) + ": " + rawLine);
            }
            cpu.writeRawByte(address + offset, static_cast<uint8_t>(byteValue));
            offset++;
        }
    }
}
