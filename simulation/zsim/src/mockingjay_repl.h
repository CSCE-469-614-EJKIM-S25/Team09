#ifndef MOCKINGJAY_REPL_H_
#define MOCKINGJAY_REPL_H_

#include "repl_policies.h"
#include "cache_arrays.h"
#include "hash.h"
#include <cmath>

//Adapted from Ishan Shah's ChampSim implementation of Mockingjay
class MockingjayReplPolicy : public ReplPolicy {
private:
    // Cache parameters. Need to define due to ZSim flexibility.
    uint32_t numLines; //sets * ways
    uint32_t numSets; 
    uint32_t numWays; // lines per set
    uint32_t numCores;
    
    // Constants calculated from ZSim parameters
    uint32_t LOG2_BLOCK_SIZE;
    uint32_t LOG2_LLC_SET;
    uint32_t LOG2_LLC_SIZE;
    uint32_t LOG2_SAMPLED_SETS;
    
    //Reuse Distance constants 
    const uint32_t HISTORY = 8; //How far we look back in history
    const uint32_t GRANULARITY = 8; //Factor for ETR and reuse distance
    
    //Special constants
    uint32_t INF_RD; //Infinite reuse distance
    int32_t INF_ETR; //Infinite estimated time remaining
    uint32_t MAX_RD; //Max reused distance
    
    //Sampled cache parameters
    const uint32_t SAMPLED_CACHE_WAYS = 5;
    const uint32_t LOG2_SAMPLED_CACHE_SETS = 4;
    uint32_t SAMPLED_CACHE_TAG_BITS;
    uint32_t PC_SIGNATURE_BITS;
    const uint32_t TIMESTAMP_BITS = 8;
    
    const double TEMP_DIFFERENCE = 1.0/16.0; //How quickly predictions are updated
    double FLEXMIN_PENALTY; //Penalty for prefetched lines
    
    // ETR counters and clocks
    int8_t* etr; //per cache line
    uint8_t* etrClock; //for aging ETR values
    
    // Reuse Distance Predictor
    struct RDPEntry {
        uint32_t reuseDistance; //predicted reuse distance
    };
    
    // Simple table of reuse distance predictors
    RDPEntry* rdpTable;
    
    // Current timestamp for each set
    uint32_t* currentTimestamp;
    
    // Sampled set map
    bool* isSampledSetMap;
    
    // Sampled cache structures
    struct SampledCacheLine {
        bool valid; //is the entry in use
        uint64_t tag;
        uint64_t signature; //pc signature
        int timestamp; //when was the line last accessed
    };
    
    // Sampled cache storage
    SampledCacheLine* sampledCache;
    uint32_t sampledCacheSize;
    
    // Helper methods
    bool isSampledSet(uint32_t set) {
        return isSampledSetMap[set]; //checking if set is sampled
    }
    
    //Hash function for generating signatures. Uses CRC-based algorithm
    uint64_t crcHash(uint64_t blockAddress) {
        static const uint64_t crcPolynomial = 3988292384ULL;
        uint64_t returnVal = blockAddress;
        for (uint32_t i = 0; i < 3; i++) {
            returnVal = ((returnVal & 1) == 1) ? ((returnVal >> 1) ^ crcPolynomial) : (returnVal >> 1);
        }
        return returnVal;
    }
    
    //Generating a unique signature for cache access
    uint64_t getPCSignature(uint64_t pc, bool hit, bool prefetch, uint32_t coreId) {
        if (numCores == 1) {
            // Single-core implementation
            pc = pc << 1;
            if (hit) {
                pc = pc | 1; //Encoding hit info
            }
            pc = pc << 1;
            if (prefetch) {
                pc = pc | 1; //Encoding prefetch info
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
    
    //Calculating index in the sampled cache
    uint32_t getSampledCacheIndex(uint64_t full_addr){
        // Calculate the sampled cache set index
        uint64_t addr = full_addr >> LOG2_BLOCK_SIZE;
        addr = (addr << (64 - (LOG2_SAMPLED_CACHE_SETS + LOG2_LLC_SET))) >> (64 - (LOG2_SAMPLED_CACHE_SETS + LOG2_LLC_SET));
        return static_cast<uint32_t>(addr);
    }
    
    //Extracting tag for sampled cache line
    uint64_t getSampledCacheTag(uint64_t fullAddr) {
        return (fullAddr >> (LOG2_LLC_SET + LOG2_BLOCK_SIZE + LOG2_SAMPLED_CACHE_SETS)) & 
               ((1ULL << SAMPLED_CACHE_TAG_BITS) - 1);
    }
    
    //Searching for a line in the sampled cache
    int searchSampledCache(uint64_t blockTag, uint32_t sampledSetIdx) {
        uint32_t baseIdx = sampledSetIdx * SAMPLED_CACHE_WAYS;
        
        for (uint32_t way = 0; way < SAMPLED_CACHE_WAYS; way++) {
            if (sampledCache[baseIdx + way].valid && sampledCache[baseIdx + way].tag == blockTag) {
                return way;
            }
        }
        return -1; //If not found
    }
    
    //Increases RD for a line that wasn't reused
    void detrain(uint32_t sampledSetIdx, int way) {
        if (way < 0 || way >= (int)SAMPLED_CACHE_WAYS) { //invalid way
            return;
        }
        
        uint32_t idx = sampledSetIdx * SAMPLED_CACHE_WAYS + way; //convert set and way to an index
        if (!sampledCache[idx].valid) { //invalid, nothing to update
            return;
        }
        
        uint64_t signature = sampledCache[idx].signature; //get pc signature for the cache line
        
        // Update RDP for this signature with INF_RD if signature is valid
        if (signature < (1ULL << PC_SIGNATURE_BITS)) {
            rdpTable[signature].reuseDistance = MIN(rdpTable[signature].reuseDistance + 1, INF_RD); //increasing reused distance
        }
        
        sampledCache[idx].valid = false; //finished learning from an entry, free it up for a new one
    }
    
    //Adjust RD smoothly
    int temporalDifference(int init /*initial RD*/, int sample /*sampled RD*/) {
        if (sample > init) {
            int diff = sample - init;
            int weight = std::min(1, diff / 16); // limit to +1
            return std::min(init + weight, (int)INF_RD);
        } else if (sample < init) {
            int diff = init - sample;
            int weight = std::min(1, diff / 16); // limit to -1
            return std::max(init - weight, 0);
        } else {
            return init;
        }
    }
    
    //increment the timestamp
    uint32_t incrementTimestamp(uint32_t input) {
        input++;
        input = input % (1 << TIMESTAMP_BITS); //handles wraparounds
        return input;
    }
    
    //Calculate elapsed time between timestamps. Global=current, Local=last
    int timeElapsed(int global, int local) {
        if (global >= local) {
            return global - local;
        }
        //handle wraparound
        return global + (1 << TIMESTAMP_BITS) - local;
    }

public:
    //Constructor
    explicit MockingjayReplPolicy(uint32_t _numLines, uint32_t _numSets) 
        : numLines(_numLines), numSets(_numSets), numWays(_numLines/_numSets) {
        
        numCores = zinfo->numCores; //getting number of cores
        
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
        for (uint64_t i = 0; i < (1ULL << PC_SIGNATURE_BITS); i++) {
            rdpTable[i].reuseDistance = INF_RD; //untrained entries should start at INF_RD
        }
        
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
        
        //Details for log file
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
        //not used
    }
    
    //Main learning method called on each cache line access. 
    void update(uint32_t id, const MemReq* req) override {
        uint32_t set = id / numWays; //calculate the set of the line id
        uint32_t cpuId = req->srcId; //id of cpu that issued the request
        bool isPrefetch = (req->flags & MemReq::PREFETCH); //true if prefetch
        bool isHit = (req->type == GETS || req->type == GETX); //true if a hit
        
        // Handle writebacks specially
        if (req->type == PUTS || req->type == PUTX) {
            // Set immediate eviction priority for writebacks
            etr[id] = -INF_ETR;
            return;
        }
        
        // Generate PC signature by hashing PC, hit/prefetch flag, and core into a compact signature
        uint64_t pcSignature = getPCSignature(req->pcAddr, isHit, isPrefetch, cpuId);
        
        // Early bypass check. If predictor says PC has large RD
        if (rdpTable[pcSignature].reuseDistance > MAX_RD) {
            etr[id] = INF_ETR; // Set to max priority for eviction if line is forced in
            return;
        }
        // Update sampled cache if this is a sampled set
        if (isSampledSet(set)) {
            //compute index and tag for sampled cache
            uint32_t sampledCacheIndex = getSampledCacheIndex(req->lineAddr);
            uint64_t sampledCacheTag = getSampledCacheTag(req->lineAddr);

            //look for existing entry
            int sampledCacheWay = searchSampledCache(sampledCacheTag, sampledCacheIndex);
            
            //if found
            if (sampledCacheWay > -1) {
                // Hit in sampled cache
                uint32_t idx = sampledCacheIndex * SAMPLED_CACHE_WAYS + sampledCacheWay; //compute index
                uint64_t lastSignature = sampledCache[idx].signature; //extract signature of entry
                uint64_t lastTimestamp = sampledCache[idx].timestamp; //extract timestampt
                int sample = timeElapsed(currentTimestamp[set], lastTimestamp); //elapsed time since insertion
                
                //updating only if within the valid reuse window
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
                    
                    // Clear the entry so it won't be reused again
                    sampledCache[idx].valid = false;
                }
            }
            
            // Find space for new entry (a victim way in the sampled cache).
            int lruWay = -1;
            int lruRd = -1;
            for (uint32_t w = 0; w < SAMPLED_CACHE_WAYS; w++) {
                uint32_t idx = sampledCacheIndex * SAMPLED_CACHE_WAYS + w;
                if (!sampledCache[idx].valid) {
                    //prefer empty slots immediately
                    lruWay = w;
                    lruRd = INF_RD + 1;
                    break;
                }
                
                //calculate age for occupied slots
                uint64_t lastTimestamp = sampledCache[idx].timestamp;
                int sample = timeElapsed(currentTimestamp[set], lastTimestamp);
                if (sample > (int)INF_RD) { //old, need to detrain
                    lruWay = w;
                    lruRd = INF_RD + 1;
                    detrain(sampledCacheIndex, w);
                } else if (sample > lruRd) { //else choose entry with largest elapsed time
                    lruWay = w;
                    lruRd = sample;
                }
            }
            
            detrain(sampledCacheIndex, lruWay); //final detrain of chosen victim way, if any
            
            // Insert new sampled entry at that way
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
        
        // Age lines in the set at every GRANULARIty
        if (etrClock[set] == GRANULARITY) {
            for (uint32_t w = 0; w < numWays; w++) {
                uint32_t lineId = set * numWays + w;
                //only age non-scanning lines (ETR < INF_ETR, no abs)
                //and skip the currently accessed line
                if (lineId != id && etr[lineId] < INF_ETR) {
                    etr[lineId]--;
                }
            }
            etrClock[set] = 0;
        }
        etrClock[set]++; //increment per-set access counter
        
        //update ETR for the accessed line based on predictor's RD
        if (pcSignature >= (1ULL << PC_SIGNATURE_BITS) || rdpTable[pcSignature].reuseDistance == 0) {
            //for untrained or invalid signature: assume imminent reuse in single core, conservative in multicore
            etr[id] = (numCores == 1) ? 0 : INF_ETR;
        } else if (rdpTable[pcSignature].reuseDistance > MAX_RD) {
            //predicted far reuse: give lowest priority
            etr[id] = INF_ETR;
        } else {
            //candidate is within max RD — check if we should bypass it
            uint32_t predictedETR = rdpTable[pcSignature].reuseDistance / GRANULARITY;
            bool shouldBypass = true;

            for (uint32_t w = 0; w < numWays; w++) { //checking to see if the ETR is worse than the rest of the lines
                uint32_t lineId = set * numWays + w;
                if (etr[lineId] < (int32_t)predictedETR) {
                    shouldBypass = false;
                    break;
                }
            }
            //bypassing helps to avoid filling the cache with useless lines
            if (shouldBypass) {
                //don't promote — it has worse reuse than all existing lines
                etr[id] = INF_ETR;
                return;
            } else {
                //assign ETR based on predicted reuse distance
                etr[id] = predictedETR;
            }
        }
    }
    
    void replaced(uint32_t id) override {
        // Reset ETR when a line is replaced
        etr[id] = 0;
    }
    
    //determining which line to evict when a new line needs to be inserted
    //core eviction policy based on ETR
    template <typename C>
    uint32_t rank(const MemReq* req, C cands) {
        uint32_t bestCand = -1; //id of best eviction candidate
        int32_t maxEtr = -1; //max absolute ETR value seen
        
        for (auto ci = cands.begin(); ci != cands.end(); ci.inc()) {
            uint32_t candId = *ci;
            
            // If invalid, select it right away for eviction
            if (!cc->isValid(candId)) {
                bestCand = candId;
                break;
            }
            // Get absolute ETR value
            int32_t absEtr = abs(etr[candId]);
            
            //prioritize evicting highest absolute ETR first like in paper
            //lastly, negative ETR like in paper
            if (absEtr > maxEtr || 
                (absEtr == maxEtr && etr[candId] < 0))
                 {
                maxEtr = absEtr;
                bestCand = candId;
            }
        }
        return bestCand;
    }
    
    DECL_RANK_BINDINGS;
};

#endif // MOCKINGJAY_REPL_H_