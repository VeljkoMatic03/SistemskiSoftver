#ifndef COMMON_ERRORS_HPP
#define COMMON_ERRORS_HPP

#include <stdexcept>
#include <string>

// Thrown anywhere line parsing/processing hits a syntax or semantic error.
// Caught ONCE, in the relevant tool's main().
class AssemblerError : public std::runtime_error {
public:
    AssemblerError(const std::string& message, int line = -1, const std::string& lineText = "")
        : std::runtime_error(message), line(line), lineText(lineText) {}

    int line;
    std::string lineText;
};

class LinkerError : public std::runtime_error {
public:
    explicit LinkerError(const std::string& message) : std::runtime_error(message) {}
};

class EmulatorError : public std::runtime_error {
public:
    explicit EmulatorError(const std::string& message) : std::runtime_error(message) {}
};

#endif // COMMON_ERRORS_HPP
