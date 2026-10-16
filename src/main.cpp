#include <iostream>
#include <string>
#include <stdexcept>
#include <cstring>
#include "../include/Config.h"
#include "../include/Simulator.h"

// ─────────────────────────────────────────────────────────────
//  ANSI helpers (duplicated here for standalone readability)
// ─────────────────────────────────────────────────────────────
namespace Color {
    constexpr const char* RESET  = "\033[0m";
    constexpr const char* BOLD   = "\033[1m";
    constexpr const char* CYAN   = "\033[36m";
    constexpr const char* YELLOW = "\033[33m";
    constexpr const char* RED    = "\033[31m";
}

// ─────────────────────────────────────────────────────────────
//  Usage
// ─────────────────────────────────────────────────────────────
static void usage(const char* prog) {
    std::cerr
        << Color::BOLD << "\nUsage:\n" << Color::RESET
        << "  " << Color::CYAN << prog << Color::RESET
        << " <config.txt> <trace.txt> [options]\n\n"
        << Color::BOLD << "Options:\n" << Color::RESET
        << "  --benchmark    Run all policies in parallel and time them\n"
        << "  --compare      Run and compare all replacement policies\n"
        << "  --help         Show this message\n\n"
        << Color::BOLD << "Examples:\n" << Color::RESET
        << "  " << prog << " configs/default.cfg traces/sample.trace\n"
        << "  " << prog << " configs/default.cfg traces/sample.trace --compare\n"
        << "  " << prog << " configs/default.cfg traces/large.trace --benchmark\n\n";
}

// ─────────────────────────────────────────────────────────────
//  main
// ─────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    if (argc < 3) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    // ── Parse CLI flags ──────────────────────────────────────
    bool flagBenchmark = false;
    bool flagCompare   = false;
    bool flagHelp      = false;

    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--benchmark") == 0) flagBenchmark = true;
        else if (std::strcmp(argv[i], "--compare")   == 0) flagCompare   = true;
        else if (std::strcmp(argv[i], "--help")      == 0) flagHelp      = true;
        else {
            std::cerr << Color::RED << "Unknown option: " << argv[i]
                      << Color::RESET << "\n";
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (flagHelp) { usage(argv[0]); return EXIT_SUCCESS; }

    const std::string configFile = argv[1];
    const std::string traceFile  = argv[2];

    try {
        // ── Load & validate config ───────────────────────────
        SimConfig cfg = ConfigParser::parse(configFile);
        ConfigParser::validate(cfg);

        // CLI flags override config file modes
        if (flagBenchmark) cfg.benchmarkMode = true;
        if (flagCompare)   cfg.compareMode   = true;

        // ── Run simulation ───────────────────────────────────
        Simulator sim(cfg);

        if (cfg.benchmarkMode)
            sim.runBenchmark(traceFile);
        else if (cfg.compareMode)
            sim.runCompare(traceFile);
        else
            sim.runTrace(traceFile);

    } catch (const std::exception& e) {
        std::cerr << "\n" << Color::RED << Color::BOLD
                  << "  ✘ Error: " << Color::RESET
                  << Color::RED << e.what() << Color::RESET << "\n\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
