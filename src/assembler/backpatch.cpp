#include "assembler/backpatch.hpp"

void BackpatchTable::add(const std::string& symbolName, const BackpatchEntry& entry) {
    table[symbolName].push_back(entry);
}

std::vector<BackpatchEntry> BackpatchTable::resolveAll(const std::string& symbolName) {
    auto it = table.find(symbolName);
    if (it == table.end()) {
        return {};
    }
    std::vector<BackpatchEntry> result = std::move(it->second);
    table.erase(it);
    return result;
}

std::vector<std::string> BackpatchTable::remainingSymbolNames() const {
    std::vector<std::string> names;
    for (const auto& kv : table) {
        names.push_back(kv.first);
    }
    return names;
}
