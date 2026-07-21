#ifndef ASSEMBLER_LEXER_HPP
#define ASSEMBLER_LEXER_HPP

#include <optional>
#include <string>
#include <vector>

// struct representing result of parsing of one line
struct ParsedLine {
    std::string label;                  // label name (if exists)
    std::string mnemonicOrDirective;    // first word after label name (if exists) must be 
                                        // either mnemonic or a directive
    std::vector<std::string> rawArgs;   // arguments or operands
};

// remove comment from the end of the line
std::string stripComment(const std::string& line);

// trim whitespace
std::string trim(const std::string& s);

// checks if word begins with a "."
bool isDirectiveWord(const std::string& word);

// split by comma, not including [] (in case of regind addressing)
std::vector<std::string> splitTopLevelByComma(const std::string& s);

// parses one singular line
// throws AssemblerError if there is more than one label in one line
ParsedLine parseLine(const std::string& rawLine, int lineNum);

#endif // ASSEMBLER_LEXER_HPP
