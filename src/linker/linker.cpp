#include "linker/linker.hpp"

#include "common/errors.hpp"

void checkMultipleDefinitions(const AggregatedState& state) {
    for (const auto& kv : state.symbols) {
        const GlobalSymbol& sym = kv.second;
        if (sym.definedInFiles.size() <= 1) continue;

        std::string message = "symbol '" + sym.name + "' multiply defined in:";
        for (const std::string& path : sym.definedInFiles) {
            message += " " + path;
        }
        throw LinkerError(message);
    }
}
