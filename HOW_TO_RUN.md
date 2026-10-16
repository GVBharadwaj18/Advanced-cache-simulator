# How to Run the Cache Simulator

A quick guide to building and running the Multi-Level Cache Simulator and generating visual reports.

---

## 1. Quick Run (Pre-compiled Binary)

If you are on Linux or using **Windows WSL**, the pre-compiled `cache_sim` binary is ready to run immediately.

### On Linux / WSL Terminal:
```bash
# Standard simulation
./cache_sim configs/default.cfg traces/sample.trace

# Compare all replacement policies (LRU, FIFO, Random)
./cache_sim configs/default.cfg traces/sample.trace --compare

# Parallel multithreaded benchmark
./cache_sim configs/default.cfg traces/large.trace --benchmark
```

### From Windows PowerShell (via WSL):
```powershell
# Standard simulation
wsl ./cache_sim configs/default.cfg traces/sample.trace

# Compare policies
wsl ./cache_sim configs/default.cfg traces/sample.trace --compare

# Benchmark mode
wsl ./cache_sim configs/default.cfg traces/large.trace --benchmark
```

---

## 2. Compiling from Source

Requirements: A C++17 capable compiler (`g++` >= 9) and POSIX threads (`-lpthread`).

### Option A: Using Make (Linux / macOS / WSL with `make` installed)
```bash
make            # Build release binary
make run        # Build and run sample trace
make compare    # Run policy comparison
make benchmark  # Run multithreaded benchmark
make clean      # Clean build artifacts
```

### Option B: Direct Compilation with g++
If `make` is not installed, compile all source files directly:
```bash
g++ -std=c++17 -O2 -Iinclude src/Cache.cpp src/Config.cpp src/main.cpp src/Simulator.cpp -o cache_sim -lpthread
```
*(Or from PowerShell: `wsl g++ -std=c++17 -O2 -Iinclude src/Cache.cpp src/Config.cpp src/main.cpp src/Simulator.cpp -o cache_sim -lpthread`)*

---

## 3. Visualizing Results (Charts & Graphs)

The simulator exports CSV metrics into the `results/` directory after each run. You can plot high-resolution dark-themed reports using `visualize.py`.

### Prerequisites:
```bash
pip install matplotlib numpy
```

### Generate Plots:
```bash
# Auto-detect latest CSV and generate report.png in results/
python visualize.py

# Plot a specific result CSV
python visualize.py --csv results/compare_results.csv --out results/report.png

# Generate charts for all CSVs in results/
python visualize.py --all
```

---

## 4. Available Traces and Configs

- **Configs (`configs/`)**:
  - `default.cfg`: 32 KB 4-way L1, 256 KB 8-way L2, 100-cycle DRAM.
  - `high_perf.cfg`: 64 KB 8-way L1, 512 KB 16-way L2, compare mode enabled.

- **Traces (`traces/`)**:
  - `sample.trace`: 300 memory accesses (quick test).
  - `large.trace`: 5,000 memory accesses (benchmark trace).
  - `adversarial_*.trace`: Specific access patterns designed to test LRU thrashing, FIFO Belady anomalies, sweep behavior, and write-heavy loads.
