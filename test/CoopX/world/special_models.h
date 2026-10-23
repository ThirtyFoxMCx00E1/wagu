#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cctype>

namespace coop_special {
constexpr uint16_t kFirstModelIndex = 290;
constexpr uint16_t kLastModelIndex = 299;
constexpr int kFirstRemoteSlot = 6;
constexpr int kLastRemoteSlot = 10;
constexpr size_t kNameCapacity = 16;

inline int SlotFromModelIndex(uint16_t modelIndex) {
    return modelIndex >= kFirstModelIndex && modelIndex <= kLastModelIndex
        ? static_cast<int>(modelIndex - kFirstModelIndex + 1)
        : 0;
}

inline uint16_t ModelIndexFromSlot(int slot) {
    return slot >= 1 && slot <= 10
        ? static_cast<uint16_t>(kFirstModelIndex + slot - 1)
        : 0;
}

inline bool NormalizeName(const char* input, char* output, size_t outputSize) {
    if (!output || outputSize == 0) return false;
    output[0] = '\0';
    if (!input) return false;
    char candidate[kNameCapacity]{};
    size_t length = 0;
    while (input[length] && input[length] != '\r' && input[length] != '\n' && length + 1 < sizeof(candidate)) {
        candidate[length] = static_cast<char>(std::toupper(static_cast<unsigned char>(input[length])));
        ++length;
    }
    while (input[length] == '\r' || input[length] == '\n') ++length;
    if (input[length] != '\0' || length == 0 || length + 1 > outputSize) return false;
    candidate[length] = '\0';
    static constexpr const char* kAllowed[] = {
        "ANDRE", "BBTHIN", "BB", "CAT", "CESAR", "COPGRL1", "COPGRL2", "COPGRL3",
        "CLAUDE", "CROGRL1", "CROGRL2", "CROGRL3", "DWAYNE", "EMMET", "FORELLI",
        "GANGRL1", "GANGRL2", "GANGRL3", "GUNGRL1", "GUNGRL2", "GUNGRL3", "HERN",
        "JANITOR", "JETHRO", "JIZZY", "KENDL", "MACCER", "MADDOGG", "MECGRL1",
        "MECGRL2", "MECGRL3", "NURGRL1", "NURGRL2", "NURGRL3", "OGLOC", "PAUL",
        "PULASKI", "ROSE", "RYDER1", "RYDER2", "RYDER3", "SINDACO", "SMOKE",
        "SMOKEV", "SUZIE", "SWEET", "TBONE", "TENPEN", "TORINO", "TRUTH", "WUZIMU", "ZERO"
    };
    for (const char* allowed : kAllowed) {
        if (std::strcmp(candidate, allowed) == 0) {
            std::memcpy(output, candidate, length + 1);
            return true;
        }
    }
    return false;
}
}
