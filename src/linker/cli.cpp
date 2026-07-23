#include "linker/cli.hpp"

#include <cstdint>
#include <string>

#include "common/errors.hpp"

LinkerOptions parseArgs(int argc, char** argv) {
    LinkerOptions opts;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-o") {
            if (i + 1 >= argc) throw LinkerError("-o requires an output path");
            opts.outputPath = argv[++i];
        } else if (arg == "-relocatable") {
            opts.relocatable = true;
        } else if (arg == "-hex") {
            opts.hex = true;
        } else if (arg.rfind("-place=", 0) == 0) {
            std::string rest = arg.substr(7);
            int at = static_cast<int>(rest.find('@'));
            if (at < 0) {
                throw LinkerError("invalid -place option (expected name@address): " + arg);
            }
            PlaceOption place;
            place.sectionName = rest.substr(0, at);
            unsigned long raw = std::stoul(rest.substr(at + 1), nullptr, 0);
            place.address = static_cast<int>(static_cast<uint32_t>(raw));
            opts.placements.push_back(place);
        } else {
            opts.inputPaths.push_back(arg);
        }
    }

    if (opts.inputPaths.empty()) {
        throw LinkerError("no input files given");
    }
    if (opts.outputPath.empty()) {
        throw LinkerError("-o output path is required");
    }
    if (opts.relocatable == opts.hex) {
        throw LinkerError("exactly one of -hex or -relocatable must be given");
    }

    return opts;
}
