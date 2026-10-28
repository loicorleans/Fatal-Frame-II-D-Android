// Cheats as memory writes into the running game, and the scanner that finds their addresses, see cheat_engine.h.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "common/options.h"
#include "game/cheat_engine.h"
#include "xdk/hle.h"

using namespace kernel;

namespace game {

namespace {

constexpr const char* kPaths[] = {"/storage/emulated/0/FatalFrame2/cheats.txt",
                                  "/storage/emulated/0/Android/data/com.fatalframe2.recomp/files/cheats.txt"};
constexpr uint32_t kRamStart = 0x10000, kRamEnd = 0x04000000;          // the console's 64 MB
constexpr uint32_t kGlobalsStart = 0x19F360, kGlobalsEnd = 0x379398;   // .data and what follows it
constexpr size_t kMaxHits = 3000000;

std::mutex g_mutex;
std::vector<CheatEntry> g_cheats;
std::atomic<int> g_enabled{0};
std::string g_file;
std::filesystem::file_time_type g_file_time;
std::chrono::steady_clock::time_point g_checked;

std::vector<uint32_t> g_hit_addr, g_hit_prev;  // the scanner's candidates and what they held at the last scan
char g_scan_type = 'd';

uint32_t SizeOf(char type) { return type == 'b' ? 1 : type == 'w' ? 2 : 4; }

bool Valid(uint32_t addr, uint32_t size) { return addr >= kRamStart && addr + size <= kRamEnd; }

uint32_t ReadBits(uint32_t addr, uint32_t size) {
    uint32_t v = 0;
    std::memcpy(&v, HostPtr(addr), size);
    return v;
}

double AsNumber(char type, uint32_t bits) {
    if (type == 'f') {
        float f;
        std::memcpy(&f, &bits, 4);
        return double(f);
    }
    return double(bits);
}

uint32_t ToBits(char type, double value) {
    if (type == 'f') {
        float f = float(value);
        uint32_t b;
        std::memcpy(&b, &f, 4);
        return b;
    }
    return uint32_t(int64_t(value));
}

void Write(const CheatEntry& c) {
    uint32_t size = SizeOf(c.type);
    if (!Valid(c.addr, size)) return;
    uint32_t bits = ToBits(c.type, c.value);
    std::memcpy(HostPtr(c.addr), &bits, size);
}

std::string Trim(std::string s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// "name | 0xADDRESS | type | value"; a line that does not read is skipped
bool ParseLine(const std::string& line, CheatEntry& out) {
    if (line.empty() || line[0] == '#') return false;
    std::vector<std::string> part;
    size_t from = 0;
    for (;;) {
        size_t bar = line.find('|', from);
        part.push_back(Trim(line.substr(from, bar == std::string::npos ? std::string::npos : bar - from)));
        if (bar == std::string::npos) break;
        from = bar + 1;
    }
    if (part.size() < 4 || part[0].empty() || part[2].empty()) return false;
    char type = char(std::tolower(uint8_t(part[2][0])));
    if (type != 'b' && type != 'w' && type != 'd' && type != 'f') return false;
    char* end = nullptr;
    unsigned long addr = std::strtoul(part[1].c_str(), &end, 0);
    if (end == part[1].c_str() || !Valid(uint32_t(addr), SizeOf(type))) return false;
    out.name = part[0];
    out.addr = uint32_t(addr);
    out.type = type;
    out.value = std::strtod(part[3].c_str(), nullptr);
    return true;
}

void Recount() {
    int n = 0;
    for (const CheatEntry& c : g_cheats) n += c.on;
    g_enabled = n;
}

std::string FileToAppend() {
    for (const char* p : kPaths)
        if (std::filesystem::exists(std::filesystem::path(p).parent_path())) return p;
    return kPaths[0];
}

}  // namespace

const char* ScanTypeName(char type) {
    return type == 'b' ? "1 BYTE" : type == 'w' ? "2 BYTES" : type == 'f' ? "FLOAT" : "4 BYTES";
}

void CheatsRefresh() {
    auto now = std::chrono::steady_clock::now();
    if (now - g_checked < std::chrono::milliseconds(1500)) return;
    g_checked = now;
    for (const char* path : kPaths) {
        std::error_code ec;
        auto time = std::filesystem::last_write_time(path, ec);
        if (ec) continue;
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_file == path && time == g_file_time) return;
        std::ifstream f(path);
        if (!f) continue;
        std::vector<CheatEntry> fresh;
        std::string line;
        while (std::getline(f, line)) {
            CheatEntry c;
            if (!ParseLine(Trim(line), c)) continue;
            for (const CheatEntry& old : g_cheats)  // a cheat kept on across the reload
                if (old.addr == c.addr && old.type == c.type && old.name == c.name) c.on = old.on;
            fresh.push_back(std::move(c));
        }
        g_cheats = std::move(fresh);
        g_file = path;
        g_file_time = time;
        Recount();
        return;
    }
}

std::vector<CheatEntry> CheatsList() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_cheats;
}

void CheatSet(size_t index, bool on) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (index < g_cheats.size()) g_cheats[index].on = on;
    Recount();
}

bool CheatAdd(const std::string& name, uint32_t addr, char type, double value) {
    if (!Valid(addr, SizeOf(type))) return false;
    std::string path = FileToAppend();
    {
        std::ofstream f(path, std::ios::app);
        if (!f) return false;
        char text[128];
        std::snprintf(text, sizeof(text), "%s | 0x%08X | %c | %.6g\n", name.c_str(), addr, type, value);
        f << text;
    }
    g_checked = {};  // read the file again at the next refresh
    return true;
}

void ApplyCheats() {
    if (!g_enabled.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const CheatEntry& c : g_cheats)
        if (c.on) Write(c);
}

// ---- the scanner -----------------------------------------------------------------------------------------------

namespace {

bool Equals(char type, uint32_t bits, double value) {
    if (type == 'f') return std::fabs(AsNumber('f', bits) - value) < 0.001;
    return bits == (ToBits(type, value) & (SizeOf(type) == 4 ? 0xFFFFFFFFu : (1u << (8 * SizeOf(type))) - 1));
}

}  // namespace

void ScanFirst(char type, bool all_ram, double value) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_scan_type = type;
    g_hit_addr.clear();
    g_hit_prev.clear();
    uint32_t size = SizeOf(type), lo = all_ram ? kRamStart : kGlobalsStart, hi = all_ram ? kRamEnd : kGlobalsEnd;
    lo = (lo + size - 1) / size * size;
    for (uint32_t a = lo; a + size <= hi && g_hit_addr.size() < kMaxHits; a += size) {
        uint32_t bits = ReadBits(a, size);
        if (!Equals(type, bits, value)) continue;
        g_hit_addr.push_back(a);
        g_hit_prev.push_back(bits);
    }
}

void ScanFirstUnknown(char type) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_scan_type = type;
    g_hit_addr.clear();
    g_hit_prev.clear();
    uint32_t size = SizeOf(type), lo = (kGlobalsStart + size - 1) / size * size;
    for (uint32_t a = lo; a + size <= kGlobalsEnd; a += size) {
        g_hit_addr.push_back(a);
        g_hit_prev.push_back(ReadBits(a, size));
    }
}

void ScanNext(ScanMode mode, double value) {
    std::lock_guard<std::mutex> lock(g_mutex);
    uint32_t size = SizeOf(g_scan_type);
    size_t kept = 0;
    for (size_t i = 0; i < g_hit_addr.size(); i++) {
        uint32_t bits = ReadBits(g_hit_addr[i], size);
        double now = AsNumber(g_scan_type, bits), before = AsNumber(g_scan_type, g_hit_prev[i]);
        bool keep = false;
        switch (mode) {
            case ScanMode::Equal: keep = Equals(g_scan_type, bits, value); break;
            case ScanMode::Changed: keep = bits != g_hit_prev[i]; break;
            case ScanMode::Unchanged: keep = bits == g_hit_prev[i]; break;
            case ScanMode::Increased: keep = now > before; break;
            case ScanMode::Decreased: keep = now < before; break;
        }
        if (!keep) continue;
        g_hit_addr[kept] = g_hit_addr[i];
        g_hit_prev[kept] = bits;
        kept++;
    }
    g_hit_addr.resize(kept);
    g_hit_prev.resize(kept);
}

void ScanReset() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_hit_addr.clear();
    g_hit_prev.clear();
    g_hit_addr.shrink_to_fit();
    g_hit_prev.shrink_to_fit();
}

size_t ScanCount() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_hit_addr.size();
}

std::vector<ScanHit> ScanResults(size_t max) {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<ScanHit> out;
    uint32_t size = SizeOf(g_scan_type);
    for (size_t i = 0; i < g_hit_addr.size() && i < max; i++)
        out.push_back({g_hit_addr[i], AsNumber(g_scan_type, ReadBits(g_hit_addr[i], size))});
    return out;
}

}  // namespace game
