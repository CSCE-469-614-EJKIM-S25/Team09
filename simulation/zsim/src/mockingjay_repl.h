#ifndef MOCKINGJAY_REPL_H_
#define MOCKINGJAY_REPL_H_

#include "repl_policies.h"
#include "cache_arrays.h"
#include "hash.h"
#include <cmath>

//Adapted from Ishan Shah's ChampSim implementation of Mockingjay
class MockingjayReplPolicy : public ReplPolicy {
private:
    // Cache parameters
    uint32_t numLines;
    uint32_t numSets;
    uint32_t numWays;
    uint32_t numCores;
    
    // Constants calculated from ZSim parameters
    uint32_t LOG2_BLOCK_SIZE;
    uint32_t LOG2_LLC_SET;
    uint32_t LOG2_LLC_SIZE;
    uint32_t LOG2_SAMPLED_SETS;
    
    const uint32_t HISTORY = 8;
    const uint32_t GRANULARITY = 8;
    
    uint32_t INF_RD;
    int32_t INF_ETR;
    uint32_t MAX_RD;
    
    const uint32_t SAMPLED_CACHE_WAYS = 5;
    const uint32_t LOG2_SAMPLED_CACHE_SETS = 4;
    uint32_t SAMPLED_CACHE_TAG_BITS;
    uint32_t PC_SIGNATURE_BITS;
    const uint32_t TIMESTAMP_BITS = 8;
    
    const double TEMP_DIFFERENCE = 1.0/16.0;
    double FLEXMIN_PENALTY;
    
    // ETR counters and clocks
    int8_t* etr;
    uint8_t* etrClock;
    
    // Reuse Distance Predictor
    struct RDPEntry {
        uint32_t reuseDistance;
    };
    
    // Using a simpler array approach instead of unordered_map
    RDPEntry* rdpTable;
    
    // Current timestamp for each set
    uint32_t* currentTimestamp;
    
    // Sampled set map
    bool* isSampledSetMap;
    
    // Sampled cache structures
    struct SampledCacheLine {
        bool valid;
        uint64_t tag;
        uint64_t signature;
        int timestamp;
    };
    
    // Using arrays for the sampled cache instead of unordered_map
    SampledCacheLine* sampledCache;
    uint32_t sampledCacheSize;
    
    // Helper methods
    bool isSampledSet(uint32_t set) {
        return isSampledSetMap[set];
    }
    
    uint64_t crcHash(uint64_t blockAddress) {
        static const uint64_t crcPolynomial = 3988292384ULL;
        uint64_t returnVal = blockAddress;
        for (uint32_t i = 0; i < 3; i++) {
            returnVal = ((returnVal & 1) == 1) ? ((returnVal >> 1) ^ crcPolynomial) : (returnVal >> 1);
        }
        return returnVal;
    }
    
    uint64_t getPCSignature(uint64_t pc, bool hit, bool prefetch, uint32_t coreId) {
        if (numCores == 1) {
            // Single-core implementation
            pc = pc << 1;
            if (hit) {
                pc = pc | 1;
            }
            pc = pc << 1;
            if (prefetch) {
                pc = pc | 1;
            }
            pc = crcHash(pc);
            // Mask to signature bits
            pc = pc & ((1ULL << PC_SIGNATURE_BITS) - 1);
        } else {
            // Multi-core implementation
            pc = pc << 1;
            if (prefetch) {
                pc = pc | 1;
            }
            pc = pc << 2;
            pc = pc | (coreId & 0x3); // Include core ID
            pc = crcHash(pc);
            // Mask to signature bits
            pc = pc & ((1ULL << PC_SIGNATURE_BITS) - 1);
        }
        return pc;
    }
    
    uint32_t getSampledCacheIndex(uint32_t set, uint64_t blockAddr) {
        // Calculate the sampled cache set index
        uint32_t sampledSetId = set & ((1 << LOG2_SAMPLED_SETS) - 1);
        uint32_t blockAddrBits = (uint32_t)(blockAddr & 0xF); // Lower 4 bits of address
        return (sampledSetId << LOG2_SAMPLED_CACHE_SETS) | blockAddrBits;
    }
    
    uint64_t getSampledCacheTag(uint64_t fullAddr) {
        return (fullAddr >> (LOG2_LLC_SET + LOG2_BLOCK_SIZE + LOG2_SAMPLED_CACHE_SETS)) & 
               ((1ULL << SAMPLED_CACHE_TAG_BITS) - 1);
    }
    
    int searchSampledCache(uint64_t blockTag, uint32_t sampledSetIdx) {
        uint32_t baseIdx = sampledSetIdx * SAMPLED_CACHE_WAYS;
        
        for (uint32_t way = 0; way < SAMPLED_CACHE_WAYS; way++) {
            if (sampledCache[baseIdx + way].valid && sampledCache[baseIdx + way].tag == blockTag) {
                return way;
            }
        }
        return -1;
    }
    
    void detrain(uint32_t sampledSetIdx, int way) {
        if (way < 0 || way >= (int)SAMPLED_CACHE_WAYS) {
            return;
        }
        
        uint32_t idx = sampledSetIdx * SAMPLED_CACHE_WAYS + way;
        if (!sampledCache[idx].valid) {
            return;
        }
        
        uint64_t signature = sampledCache[idx].signature;
        
        // Update RDP for this signature with INF_RD
        if (signature < (1ULL << PC_SIGNATURE_BITS)) {
            rdpTable[signature].reuseDistance = MIN(rdpTable[signature].reuseDistance + 1, INF_RD);
        }
        
        sampledCache[idx].valid = false;
    }
    
    int temporalDifference(int init, int sample) {
        if (sample > init) {
            int diff = sample - init;
            double weight = diff * TEMP_DIFFERENCE;
            weight = MIN(1.0, weight);
            return MIN(init + (int)weight, (int)INF_RD);
        } else if (sample < init) {
            int diff = init - sample;
            double weight = diff * TEMP_DIFFERENCE;
            weight = MIN(1.0, weight);
            return MAX(init - (int)weight, 0);
        } else {
            return init;
        }
    }
    
    uint32_t incrementTimestamp(uint32_t input) {
        input++;
        input = input % (1 << TIMESTAMP_BITS);
        return input;
    }
    
    int timeElapsed(int global, int local) {
        if (global >= local) {
            return global - local;
        }
        // Handle wrap-around
        return global + (1 << TIMESTAMP_BITS) - local;
    }

public:
    explicit MockingjayReplPolicy(uint32_t _numLines, uint32_t _numSets) 
        : numLines(_numLines), numSets(_numSets), numWays(_numLines/_numSets) {
        
        numCores = zinfo->numCores;
        
        // Calculate log2 values based on ZSim parameters
        LOG2_BLOCK_SIZE = ilog2(zinfo->lineSize);
        LOG2_LLC_SET = ilog2(numSets);
        LOG2_LLC_SIZE = LOG2_LLC_SET + ilog2(numWays) + LOG2_BLOCK_SIZE;
        LOG2_SAMPLED_SETS = LOG2_LLC_SIZE - 16;
        
        // Calculate derived constants
        INF_RD = numWays * HISTORY - 1;
        INF_ETR = (numWays * HISTORY / GRANULARITY) - 1;
        MAX_RD = INF_RD - 22;
        
        SAMPLED_CACHE_TAG_BITS = 31 - LOG2_LLC_SIZE;
        PC_SIGNATURE_BITS = LOG2_LLC_SIZE - 10;
        
        // Calculate FLEXMIN_PENALTY based on core count
        FLEXMIN_PENALTY = 2.0 - log2(numCores)/4.0;
        
        // Initialize ETR counters and clocks
        etr = gm_calloc<int8_t>(numLines);
        etrClock = gm_calloc<uint8_t>(numSets);
        
        // Initialize timestamps
        currentTimestamp = gm_calloc<uint32_t>(numSets);
        
        // Initialize RDP table
        rdpTable = gm_calloc<RDPEntry>(1 << PC_SIGNATURE_BITS);
        
        // Initialize sampled set map
        isSampledSetMap = gm_calloc<bool>(numSets);
        for (uint32_t set = 0; set < numSets; set++) {
            uint32_t maskLength = LOG2_LLC_SET - LOG2_SAMPLED_SETS;
            uint32_t mask = (1 << maskLength) - 1;
            isSampledSetMap[set] = ((set & mask) == ((set >> (LOG2_LLC_SET - maskLength)) & mask));
        }
        
        // Count sampled sets
        uint32_t numSampledSets = 0;
        for (uint32_t set = 0; set < numSets; set++) {
            if (isSampledSetMap[set]) {
                numSampledSets++;
            }
        }
        
        // Initialize sampled cache
        uint32_t totalSampledLines = numSampledSets * (1 << LOG2_SAMPLED_CACHE_SETS) * SAMPLED_CACHE_WAYS;
        sampledCacheSize = totalSampledLines;
        sampledCache = gm_calloc<SampledCacheLine>(sampledCacheSize);
        
        info("Mockingjay initialized: numCores=%d, LOG2_LLC_SIZE=%d, PC_SIGNATURE_BITS=%d, INF_RD=%d, MAX_RD=%d, FLEXMIN_PENALTY=%.2f",
             numCores, LOG2_LLC_SIZE, PC_SIGNATURE_BITS, INF_RD, MAX_RD, FLEXMIN_PENALTY);
    }
    
    ~MockingjayReplPolicy() {
        gm_free(etr);
        gm_free(etrClock);
        gm_free(currentTimestamp);
        gm_free(rdpTable);
        gm_free(isSampledSetMap);
        gm_free(sampledCache);
    }
    
    void initStats(AggregateStat* parent) override {
        // Not neeeded
    }
    
    void update(uint32_t id, const MemReq* req) override {
        // Extract set and way
        uint32_t set = id / numWays;
        //uint32_t way = id % numWays; //Not used?
        uint32_t cpuId = req->srcId;
        bool isPrefetch = (req->flags & MemReq::PREFETCH);
        bool isHit = (req->type == GETS);
        
        // Skip updating for writebacks
        if (req->type == PUTS || req->type == PUTX) {
            if (!isHit) {
                // Initialize ETR for writebacks
                etr[id] = -INF_ETR;
            }
            return;
        }
        
        // Generate PC signature
        uint64_t pcSignature = getPCSignature(req->pcAddr, isHit, isPrefetch, cpuId);
        
        // Update sampled cache if this is a sampled set
        if (isSampledSet(set)) {
            uint32_t sampledCacheIndex = getSampledCacheIndex(set, req->lineAddr);
            uint64_t sampledCacheTag = getSampledCacheTag(req->lineAddr);
            int sampledCacheWay = searchSampledCache(sampledCacheTag, sampledCacheIndex);
            
            if (sampledCacheWay > -1) {
                // Hit in sampled cache
                uint32_t idx = sampledCacheIndex * SAMPLED_CACHE_WAYS + sampledCacheWay;
                uint64_t lastSignature = sampledCache[idx].signature;
                uint64_t lastTimestamp = sampledCache[idx].timestamp;
                int sample = timeElapsed(currentTimestamp[set], lastTimestamp);
                
                if (sample <= (int)INF_RD) {
                    // Apply FLEXMIN penalty for prefetches
                    if (isPrefetch) {
                        sample = sample * FLEXMIN_PENALTY;
                    }
                    
                    // Update RDP with observed reuse distance
                    if (lastSignature < (1ULL << PC_SIGNATURE_BITS)) {
                        int init = rdpTable[lastSignature].reuseDistance;
                        rdpTable[lastSignature].reuseDistance = temporalDifference(init, sample);
                    }
                    
                    // Clear the entry
                    sampledCache[idx].valid = false;
                }
            }
            
            // Find space for new entry
            int lruWay = -1;
            int lruRd = -1;
            for (uint32_t w = 0; w < SAMPLED_CACHE_WAYS; w++) {
                uint32_t idx = sampledCacheIndex * SAMPLED_CACHE_WAYS + w;
                if (!sampledCache[idx].valid) {
                    lruWay = w;
                    lruRd = INF_RD + 1;
                    break;
                }
                
                uint64_t lastTimestamp = sampledCache[idx].timestamp;
                int sample = timeElapsed(currentTimestamp[set], lastTimestamp);
                if (sample > (int)INF_RD) {
                    lruWay = w;
                    lruRd = INF_RD + 1;
                    detrain(sampledCacheIndex, w);
                } else if (sample > lruRd) {
                    lruWay = w;
                    lruRd = sample;
                }
            }
            
            detrain(sampledCacheIndex, lruWay);
            
            // Insert new entry
            if (lruWay >= 0) {
                uint32_t idx = sampledCacheIndex * SAMPLED_CACHE_WAYS + lruWay;
                sampledCache[idx].valid = true;
                sampledCache[idx].signature = pcSignature;
                sampledCache[idx].tag = sampledCacheTag;
                sampledCache[idx].timestamp = currentTimestamp[set];
            }
            
            // Increment timestamp
            currentTimestamp[set] = incrementTimestamp(currentTimestamp[set]);
        }
        
        // Age lines in the set
        if (etrClock[set] == GRANULARITY) {
            for (uint32_t w = 0; w < numWays; w++) {
                uint32_t lineId = set * numWays + w;
                if (lineId != id && abs(etr[lineId]) < INF_ETR) {
                    etr[lineId]--;
                }
            }
            etrClock[set] = 0;
        }
        etrClock[set]++;
        
        // Update ETR for the accessed line
        if (pcSignature >= (1ULL << PC_SIGNATURE_BITS) || rdpTable[pcSignature].reuseDistance == 0) {
            if (numCores == 1) {
                etr[id] = 0;
            } else {
                etr[id] = INF_ETR;
            }
        } else {
            if (rdpTable[pcSignature].reuseDistance > MAX_RD) {
                etr[id] = INF_ETR;
            } else {
                etr[id] = rdpTable[pcSignature].reuseDistance / GRANULARITY;
            }
        }
    }
    
    void replaced(uint32_t id) override {
        // Reset ETR when a line is replaced
        etr[id] = 0;
    }
    
    template <typename C>
    uint32_t rank(const MemReq* req, C cands) {
        // Check if we should bypass based on predicted reuse distance
        if (req && req->type != PUTS && req->type != PUTX) { // Not a writeback
            uint64_t pcSignature = getPCSignature(req->pcAddr, false, (req->flags & MemReq::PREFETCH), req->srcId);
            if (pcSignature < (1ULL << PC_SIGNATURE_BITS) && 
                rdpTable[pcSignature].reuseDistance > MAX_RD) {
                // This line has a very long predicted reuse distance - bypass it
                return (uint32_t)-1; // Special value indicating bypass
            }
        }
        
        // Find the line with maximum absolute ETR value
        uint32_t bestCand = -1;
        int32_t maxEtr = -1;
        
        for (auto ci = cands.begin(); ci != cands.end(); ci.inc()) {
            uint32_t candId = *ci;
            
            // Skip invalid lines (already handled in ZSim)
            if (!cc->isValid(candId)) {
                bestCand = candId;
                break;
            }
            
            // Get absolute ETR value
            int32_t absEtr = abs(etr[candId]);
            
            // Prioritize lines with negative ETR (exceeded their ETA)
            if (absEtr > maxEtr || (absEtr == maxEtr && etr[candId] < 0)) {
                maxEtr = absEtr;
                bestCand = candId;
            }
        }
        
        return bestCand;
    }
    
    DECL_RANK_BINDINGS;
};

#endif // MOCKINGJAY_REPL_H_