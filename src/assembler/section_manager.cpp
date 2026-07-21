#include "assembler/section_manager.hpp"
#include "common/errors.hpp"

void SectionManager::openSection(const std::string& name) {
    auto it = sections.find(name);
    if (it == sections.end()) {
        SectionData sec;
        sec.name = name;
        sec.num = nextNum++;
        sections.emplace(name, std::move(sec));
        sectionOrder.push_back(name);
    }
    // if the section already exists (reopen), we just continue from its current size - nothing else to touch.
    currentSectionName = name;
}

int SectionManager::currentSectionId() const {
    return sections.at(currentSectionName).num;
}

int SectionManager::currentOffset() const {
    return static_cast<int>(sections.at(currentSectionName).data.size());
}

int SectionManager::appendBytes(const std::vector<uint8_t>& bytes) {
    if (!hasActiveSection()) {
        throw AssemblerError("nema aktivne sekcije - sadrzaj mora biti unutar .section bloka");
    }
    SectionData& sec = sections.at(currentSectionName);
    int offset = sec.data.size();
    sec.data.insert(sec.data.end(), bytes.begin(), bytes.end());
    return offset;
}

int SectionManager::appendZeros(int count) {
    if (!hasActiveSection()) {
        throw AssemblerError("nema aktivne sekcije - sadrzaj mora biti unutar .section bloka");
    }
    SectionData& sec = sections.at(currentSectionName);
    int offset = sec.data.size();
    sec.data.insert(sec.data.end(), count, uint8_t{0});
    return offset;
}

uint8_t* SectionManager::rawPointerAt(int sectionId, int offset) {
    SectionData& sec = sections.at(sectionOrder.at(sectionId));
    return sec.data.data() + offset;
}
