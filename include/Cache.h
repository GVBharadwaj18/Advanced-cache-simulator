#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <limits>
#include <random>
#include <queue>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────
//  Enumerations
// ─────────────────────────────────────────────────────────────

enum class ReplacementPolicy { LRU, FIFO, RANDOM };
enum class WritePolicy       { WRITE_BACK, WRITE_THROUGH };
enum class WriteAllocPolicy  { WRITE_ALLOCATE, NO_WRITE_ALLOCATE };

// ─────────────────────────────────────────────────────────────
//  CacheLine  –  one block slot inside a set
// ─────────────────────────────────────────────────────────────
struct CacheLine {
    bool     valid    = false;
    bool     dirty    = false;
    uint64_t tag      = 0;
    uint64_t lastUsed = 0;   // LRU timestamp
    uint64_t loadedAt = 0;   // FIFO order

    CacheLine() = default;
};

// ─────────────────────────────────────────────────────────────
//  CacheSet  –  a collection of ways (lines)
// ─────────────────────────────────────────────────────────────
struct CacheSet {
    std::vector<CacheLine> lines;
    std::queue<int>        fifoOrder;  // tracks insertion order for FIFO

    explicit CacheSet(int ways) : lines(ways) {}
};

// ─────────────────────────────────────────────────────────────
//  CacheStats  –  per-level statistics
// ─────────────────────────────────────────────────────────────
struct CacheStats {
    uint64_t reads       = 0;
    uint64_t writes      = 0;
    uint64_t readHits    = 0;
    uint64_t readMisses  = 0;
    uint64_t writeHits   = 0;
    uint64_t writeMisses = 0;
    uint64_t evictions   = 0;
    uint64_t dirtyEvict  = 0;   // dirty-line write-backs

    uint64_t hits()   const { return readHits   + writeHits;   }
    uint64_t misses() const { return readMisses  + writeMisses; }
    uint64_t total()  const { return reads + writes;            }

    double hitRate()  const { return total() ? 100.0 * hits()   / total() : 0.0; }
    double missRate() const { return total() ? 100.0 * misses() / total() : 0.0; }
};

// ─────────────────────────────────────────────────────────────
//  Cache  –  one configurable cache level
// ─────────────────────────────────────────────────────────────
class Cache {
public:
    // ── Construction ────────────────────────────────────────
    Cache(const std::string& levelName,
          uint64_t          cacheSize,
          uint32_t          blockSize,
          uint32_t          associativity,
          uint32_t          accessLatency,
          ReplacementPolicy rpolicy,
          WritePolicy       wpolicy,
          WriteAllocPolicy  wapolicy);

    // ── Primary interface ───────────────────────────────────
    bool read (uint64_t address, uint64_t timestamp);
    bool write(uint64_t address, uint64_t timestamp);

    // ── Accessors ───────────────────────────────────────────
    const CacheStats& stats()       const { return stats_; }
    const std::string& name()       const { return name_;  }
    uint32_t          accessLatency() const { return latency_; }

    uint32_t numSets()        const { return numSets_;       }
    uint32_t associativity()  const { return assoc_;         }
    uint32_t blockSize()      const { return blockSize_;     }
    uint64_t cacheSize()      const { return cacheSize_;     }
    ReplacementPolicy policy() const { return rPolicy_;      }

    void reset();

private:
    // ── Config ───────────────────────────────────────────────
    std::string      name_;
    uint64_t         cacheSize_;
    uint32_t         blockSize_;
    uint32_t         assoc_;
    uint32_t         numSets_;
    uint32_t         latency_;
    uint32_t         offsetBits_;
    uint32_t         indexBits_;

    ReplacementPolicy rPolicy_;
    WritePolicy       wPolicy_;
    WriteAllocPolicy  waPolicy_;

    // ── Storage ──────────────────────────────────────────────
    std::vector<CacheSet> sets_;
    CacheStats            stats_;
    std::mt19937          rng_;

    // ── Helpers ──────────────────────────────────────────────
    void     decompose(uint64_t addr, uint64_t& tag,
                       uint32_t& setIdx, uint32_t& offset) const;
    int      findLine (const CacheSet& s, uint64_t tag)    const;
    int      selectVictim(CacheSet& s, uint64_t timestamp);
    void     install   (CacheSet& s, int way, uint64_t tag,
                        bool dirty, uint64_t timestamp);
    void     updateLRU (CacheSet& s, int way, uint64_t timestamp);
};
