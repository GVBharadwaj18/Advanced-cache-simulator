# Advanced Multi-Level Cache Simulator

> A high-fidelity, production-quality CPU cache hierarchy simulator written in modern C++17.
> Designed to model real-world memory subsystem behavior with configurable topology,
> multiple replacement policies, and detailed cycle-accurate performance metrics.

---

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Architecture](#architecture)
- [Build Instructions](#build-instructions)
- [Usage](#usage)
- [Configuration Reference](#configuration-reference)
- [Trace File Format](#trace-file-format)
- [Sample Output](#sample-output)
- [Performance Metrics Explained](#performance-metrics-explained)
- [Performance Insights](#performance-insights)
- [Future Improvements](#future-improvements)

> For a quick start guide and commands for Windows/Linux/WSL, see [HOW_TO_RUN.md](HOW_TO_RUN.md).

---

## Overview

This project simulates a two-level CPU memory hierarchy (`CPU → L1 → L2 → Main Memory`)
with configurable cache geometry, replacement policies, and write strategies. It is built to
accurately reproduce cache behavior seen in real processors (e.g., x86-64, ARM Cortex-A, NVIDIA GPU SMs).

The simulator accepts memory trace files, processes every read/write access through the
hierarchy, and produces detailed statistics including hit/miss rates per level, AMAT, total
cycle count, stall cycles, and wall-clock benchmark timing via multithreaded parallel runs.

---

## Features

| Feature | Details |
|---|---|
| **Cache Levels** | L1 + L2, independently configurable |
| **Replacement Policies** | LRU, FIFO, Random |
| **Write Policies** | Write-Back, Write-Through |
| **Write-Allocate** | Write-Allocate, No-Write-Allocate |
| **Associativity** | Direct-Mapped, N-Way, Fully Associative |
| **Compare Mode** | Runs all 3 policies side-by-side, prints comparison table |
| **Benchmark Mode** | Runs all policies in **parallel threads**, measures wall-clock time |
| **CSV Export** | All results auto-exported to `results/` |
| **Coloured Output** | ANSI terminal colours for fast visual scanning |
| **Config Validation** | Power-of-2 checks, size sanity, policy name validation |
| **Error Handling** | Descriptive exceptions for all I/O and config errors |
| **Clean CLI** | `./cache_sim config trace [--compare|--benchmark|--help]` |

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        CPU (Processor)                      │
│                     Issues R/W requests                     │
└───────────────────────────┬─────────────────────────────────┘
                            │
                    ┌───────▼───────┐
                    │   L1 Cache    │  32 KB, 4-way, 64B line
                    │  (4 cycles)   │  LRU / FIFO / RANDOM
                    └───────┬───────┘
                            │ miss
                    ┌───────▼───────┐
                    │   L2 Cache    │  256 KB, 8-way, 64B line
                    │  (12 cycles)  │  Same replacement policy
                    └───────┬───────┘
                            │ miss
                    ┌───────▼───────┐
                    │  Main Memory  │  DRAM  (~100 cycles)
                    └───────────────┘

Write Policies:
  Write-Back       : dirty bit set on write hit; writeback on eviction
  Write-Through    : propagate every write immediately to next level
  Write-Allocate   : on write miss, fetch block first, then update
  No-Write-Allocate: on write miss, write directly to next level
```

### Module Layout

```
Cache-Simulator/
├── src/
│   ├── main.cpp        – CLI entry point, flag parsing
│   ├── Cache.cpp       – CacheLine / CacheSet / Cache implementation
│   ├── Simulator.cpp   – Trace loading, hierarchy orchestration, output
│   └── Config.cpp      – KEY=VALUE config parser + validator
├── include/
│   ├── Cache.h         – CacheLine, CacheSet, CacheStats, Cache class
│   ├── Simulator.h     – MemoryAccess, SimResult, Simulator class
│   └── Config.h        – SimConfig struct, ConfigParser class
├── traces/
│   ├── sample.trace    – 300-access mixed read/write trace
│   └── large.trace     – 5,000-access benchmark trace
├── configs/
│   ├── default.cfg     – Standard 32KB L1 / 256KB L2 config
│   └── high_perf.cfg   – 64KB L1 / 512KB L2, compare mode enabled
├── results/            – Auto-created; CSV exports land here
└── Makefile
```

### Class Hierarchy

```
Cache
  └── owns: vector<CacheSet>
              └── owns: vector<CacheLine>   (valid, dirty, tag, lastUsed)
                        queue<int>           (FIFO insertion order)

Simulator
  ├── creates: Cache l1, Cache l2
  ├── calls:   processAccess() per trace entry
  └── returns: SimResult  (l1Stats, l2Stats, AMAT, cycles, stallCycles)

ConfigParser  (static)
  └── parses → SimConfig  (all cache geometry + policy parameters)
```

---

## Build Instructions

### Requirements

- `g++` ≥ 9.0 (C++17 + `<filesystem>`)
- GNU `make`
- POSIX threads (`-lpthread`)

### Build

```bash
make
```

### Build Variants

```bash
make           # Optimised release build (-O2)
make debug     # Debug build with AddressSanitizer + UBSanitizer
make clean     # Remove build artefacts
```

---

## Usage

```bash
# Standard run
./cache_sim configs/default.cfg traces/sample.trace

# Compare all replacement policies (LRU vs FIFO vs RANDOM)
./cache_sim configs/default.cfg traces/sample.trace --compare

# Multithreaded benchmark (all policies in parallel, timed)
./cache_sim configs/default.cfg traces/large.trace --benchmark

# Help
./cache_sim --help
```

### Convenience targets

```bash
make run        # build + run with sample trace
make compare    # build + run in compare mode
make benchmark  # build + run benchmark on large trace
```

---

## Configuration Reference

```ini
# L1 Cache
L1_SIZE    = 32768     # bytes  (must be power of 2)
L1_ASSOC   = 4         # ways   (1 = direct-mapped)
L1_BLOCK   = 64        # bytes  (must be power of 2)
L1_LATENCY = 4         # cycles

# L2 Cache
L2_SIZE    = 262144    # 256 KB
L2_ASSOC   = 8
L2_BLOCK   = 64
L2_LATENCY = 12

# DRAM
MEMORY_LATENCY = 100   # cycles

# Replacement Policy: LRU | FIFO | RANDOM
POLICY = LRU

# Write Policy: WRITE_BACK | WRITE_THROUGH
WRITE_POLICY = WRITE_BACK

# Write Allocate: WRITE_ALLOCATE | NO_WRITE_ALLOCATE
ALLOC_POLICY = WRITE_ALLOCATE

# Optional modes (TRUE / FALSE)
# BENCHMARK = TRUE
# COMPARE   = TRUE
```

---

## Trace File Format

```
# Lines starting with # are comments
R 0x1A3F00     # Read from address 0x1A3F00
W 0x4B2C10     # Write to address 0x4B2C10
R 0x1A3F00     # Re-read  (L1 hit if block still cached)
```

Addresses may be decimal or `0x`-prefixed hexadecimal.
Operations are case-insensitive (`r`/`R`, `w`/`W`).

---

## Sample Output

```
  ▶  Policy: LRU

  ┌─ L1 Cache Statistics -------------------------------------------┐
  │  Total Accesses          5,000
  │  Reads                   3,676
  │  Writes                  1,324
  │  Read Hits               3,006   ← green
  │  Read Misses               670   ← red
  │  Write Hits              1,059   ← green
  │  Write Misses              265   ← red
  │  Evictions                 530
  │  Dirty Evictions           204
  │  Hit Rate                81.30%  ← green
  │  Miss Rate                18.70% ← red
  └-----------------------------------------------------------------┘

  ┌─ Performance Summary -------------------------------------------┐
  │  AMAT (cycles)            18.42  ← cyan
  │  Total Cycles            92,090
  │  Stall Cycles            72,090
  │  Wall-Clock Runtime (ms)   5.83  ← magenta (benchmark only)
  └-----------------------------------------------------------------┘

  ┌─ Policy Comparison Table ----------------------------------------┐
  │  Policy    L1 HitRate    L2 HitRate    AMAT    Total Cycles
  │  ──────────────────────────────────────────────────────────
  │  LRU       81.30%        20.14%        18.42   92,090
  │  FIFO       79.62%        18.87%        19.91   99,540
  │  RANDOM    77.10%        17.21%        21.67  108,320
  └-----------------------------------------------------------------┘

  ✔ Results exported → results/compare_results.csv
```

---

## Performance Metrics Explained

| Metric | Formula |
|---|---|
| **AMAT** | `L1_latency + L1_miss_rate × (L2_latency + L2_miss_rate × Mem_latency)` |
| **Stall Cycles** | `total_cycles − (num_accesses × L1_latency)` |
| **Hit Rate** | `hits / total_accesses × 100` |
| **L2 Miss Rate** | Fraction of L2 accesses that miss (L2 only sees L1 misses) |

---

## Performance Insights

- **LRU consistently outperforms FIFO and RANDOM** on traces with temporal locality
  because recently used blocks are statistically more likely to be reused soon.

- **Write-Back + Write-Allocate** is the optimal combination for write-heavy workloads:
  dirty blocks batch DRAM writes, and fetching on miss avoids repeated cold misses.

- **Increasing associativity** reduces conflict misses at the cost of higher lookup latency.
  4-way (L1) and 8-way (L2) represent the sweet spot in modern x86 designs.

- **AMAT is the primary figure of merit**: even a 1-cycle improvement in AMAT translates
  to millions of saved cycles in a long-running workload.

---

## Future Improvements

- [ ] L3 cache level and NUMA-aware memory modelling
- [ ] Prefetching: stride-based and stream prefetchers
- [ ] Victim cache between L1 and L2
- [ ] Out-of-order / non-blocking cache (MSHR modelling)
- [ ] Python visualisation script for hit-rate vs. cache-size sweep graphs
- [ ] Pin-based real trace collection from live processes
- [ ] Inclusive vs. exclusive cache hierarchy policy
- [ ] Cache coherency protocol simulation (MESI) for multi-core traces

---

*Built to demonstrate systems-level C++ engineering: memory hierarchy design, OOP modularity,
multithreaded benchmarking, and cycle-accurate performance analysis.*
