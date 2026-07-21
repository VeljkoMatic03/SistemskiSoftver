#ifndef ASSEMBLER_SECTION_MANAGER_HPP
#define ASSEMBLER_SECTION_MANAGER_HPP

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "utils/structs.hpp"

// used to store memory into data buffer while we are parsing each line
struct SectionData {
    std::string name;
    std::vector<uint8_t> data;
    int num;
};

// manages every section, tracks current active section
class SectionManager {
public:
    SectionManager() : nextNum(0) {}

    std::string currentSectionName;

    // opens new or continues existing section
    void openSection(const std::string& name);

    bool hasActiveSection() const { return !currentSectionName.empty(); }
    //const std::string& currentSectionName() const { return currentSectionName; }
    int currentSectionId() const;

    // Current offset within the active section = size of its buffer.
    int currentOffset() const;

    // Appends bytes to the end of the active section, returns the offset they were written at.
    int appendBytes(const std::vector<uint8_t>& bytes);
    int appendZeros(int count);

    // Access for patching (backpatch) bytes already written within the active section.
    uint8_t* rawPointerAt(int sectionId, int offset);

    // All sections, in the order they were opened (for serialization).
    const std::vector<std::string>& order() const { return sectionOrder; }
    const SectionData& get(const std::string& name) const { return sections.at(name); }
    const SectionData& getById(int id) const { return sections.at(sectionOrder.at(id)); }

private:
    std::unordered_map<std::string, SectionData> sections;
    std::vector<std::string> sectionOrder;
    int nextNum;
};

#endif // ASSEMBLER_SECTION_MANAGER_HPP
