#include "../include/Simulator.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <filesystem>

// ─────────────────────────────────────────────────────────────
//  ANSI colour helpers
// ─────────────────────────────────────────────────────────────
namespace Color {
    constexpr const char* RESET   = "\033[0m";
    constexpr const char* BOLD    = "\033[1m";
    constexpr const char* GREEN   = "\033[32m";
    constexpr const char* CYAN    = "\033[36m";
    constexpr const char* YELLOW  = "\033[33m";
    constexpr const char* RED     = "\033[31m";
    constexpr const char* MAGENTA = "\033[35m";
    constexpr const char* BLUE    = "\033[34m";
    constexpr const char* WHITE   = "\033[37m";
    constexpr const char* DIM     = "\033[2m";
}

static void banner() {
    std::cout << Color::CYAN << Color::BOLD
    << R"(
 ██████╗ █████╗  ██████╗██╗  ██╗███████╗    ███████╗██╗███╗   ███╗
██╔════╝██╔══██╗██╔════╝██║  ██║██╔════╝    ██╔════╝██║████╗ ████║
██║     ███████║██║     ███████║█████╗      ███████╗██║██╔████╔██║
██║     ██╔══██║██║     ██╔══██║██╔══╝      ╚════██║██║██║╚██╔╝██║
╚██████╗██║  ██║╚██████╗██║  ██║███████╗    ███████║██║██║ ╚═╝ ██║
 ╚═════╝╚═╝  ╚═╝ ╚═════╝╚═╝  ╚═╝╚══════╝    ╚══════╝╚═╝╚═╝     ╚═╝
)" << Color::RESET
    << Color::DIM << "  Advanced Multi-Level Cache Simulator  |  C++17  |  v2.0\n"
    << Color::RESET
    << "  " << std::string(65, '-') << "\n\n";
}

static void sectionHeader(const std::string& title) {
    std::cout << "\n" << Color::BOLD << Color::BLUE
              << "  ┌─ " << title << " " << std::string(58 - title.size(), '-') << "┐\n"
              << Color::RESET;
}

static void sectionFooter() {
    std::cout << Color::BLUE << Color::BOLD
              << "  └" << std::string(63, '-') << "┘\n" << Color::RESET;
}

// Print a key-value stat row
static void statRow(const char* label, const std::string& value,
                    const char* color = Color::WHITE) {
    std::cout << Color::DIM << "  │  " << Color::RESET
              << std::left << std::setw(32) << label
              << color << Color::BOLD << value
              << Color::RESET << "\n";
}

// ─────────────────────────────────────────────────────────────
//  Simulator constructor
// ─────────────────────────────────────────────────────────────
Simulator::Simulator(const SimConfig& cfg) : cfg_(cfg) {}

// ─────────────────────────────────────────────────────────────
//  loadTrace  –  parse trace file into MemoryAccess vector
// ─────────────────────────────────────────────────────────────
std::vector<MemoryAccess> Simulator::loadTrace(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("Cannot open trace file: " + path);

    std::vector<MemoryAccess> trace;
    std::string line;
    int lineNo = 0;

    while (std::getline(file, line)) {
        ++lineNo;
        // strip inline comments
        auto commentPos = line.find('#');
        if (commentPos != std::string::npos)
            line = line.substr(0, commentPos);

        std::istringstream iss(line);
        char   type;
        std::string addrStr;

        if (!(iss >> type >> addrStr)) continue; // blank/comment

        type = static_cast<char>(std::toupper(static_cast<unsigned char>(type)));
        if (type != 'R' && type != 'W')
            throw std::runtime_error("Trace line " + std::to_string(lineNo)
                                     + ": expected 'R' or 'W', got: " + type);

        uint64_t addr;
        try {
            addr = std::stoull(addrStr, nullptr, 0);
        } catch (...) {
            throw std::runtime_error("Trace line " + std::to_string(lineNo)
                                     + ": bad address: " + addrStr);
        }
        trace.push_back({type, addr});
    }

    if (trace.empty())
        throw std::runtime_error("Trace file is empty: " + path);

    return trace;
}

// ─────────────────────────────────────────────────────────────
//  processAccess  –  one memory operation through L1 → L2 → Mem
// ─────────────────────────────────────────────────────────────
void Simulator::processAccess(const MemoryAccess& ma,
                               Cache& l1, Cache& l2,
                               uint64_t timestamp,
                               uint64_t& cycles)
{
    if (ma.type == 'R') {
        bool l1Hit = l1.read(ma.address, timestamp);
        if (l1Hit) {
            cycles += l1.accessLatency();
            return;
        }
        // L1 miss → try L2
        cycles += l1.accessLatency();
        bool l2Hit = l2.read(ma.address, timestamp);
        cycles += l2.accessLatency();
        if (!l2Hit) {
            cycles += cfg_.memLatency; // go to DRAM
        }
    } else { // Write
        bool l1Hit = l1.write(ma.address, timestamp);
        if (l1Hit) {
            cycles += l1.accessLatency();
            if (cfg_.writePolicy == WritePolicy::WRITE_THROUGH) {
                // Must propagate to L2
                l2.write(ma.address, timestamp);
                cycles += l2.accessLatency();
            }
            return;
        }
        // L1 miss
        cycles += l1.accessLatency();
        bool l2Hit = l2.write(ma.address, timestamp);
        cycles += l2.accessLatency();
        if (!l2Hit) {
            cycles += cfg_.memLatency;
        }
    }
}

// ─────────────────────────────────────────────────────────────
//  computeAMAT
//  AMAT = L1_hit_time + L1_miss_rate × (L2_hit_time + L2_miss_rate × Mem_time)
// ─────────────────────────────────────────────────────────────
double Simulator::computeAMAT(const SimResult& r, const SimConfig& cfg) {
    double l1MissRate = r.l1Stats.total() ?
        static_cast<double>(r.l1Stats.misses()) / r.l1Stats.total() : 0.0;
    double l2MissRate = r.l2Stats.total() ?
        static_cast<double>(r.l2Stats.misses()) / r.l2Stats.total() : 0.0;

    return cfg.l1Latency
         + l1MissRate * (cfg.l2Latency + l2MissRate * cfg.memLatency);
}

// ─────────────────────────────────────────────────────────────
//  run  –  core simulation loop
// ─────────────────────────────────────────────────────────────
SimResult Simulator::run(const std::vector<MemoryAccess>& trace,
                          ReplacementPolicy overridePolicy,
                          const std::string& policyLabel)
{
    Cache l1("L1", cfg_.l1Size, cfg_.l1Block, cfg_.l1Assoc,
             cfg_.l1Latency, overridePolicy,
             cfg_.writePolicy, cfg_.writeAllocPolicy);

    Cache l2("L2", cfg_.l2Size, cfg_.l2Block, cfg_.l2Assoc,
             cfg_.l2Latency, overridePolicy,
             cfg_.writePolicy, cfg_.writeAllocPolicy);

    uint64_t cycles = 0;
    uint64_t ts     = 0;

    for (const auto& ma : trace)
        processAccess(ma, l1, l2, ++ts, cycles);

    SimResult result;
    result.policyName  = policyLabel;
    result.l1Stats     = l1.stats();
    result.l2Stats     = l2.stats();
    result.totalCycles = cycles;
    result.amat        = computeAMAT(result, cfg_);

    uint64_t idealCycles = static_cast<uint64_t>(trace.size()) * cfg_.l1Latency;
    result.stallCycles   = (cycles > idealCycles) ? cycles - idealCycles : 0;

    return result;
}

// ─────────────────────────────────────────────────────────────
//  printResult  –  coloured terminal output
// ─────────────────────────────────────────────────────────────
void Simulator::printResult(const SimResult& r) {
    auto pct = [](double v) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << v << "%";
        return ss.str();
    };
    auto comma = [](uint64_t v) {
        std::string s = std::to_string(v);
        int n = static_cast<int>(s.size());
        for (int i = n - 3; i > 0; i -= 3)
            s.insert(i, ",");
        return s;
    };
    auto fix2 = [](double v) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << v;
        return ss.str();
    };

    // ── Policy header ────────────────────────────────────────
    std::cout << "\n  " << Color::MAGENTA << Color::BOLD
              << "▶  Policy: " << r.policyName
              << Color::RESET << "\n";

    // ── L1 Stats ─────────────────────────────────────────────
    sectionHeader("L1 Cache Statistics");
    statRow("Total Accesses",  comma(r.l1Stats.total()));
    statRow("Reads",           comma(r.l1Stats.reads));
    statRow("Writes",          comma(r.l1Stats.writes));
    statRow("Read Hits",       comma(r.l1Stats.readHits),  Color::GREEN);
    statRow("Read Misses",     comma(r.l1Stats.readMisses),Color::RED);
    statRow("Write Hits",      comma(r.l1Stats.writeHits), Color::GREEN);
    statRow("Write Misses",    comma(r.l1Stats.writeMisses),Color::RED);
    statRow("Evictions",       comma(r.l1Stats.evictions), Color::YELLOW);
    statRow("Dirty Evictions", comma(r.l1Stats.dirtyEvict),Color::YELLOW);
    statRow("Hit Rate",        pct(r.l1Stats.hitRate()),   Color::GREEN);
    statRow("Miss Rate",       pct(r.l1Stats.missRate()),  Color::RED);
    sectionFooter();

    // ── L2 Stats ─────────────────────────────────────────────
    sectionHeader("L2 Cache Statistics");
    statRow("Total Accesses",  comma(r.l2Stats.total()));
    statRow("Reads",           comma(r.l2Stats.reads));
    statRow("Writes",          comma(r.l2Stats.writes));
    statRow("Read Hits",       comma(r.l2Stats.readHits),  Color::GREEN);
    statRow("Read Misses",     comma(r.l2Stats.readMisses),Color::RED);
    statRow("Write Hits",      comma(r.l2Stats.writeHits), Color::GREEN);
    statRow("Write Misses",    comma(r.l2Stats.writeMisses),Color::RED);
    statRow("Evictions",       comma(r.l2Stats.evictions), Color::YELLOW);
    statRow("Dirty Evictions", comma(r.l2Stats.dirtyEvict),Color::YELLOW);
    statRow("Hit Rate",        pct(r.l2Stats.hitRate()),   Color::GREEN);
    statRow("Miss Rate",       pct(r.l2Stats.missRate()),  Color::RED);
    sectionFooter();

    // ── Performance ───────────────────────────────────────────
    sectionHeader("Performance Summary");
    statRow("AMAT (cycles)",         fix2(r.amat),           Color::CYAN);
    statRow("Total Cycles",          comma(r.totalCycles),   Color::CYAN);
    statRow("Stall Cycles",          comma(r.stallCycles),   Color::RED);
    if (r.runtimeMs > 0.0)
        statRow("Wall-Clock Runtime (ms)", fix2(r.runtimeMs),Color::MAGENTA);
    sectionFooter();
}

// ─────────────────────────────────────────────────────────────
//  exportCSV
// ─────────────────────────────────────────────────────────────
void Simulator::exportCSV(const std::vector<SimResult>& results,
                           const std::string& outPath)
{
    std::ofstream f(outPath);
    if (!f.is_open())
        throw std::runtime_error("Cannot open output file: " + outPath);

    // Header
    f << "policy,total_accesses,reads,writes,"
      << "l1_read_hits,l1_read_misses,l1_write_hits,l1_write_misses,"
      << "l1_hit_rate,l1_miss_rate,"
      << "l2_read_hits,l2_read_misses,l2_write_hits,l2_write_misses,"
      << "l2_hit_rate,l2_miss_rate,"
      << "amat,total_cycles,stall_cycles,runtime_ms\n";

    for (const auto& r : results) {
        f << r.policyName << ","
          << r.l1Stats.total()    << ","
          << r.l1Stats.reads      << ","
          << r.l1Stats.writes     << ","
          << r.l1Stats.readHits   << ","
          << r.l1Stats.readMisses << ","
          << r.l1Stats.writeHits  << ","
          << r.l1Stats.writeMisses << ","
          << std::fixed << std::setprecision(4)
          << r.l1Stats.hitRate()  << ","
          << r.l1Stats.missRate() << ","
          << r.l2Stats.readHits   << ","
          << r.l2Stats.readMisses << ","
          << r.l2Stats.writeHits  << ","
          << r.l2Stats.writeMisses << ","
          << r.l2Stats.hitRate()  << ","
          << r.l2Stats.missRate() << ","
          << r.amat         << ","
          << r.totalCycles  << ","
          << r.stallCycles  << ","
          << r.runtimeMs    << "\n";
    }

    std::cout << "\n  " << Color::GREEN << "✔ " << Color::RESET
              << "Results exported → " << Color::CYAN << outPath
              << Color::RESET << "\n";
}

// ─────────────────────────────────────────────────────────────
//  runTrace  –  single policy run
// ─────────────────────────────────────────────────────────────
void Simulator::runTrace(const std::string& traceFile) {
    banner();

    std::cout << Color::BOLD << "  Configuration\n" << Color::RESET;
    ConfigParser::print(cfg_);

    std::cout << "\n  " << Color::DIM << "Loading trace: "
              << Color::RESET << traceFile << " ... ";
    auto trace = loadTrace(traceFile);
    std::cout << Color::GREEN << trace.size() << " accesses\n" << Color::RESET;

    auto policyName = [](ReplacementPolicy p) -> const char* {
        switch(p) {
            case ReplacementPolicy::LRU:    return "LRU";
            case ReplacementPolicy::FIFO:   return "FIFO";
            case ReplacementPolicy::RANDOM: return "RANDOM";
        }
        return "?";
    };

    auto result = run(trace, cfg_.replacementPolicy,
                      policyName(cfg_.replacementPolicy));
    printResult(result);

    // Auto-export CSV
    std::filesystem::create_directories("results");
    exportCSV({result}, "results/simulation_result.csv");
}

// ─────────────────────────────────────────────────────────────
//  runBenchmark  –  multithreaded timing run
// ─────────────────────────────────────────────────────────────
void Simulator::runBenchmark(const std::string& traceFile) {
    banner();
    std::cout << Color::YELLOW << Color::BOLD
              << "  ⚡ BENCHMARK MODE – Multithreaded\n" << Color::RESET;

    auto trace = loadTrace(traceFile);
    std::cout << "  Trace size: " << trace.size() << " accesses\n\n";

    std::vector<ReplacementPolicy> policies = {
        ReplacementPolicy::LRU,
        ReplacementPolicy::FIFO,
        ReplacementPolicy::RANDOM
    };
    std::vector<const char*> names = {"LRU", "FIFO", "RANDOM"};

    std::vector<SimResult> results(policies.size());
    std::vector<std::thread> threads;
    std::mutex printMtx;

    for (size_t i = 0; i < policies.size(); ++i) {
        threads.emplace_back([&, i]() {
            auto t0 = std::chrono::high_resolution_clock::now();
            results[i] = run(trace, policies[i], names[i]);
            auto t1 = std::chrono::high_resolution_clock::now();
            results[i].runtimeMs =
                std::chrono::duration<double, std::milli>(t1 - t0).count();

            std::lock_guard<std::mutex> lk(printMtx);
            std::cout << "  " << Color::GREEN << "✔ " << Color::RESET
                      << names[i] << " done in "
                      << Color::CYAN << std::fixed << std::setprecision(2)
                      << results[i].runtimeMs << " ms\n" << Color::RESET;
        });
    }
    for (auto& t : threads) t.join();

    for (const auto& r : results) printResult(r);

    std::filesystem::create_directories("results");
    exportCSV(results, "results/benchmark_results.csv");
}

// ─────────────────────────────────────────────────────────────
//  runCompare  –  side-by-side policy comparison table
// ─────────────────────────────────────────────────────────────
void Simulator::runCompare(const std::string& traceFile) {
    banner();
    std::cout << Color::YELLOW << Color::BOLD
              << "  ⚖ COMPARE MODE – All Replacement Policies\n" << Color::RESET;

    auto trace = loadTrace(traceFile);

    std::vector<std::pair<ReplacementPolicy,const char*>> policies = {
        {ReplacementPolicy::LRU,    "LRU"},
        {ReplacementPolicy::FIFO,   "FIFO"},
        {ReplacementPolicy::RANDOM, "RANDOM"},
    };

    std::vector<SimResult> results;
    for (auto& [p, name] : policies)
        results.push_back(run(trace, p, name));

    // Print each
    for (const auto& r : results) printResult(r);

    // Comparison table
    sectionHeader("Policy Comparison Table");
    std::cout << Color::BOLD
              << "  │  " << std::left << std::setw(10) << "Policy"
              << std::setw(14) << "L1 HitRate"
              << std::setw(14) << "L2 HitRate"
              << std::setw(12) << "AMAT"
              << std::setw(16) << "Total Cycles"
              << "\n" << Color::RESET;
    std::cout << "  │  " << std::string(62, '-') << "\n";

    for (const auto& r : results) {
        std::cout << "  │  "
                  << Color::CYAN << std::left << std::setw(10) << r.policyName << Color::RESET
                  << std::setw(14) << (std::to_string(r.l1Stats.hitRate()).substr(0,5) + "%")
                  << std::setw(14) << (std::to_string(r.l2Stats.hitRate()).substr(0,5) + "%")
                  << std::setw(12) << std::to_string(r.amat).substr(0,6)
                  << std::setw(16) << r.totalCycles
                  << "\n";
    }
    sectionFooter();

    std::filesystem::create_directories("results");
    exportCSV(results, "results/compare_results.csv");
}
