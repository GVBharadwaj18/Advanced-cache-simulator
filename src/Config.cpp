#include "../include/Config.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>

// ─────────────────────────────────────────────────────────────
//  Internal helpers
// ─────────────────────────────────────────────────────────────
static std::string trim(std::string s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(),
            [](unsigned char c){ return !std::isspace(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(),
            [](unsigned char c){ return !std::isspace(c); }).base(), s.end());
    return s;
}

static std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

// ─────────────────────────────────────────────────────────────
//  readKV  –  parse "KEY=VALUE" lines (ignores # comments)
// ─────────────────────────────────────────────────────────────
std::unordered_map<std::string,std::string>
ConfigParser::readKV(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open())
        throw std::runtime_error("Cannot open config file: " + filePath);

    std::unordered_map<std::string,std::string> kv;
    std::string line;
    int lineNo = 0;

    while (std::getline(file, line)) {
        ++lineNo;
        // Strip comments
        auto commentPos = line.find('#');
        if (commentPos != std::string::npos)
            line = line.substr(0, commentPos);
        line = trim(line);
        if (line.empty()) continue;

        auto eq = line.find('=');
        if (eq == std::string::npos)
            throw std::runtime_error("Config line " + std::to_string(lineNo)
                                     + " malformed (missing '='): " + line);

        std::string key = toUpper(trim(line.substr(0, eq)));
        std::string val = trim(line.substr(eq + 1));
        kv[key] = val;
    }
    return kv;
}

// ─────────────────────────────────────────────────────────────
//  Parsers for enum values
// ─────────────────────────────────────────────────────────────
ReplacementPolicy ConfigParser::parseRP(const std::string& s) {
    std::string u = toUpper(s);
    if (u == "LRU")    return ReplacementPolicy::LRU;
    if (u == "FIFO")   return ReplacementPolicy::FIFO;
    if (u == "RANDOM") return ReplacementPolicy::RANDOM;
    throw std::invalid_argument("Unknown replacement policy: " + s);
}

WritePolicy ConfigParser::parseWP(const std::string& s) {
    std::string u = toUpper(s);
    if (u == "WRITE_BACK"    || u == "WB") return WritePolicy::WRITE_BACK;
    if (u == "WRITE_THROUGH" || u == "WT") return WritePolicy::WRITE_THROUGH;
    throw std::invalid_argument("Unknown write policy: " + s);
}

WriteAllocPolicy ConfigParser::parseWAP(const std::string& s) {
    std::string u = toUpper(s);
    if (u == "WRITE_ALLOCATE"    || u == "WA")  return WriteAllocPolicy::WRITE_ALLOCATE;
    if (u == "NO_WRITE_ALLOCATE" || u == "NWA") return WriteAllocPolicy::NO_WRITE_ALLOCATE;
    throw std::invalid_argument("Unknown write-allocate policy: " + s);
}

uint64_t ConfigParser::parseU64(const std::string& s) {
    try {
        size_t pos;
        uint64_t v = std::stoull(s, &pos, 0); // base-0 → auto hex/octal/dec
        if (pos != s.size())
            throw std::invalid_argument("trailing chars");
        return v;
    } catch (...) {
        throw std::invalid_argument("Invalid numeric value: " + s);
    }
}

uint32_t ConfigParser::parseU32(const std::string& s) {
    uint64_t v = parseU64(s);
    if (v > 0xFFFFFFFFu)
        throw std::invalid_argument("Value out of uint32_t range: " + s);
    return static_cast<uint32_t>(v);
}

// ─────────────────────────────────────────────────────────────
//  parse  –  main entry point
// ─────────────────────────────────────────────────────────────
SimConfig ConfigParser::parse(const std::string& filePath) {
    auto kv = readKV(filePath);
    SimConfig cfg;

    auto getOrDefault = [&](const std::string& key) -> const std::string* {
        auto it = kv.find(key);
        return (it != kv.end()) ? &it->second : nullptr;
    };

    // L1
    if (auto* v = getOrDefault("L1_SIZE"))    cfg.l1Size    = parseU64(*v);
    if (auto* v = getOrDefault("L1_ASSOC"))   cfg.l1Assoc   = parseU32(*v);
    if (auto* v = getOrDefault("L1_BLOCK"))   cfg.l1Block   = parseU32(*v);
    if (auto* v = getOrDefault("L1_LATENCY")) cfg.l1Latency = parseU32(*v);

    // L2
    if (auto* v = getOrDefault("L2_SIZE"))    cfg.l2Size    = parseU64(*v);
    if (auto* v = getOrDefault("L2_ASSOC"))   cfg.l2Assoc   = parseU32(*v);
    if (auto* v = getOrDefault("L2_BLOCK"))   cfg.l2Block   = parseU32(*v);
    if (auto* v = getOrDefault("L2_LATENCY")) cfg.l2Latency = parseU32(*v);

    // Main memory
    if (auto* v = getOrDefault("MEMORY_LATENCY")) cfg.memLatency = parseU32(*v);

    // Replacement / write policies
    if (auto* v = getOrDefault("POLICY"))         cfg.replacementPolicy = parseRP(*v);
    if (auto* v = getOrDefault("WRITE_POLICY"))   cfg.writePolicy       = parseWP(*v);
    if (auto* v = getOrDefault("ALLOC_POLICY"))   cfg.writeAllocPolicy  = parseWAP(*v);

    // Modes
    if (auto* v = getOrDefault("BENCHMARK"))
        cfg.benchmarkMode = (toUpper(*v) == "TRUE" || *v == "1");
    if (auto* v = getOrDefault("COMPARE"))
        cfg.compareMode   = (toUpper(*v) == "TRUE" || *v == "1");

    return cfg;
}

// ─────────────────────────────────────────────────────────────
//  validate
// ─────────────────────────────────────────────────────────────
void ConfigParser::validate(const SimConfig& cfg) {
    auto isPow2 = [](uint64_t v){ return v && !(v & (v-1)); };

    if (!isPow2(cfg.l1Size))  throw std::invalid_argument("L1_SIZE must be power of 2");
    if (!isPow2(cfg.l1Block)) throw std::invalid_argument("L1_BLOCK must be power of 2");
    if (!isPow2(cfg.l2Size))  throw std::invalid_argument("L2_SIZE must be power of 2");
    if (!isPow2(cfg.l2Block)) throw std::invalid_argument("L2_BLOCK must be power of 2");
    if (cfg.l1Assoc == 0)     throw std::invalid_argument("L1_ASSOC must be > 0");
    if (cfg.l2Assoc == 0)     throw std::invalid_argument("L2_ASSOC must be > 0");
    if (cfg.l1Size < cfg.l1Block * cfg.l1Assoc)
        throw std::invalid_argument("L1 cache too small for given block/assoc");
    if (cfg.l2Size < cfg.l2Block * cfg.l2Assoc)
        throw std::invalid_argument("L2 cache too small for given block/assoc");
}

// ─────────────────────────────────────────────────────────────
//  print  –  pretty-print config to stdout
// ─────────────────────────────────────────────────────────────
void ConfigParser::print(const SimConfig& cfg) {
    auto rpName = [](ReplacementPolicy p) -> const char* {
        switch(p) {
            case ReplacementPolicy::LRU:    return "LRU";
            case ReplacementPolicy::FIFO:   return "FIFO";
            case ReplacementPolicy::RANDOM: return "RANDOM";
        }
        return "?";
    };
    auto wpName = [](WritePolicy p) -> const char* {
        return p == WritePolicy::WRITE_BACK ? "WRITE_BACK" : "WRITE_THROUGH";
    };
    auto wapName = [](WriteAllocPolicy p) -> const char* {
        return p == WriteAllocPolicy::WRITE_ALLOCATE ? "WRITE_ALLOCATE" : "NO_WRITE_ALLOCATE";
    };

    std::cout
        << "  L1  : " << (cfg.l1Size/1024) << " KB, "
        << cfg.l1Assoc << "-way, " << cfg.l1Block << "B block, "
        << cfg.l1Latency << " cycles\n"
        << "  L2  : " << (cfg.l2Size/1024) << " KB, "
        << cfg.l2Assoc << "-way, " << cfg.l2Block << "B block, "
        << cfg.l2Latency << " cycles\n"
        << "  MEM : " << cfg.memLatency << " cycles\n"
        << "  Replacement  : " << rpName(cfg.replacementPolicy) << "\n"
        << "  Write Policy : " << wpName(cfg.writePolicy) << "\n"
        << "  Write Alloc  : " << wapName(cfg.writeAllocPolicy) << "\n";
}
