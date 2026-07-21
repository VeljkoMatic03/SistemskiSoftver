#include "assembler/lexer.hpp"
#include "common/errors.hpp"


std::string stripComment(const std::string& line) {
    int pos = static_cast<int>(line.find('#')); // string::npos truncates to -1 as int
    if (pos < 0) {
        return line;
    }
    return line.substr(0, pos);
}

bool isWhitespace(char c) {
    return c==' ' || c=='\t';
}

std::string trim(const std::string& s) {
    int len = static_cast<int>(s.size());
    int start = 0;
    while (start < len && isWhitespace(s[start])) {
        start++;
    }
    int end = len;
    while (end > start && isWhitespace(s[end-1])) {
        end--;
    }
    return s.substr(start, end - start);
}

bool isDirectiveWord(const std::string& word) {
    return !word.empty() && word[0] == '.';
}

std::vector<std::string> splitTopLevelByComma(const std::string& s) {
    std::vector<std::string> result;
    int depth = 0;
    int start = 0;
    int len = static_cast<int>(s.size());
    for (int i = 0; i < len; i++) {
        char c = s[i];
        if (c == '[') {
            depth++;
        } else if (c == ']') {
            depth--;
        } else if (c == ',' && depth == 0) {
            result.push_back(trim(s.substr(start, i - start)));
            start = i + 1;
        }
    }
    std::string last = trim(s.substr(start));
    if (!last.empty()) {
        result.push_back(last);
    }
    return result;
}

// looks for ":" which is the end of label, it must be before first whitespace
// (.ascii string can contain ":" so we must have ":" concatenated to the end of label name)
static bool findLabelColon(const std::string& line, int& colonPosOut) {
    int colonPos = static_cast<int>(line.find(':'));
    int firstSpace = static_cast<int>(line.find_first_of(" \t"));
    if (colonPos != -1 &&
        (firstSpace == -1 || colonPos < firstSpace)) {
        colonPosOut = colonPos;
        return true;
    }
    return false;
}

ParsedLine parseLine(const std::string& rawLine, int lineNum) {
    ParsedLine result;

    std::string line = trim(stripComment(rawLine));
    if (line.empty()) {
        return result;
    }

    // find label if it exists
    int colonPos;
    if (findLabelColon(line, colonPos)) {
        result.label = trim(line.substr(0, colonPos));
        line = trim(line.substr(colonPos + 1));

        // check if there are multiple labels in one line - error
        int secondColon;
        if (!line.empty() && findLabelColon(line, secondColon)) {
            throw AssemblerError("ERROR: There cannot be more than one label in one line", lineNum, rawLine);
        }
    }

    if (line.empty()) {
        return result; // just label - return
    }

    // find second word - either directive or mnemonic
    int sp = static_cast<int>(line.find_first_of(" \t"));
    std::string firstWord = (sp == -1) ? line : line.substr(0, sp);
    result.mnemonicOrDirective = firstWord;

    std::string rest = (sp == -1) ? "" : trim(line.substr(sp + 1));
    if (rest.empty()) {
        return result; // only for .end, halt, pop, etc.
    }

    // parsing the arguments, only exception is .ascii directive
    if (firstWord == ".ascii") {
        result.rawArgs.push_back(rest);
    } else {
        result.rawArgs = splitTopLevelByComma(rest);
    }
    // else if .equ - TODO

    return result;
}
