// Cheats as memory writes into the running game, and a scanner to find their addresses. The cheats are lines of
// cheats.txt in the FatalFrame2 folder (beside the game): "name | 0xADDRESS | type | value", type b (1 byte), w (2),
// d (4) or f (float). A cheat that is on has its value written at each present.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace game {

struct CheatEntry {
    std::string name;
    uint32_t addr = 0;
    char type = 'd';
    double value = 0;
    bool on = false;
};

void CheatsRefresh();                     // UI thread: reads cheats.txt again when it changed (at most every 1.5 s)
std::vector<CheatEntry> CheatsList();     // a copy
void CheatSet(size_t index, bool on);
bool CheatAdd(const std::string& name, uint32_t addr, char type, double value);  // also appends it to cheats.txt

enum class ScanMode { Equal, Changed, Unchanged, Increased, Decreased };
struct ScanHit {
    uint32_t addr;
    double value;  // read now
};
// scan range: the game's .data and its zeroed data after it (where its state mostly lives), or all 64 MB of RAM
void ScanFirst(char type, bool all_ram, double value);  // every address holding the value
void ScanFirstUnknown(char type);                       // every address of the globals, to narrow by what changes
void ScanNext(ScanMode mode, double value);
void ScanReset();
size_t ScanCount();
std::vector<ScanHit> ScanResults(size_t max);
const char* ScanTypeName(char type);

}  // namespace game
