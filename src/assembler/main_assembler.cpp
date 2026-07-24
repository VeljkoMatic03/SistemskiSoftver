#include <iostream>
#include <string>

#include "assembler/assembler.hpp"
#include "common/errors.hpp"

int main(int argc, char** argv) {
    std::string outputPath = "a.out.o";
    std::string inputPath;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            outputPath = argv[++i];
        } else {
            inputPath = arg;
        }
    }

    if (inputPath.empty()) {
        std::cerr << "format: assembler [-o output_file] input_file\n";
        return 1;
    }

    try {
        Assembler assembler;
        assembler.assembleFile(inputPath);
        assembler.writeObjectFile(outputPath);
        assembler.writeBinaryObjectFile(outputPath + ".bin");
    } catch (const AssemblerError& e) {
        std::cerr << "Error";
        if (e.line >= 0) {
            std::cerr << " (line " << e.line << ")";
        }
        std::cerr << ": " << e.what() << "\n";
        if (!e.lineText.empty()) {
            std::cerr << "  -> " << e.lineText << "\n";
        }
        return 1;
    }

    return 0;
}
