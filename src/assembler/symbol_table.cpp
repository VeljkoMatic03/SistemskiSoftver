#include "assembler/symbol_table.hpp"
#include "common/errors.hpp"

#include <algorithm>

SymbolTable::SymbolTable() : nextNum(1) {
    // num=0 is reserved for UND - we don't write it explicitly into the table,
    // serialization will add it separately at the start of the output file.
}

int SymbolTable::allocateNum() {
    return nextNum++;
}

bool SymbolTable::exists(const std::string& name) const {
    return table.find(name) != table.end();
}

SymbolTableEntry& SymbolTable::getOrCreate(const std::string& name) {
    auto it = table.find(name);
    if (it != table.end()) {
        return it->second;
    }
    SymbolTableEntry entry;
    entry.name = name;
    entry.sectionId = -1;
    entry.bind = SymbolBind::LOCAL;
    entry.value = 0;
    entry.isDefined = false;
    entry.num = allocateNum();
    entry.type = SymbolType::UND;
    auto result = table.emplace(name, entry);
    return result.first->second;
}

const SymbolTableEntry& SymbolTable::get(const std::string& name) const {
    return table.at(name);
}

const SymbolTableEntry& SymbolTable::getByNum(int num) const {
    for (const auto& kv : table) {
        if (kv.second.num == num) {
            return kv.second;
        }
    }
    throw AssemblerError("internal error: no symbol with num " + std::to_string(num));
}

void SymbolTable::defineSection(const std::string& name, int sectionId) {
    auto it = table.find(name);
    if (it != table.end()) {
        if (it->second.type != SymbolType::SEC) {
            throw AssemblerError("name '" + name + "' collides with an existing symbol of the same name");
        }
        return; // section reopened - already registered
    }
    SymbolTableEntry entry;
    entry.name = name;
    entry.sectionId = sectionId;
    entry.bind = SymbolBind::GLOBAL;
    entry.value = 0;
    entry.isDefined = true;
    entry.num = allocateNum();
    entry.type = SymbolType::SEC;
    table.emplace(name, entry);
}

void SymbolTable::defineLabel(const std::string& name, int sectionId, int offset,
                               int currentLine, const std::string& lineText) {
    SymbolTableEntry& entry = getOrCreate(name);

    if (entry.type == SymbolType::SEC) {
        throw AssemblerError("label '" + name + "' collides with a section of the same name", currentLine, lineText);
    }
    if (entry.isDefined) {
        throw AssemblerError("redefinicija simbola '" + name + "'", currentLine, lineText);
    }

    entry.sectionId = sectionId;
    entry.value = offset;
    entry.isDefined = true;
    entry.type = SymbolType::SYM;
    // bind is NOT touched here - if it was previously set to GLOBAL via .global, it stays that way.
}

void SymbolTable::declareGlobal(const std::string& name) {
    SymbolTableEntry& entry = getOrCreate(name);
    entry.bind = SymbolBind::GLOBAL;
    // isDefined is NOT touched here.
}

std::vector<SymbolTableEntry> SymbolTable::allSortedByNum() const {
    std::vector<SymbolTableEntry> result;
    result.reserve(table.size());
    for (const auto& kv : table) {
        result.push_back(kv.second);
    }
    std::sort(result.begin(), result.end(),
              [](const SymbolTableEntry& a, const SymbolTableEntry& b) {
                  return a.num < b.num;
              });
    return result;
}
