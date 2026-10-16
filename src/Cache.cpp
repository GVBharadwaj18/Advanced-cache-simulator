#include "../include/Cache.h"
#include <cmath>
#include <stdexcept>
#include <cassert>
#include <algorithm>

// ─────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────
static uint32_t log2i(uint64_t v) {
    if (v == 0) throw std::invalid_argument("log2(0) undefined");
    uint32_t bits = 0;
    while (v > 1) { v >>= 1; ++bits; }
    return bits;
}

static bool isPowerOfTwo(uint64_t v) {
    return v && !(v & (v - 1));
}

// ─────────────────────────────────────────────────────────────
//  Cache  –  constructor
// ─────────────────────────────────────────────────────────────
Cache::Cache(const std::string& levelName,
             uint64_t          cacheSize,
             uint32_t          blockSize,
             uint32_t          associativity,
             uint32_t          accessLatency,
             ReplacementPolicy rpolicy,
             WritePolicy       wpolicy,
             WriteAllocPolicy  wapolicy)
    : name_(levelName)
    , cacheSize_(cacheSize)
    , blockSize_(blockSize)
    , assoc_(associativity)
    , latency_(accessLatency)
    , rPolicy_(rpolicy)
    , wPolicy_(wpolicy)
    , waPolicy_(wapolicy)
    , rng_(std::random_device{}())
{
    if (!isPowerOfTwo(blockSize))
        throw std::invalid_argument(name_ + ": block size must be a power of 2");
    if (!isPowerOfTwo(cacheSize))
        throw std::invalid_argument(name_ + ": cache size must be a power of 2");
    if (cacheSize < blockSize * associativity)
        throw std::invalid_argument(name_ + ": cache too small for given assoc/block");

    numSets_    = static_cast<uint32_t>(cacheSize / (blockSize * associativity));
    offsetBits_ = log2i(blockSize);
    indexBits_  = log2i(numSets_);

    // Allocate sets
    sets_.reserve(numSets_);
    for (uint32_t i = 0; i < numSets_; ++i)
        sets_.emplace_back(assoc_);
}

// ─────────────────────────────────────────────────────────────
//  Address decomposition
// ─────────────────────────────────────────────────────────────
void Cache::decompose(uint64_t addr, uint64_t& tag,
                      uint32_t& setIdx, uint32_t& offset) const
{
    offset  = static_cast<uint32_t>(addr & ((1u << offsetBits_) - 1));
    setIdx  = static_cast<uint32_t>((addr >> offsetBits_) & ((1u << indexBits_) - 1));
    tag     = addr >> (offsetBits_ + indexBits_);
}

// ─────────────────────────────────────────────────────────────
//  findLine  –  returns way index if tag found and valid, else -1
// ─────────────────────────────────────────────────────────────
int Cache::findLine(const CacheSet& s, uint64_t tag) const {
    for (int w = 0; w < static_cast<int>(assoc_); ++w)
        if (s.lines[w].valid && s.lines[w].tag == tag)
            return w;
    return -1;
}

// ─────────────────────────────────────────────────────────────
//  updateLRU  –  bump timestamp for the accessed way
// ─────────────────────────────────────────────────────────────
void Cache::updateLRU(CacheSet& s, int way, uint64_t timestamp) {
    s.lines[way].lastUsed = timestamp;
}

// ─────────────────────────────────────────────────────────────
//  selectVictim  –  choose a way to evict
// ─────────────────────────────────────────────────────────────
int Cache::selectVictim(CacheSet& s, uint64_t /*timestamp*/) {
    // 1. Prefer an invalid (empty) slot
    for (int w = 0; w < static_cast<int>(assoc_); ++w)
        if (!s.lines[w].valid) return w;

    // 2. Policy-based eviction
    switch (rPolicy_) {
        case ReplacementPolicy::LRU: {
            int    victim = 0;
            uint64_t oldest = s.lines[0].lastUsed;
            for (int w = 1; w < static_cast<int>(assoc_); ++w)
                if (s.lines[w].lastUsed < oldest) {
                    oldest = s.lines[w].lastUsed;
                    victim = w;
                }
            return victim;
        }
        case ReplacementPolicy::FIFO: {
            if (!s.fifoOrder.empty()) {
                int victim = s.fifoOrder.front();
                s.fifoOrder.pop();
                return victim;
            }
            return 0;
        }
        case ReplacementPolicy::RANDOM: {
            std::uniform_int_distribution<int> dist(0, static_cast<int>(assoc_) - 1);
            return dist(rng_);
        }
    }
    return 0;
}

// ─────────────────────────────────────────────────────────────
//  install  –  load a block into a way
// ─────────────────────────────────────────────────────────────
void Cache::install(CacheSet& s, int way, uint64_t tag,
                    bool dirty, uint64_t timestamp)
{
    CacheLine& line = s.lines[way];

    // Eviction bookkeeping
    if (line.valid) {
        ++stats_.evictions;
        if (line.dirty) ++stats_.dirtyEvict;
    }

    line.valid    = true;
    line.dirty    = dirty;
    line.tag      = tag;
    line.lastUsed = timestamp;
    line.loadedAt = timestamp;

    if (rPolicy_ == ReplacementPolicy::FIFO)
        s.fifoOrder.push(way);
}

// ─────────────────────────────────────────────────────────────
//  read  –  simulate a read access
// ─────────────────────────────────────────────────────────────
bool Cache::read(uint64_t address, uint64_t timestamp) {
    uint64_t tag; uint32_t setIdx, offset;
    decompose(address, tag, setIdx, offset);

    CacheSet& s   = sets_[setIdx];
    int        way = findLine(s, tag);
    ++stats_.reads;

    if (way >= 0) {
        // Hit
        ++stats_.readHits;
        updateLRU(s, way, timestamp);
        return true;
    }

    // Miss – bring block in (always on read)
    ++stats_.readMisses;
    int victim = selectVictim(s, timestamp);
    install(s, victim, tag, /*dirty=*/false, timestamp);
    return false;
}

// ─────────────────────────────────────────────────────────────
//  write  –  simulate a write access
// ─────────────────────────────────────────────────────────────
bool Cache::write(uint64_t address, uint64_t timestamp) {
    uint64_t tag; uint32_t setIdx, offset;
    decompose(address, tag, setIdx, offset);

    CacheSet& s   = sets_[setIdx];
    int        way = findLine(s, tag);
    ++stats_.writes;

    if (way >= 0) {
        // Hit
        ++stats_.writeHits;
        updateLRU(s, way, timestamp);
        if (wPolicy_ == WritePolicy::WRITE_BACK)
            s.lines[way].dirty = true;
        // Write-through: data goes to next level (handled by Simulator)
        return true;
    }

    // Miss
    ++stats_.writeMisses;

    if (waPolicy_ == WriteAllocPolicy::WRITE_ALLOCATE) {
        // Fetch block, then update
        int victim = selectVictim(s, timestamp);
        install(s, victim, tag, /*dirty=*/(wPolicy_ == WritePolicy::WRITE_BACK), timestamp);
    }
    // No-write-allocate: write goes straight to next level, no fetch
    return false;
}

// ─────────────────────────────────────────────────────────────
//  reset
// ─────────────────────────────────────────────────────────────
void Cache::reset() {
    stats_ = CacheStats{};
    for (auto& s : sets_) {
        for (auto& line : s.lines) line = CacheLine{};
        while (!s.fifoOrder.empty()) s.fifoOrder.pop();
    }
}
