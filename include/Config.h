#pragma once

#include <string>
#include <unordered_map>
#include <stdexcept>
#include "Cache.h"

// ─────────────────────────────────────────────────────────────
//  SimConfig  –  all parameters parsed from config file
// ─────────────────────────────────────────────────────────────
struct SimConfig {
    // L1
    uint64_t l1Size       = 32768;
    uint32_t l1Assoc      = 4;
    uint32_t l1Block      = 64;
    uint32_t l1Latency    = 4;

    // L2
    uint64_t l2Size       = 262144;
    uint32_t l2Assoc      = 8;
    uint32_t l2Block      = 64;
    uint32_t l2Latency    = 12;

    // Main memory
    uint32_t memLatency   = 100;

    // Policies
    ReplacementPolicy replacementPolicy = ReplacementPolicy::LRU;
    WritePolicy       writePolicy       = WritePolicy::WRITE_BACK;
    WriteAllocPolicy  writeAllocPolicy  = WriteAllocPolicy::WRITE_ALLOCATE;

    // Benchmark / compare mode
    bool benchmarkMode    = false;
    bool compareMode      = false;   // compare all replacement policies
};

// ─────────────────────────────────────────────────────────────
//  ConfigParser  –  parses KEY=VALUE config files
// ─────────────────────────────────────────────────────────────
class ConfigParser {
public:
    static SimConfig parse(const std::string& filePath);
    static void      validate(const SimConfig& cfg);
    static void      print(const SimConfig& cfg);

private:
    static std::unordered_map<std::string,std::string>
           readKV(const std::string& filePath);

    static ReplacementPolicy parseRP (const std::string& s);
    static WritePolicy       parseWP (const std::string& s);
    static WriteAllocPolicy  parseWAP(const std::string& s);
    static uint64_t          parseU64(const std::string& s);
    static uint32_t          parseU32(const std::string& s);
};
