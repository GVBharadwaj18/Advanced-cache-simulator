#pragma once

#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <functional>
#include "Cache.h"
#include "Config.h"

// ─────────────────────────────────────────────────────────────
//  MemoryAccess  –  one entry from the trace file
// ─────────────────────────────────────────────────────────────
struct MemoryAccess {
    char     type;      // 'R' or 'W'
    uint64_t address;
};

// ─────────────────────────────────────────────────────────────
//  SimResult  –  aggregate result for one simulation run
// ─────────────────────────────────────────────────────────────
struct SimResult {
    std::string       policyName;
    CacheStats        l1Stats;
    CacheStats        l2Stats;
    uint64_t          totalCycles    = 0;
    uint64_t          stallCycles    = 0;
    double            amat           = 0.0;   // Average Memory Access Time
    double            runtimeMs      = 0.0;   // wall-clock time (benchmark)
};

// ─────────────────────────────────────────────────────────────
//  Simulator  –  orchestrates the full simulation
// ─────────────────────────────────────────────────────────────
class Simulator {
public:
    explicit Simulator(const SimConfig& cfg);

    // ── Run ─────────────────────────────────────────────────
    SimResult run(const std::vector<MemoryAccess>& trace,
                  ReplacementPolicy overridePolicy,
                  const std::string& policyLabel);

    // ── Top-level entry points ───────────────────────────────
    void runTrace       (const std::string& traceFile);
    void runBenchmark   (const std::string& traceFile);
    void runCompare     (const std::string& traceFile);

    // ── Output ──────────────────────────────────────────────
    static void printResult (const SimResult& r);
    static void exportCSV   (const std::vector<SimResult>& results,
                             const std::string& outPath);

    // ── Trace loading ────────────────────────────────────────
    static std::vector<MemoryAccess> loadTrace(const std::string& path);

private:
    SimConfig cfg_;

    // ── Per-access simulation step ───────────────────────────
    void processAccess(const MemoryAccess& ma,
                       Cache& l1, Cache& l2,
                       uint64_t timestamp,
                       uint64_t& cycles);

    // ── AMAT formula ─────────────────────────────────────────
    static double computeAMAT(const SimResult& r,
                              const SimConfig& cfg);
};
