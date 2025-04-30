#ifndef MOCKINGJAY_REPL_H_
#define MOCKINGJAY_REPL_H_

#include "repl_policies.h"
#include <unordered_map>
#include <stdlib.h>
using namespace std;

class MockingjayReplPolicy : public ReplPolicy {
    private:
        // Cache parameters. Need to define due to ZSim flexibility.
        uint32_t numLines; //sets * ways
        uint32_t numSets; 
        uint32_t numWays; // lines per set
        uint32_t numCores;
        
        // Constants calculated from ZSim parameters
        const uint32_t LOG2_BLOCK_SIZE; // aka log2 of line size
        const uint32_t LOG2_LLC_SET;
        const uint32_t LOG2_LLC_SIZE;
        const uint32_t LOG2_SAMPLED_SETS;
        
        //Reuse Distance constants 
        static constexpr int HISTORY = 8; //How far we look back in history
        static constexpr int GRANULARITY = 8; //Factor for ETR and reuse distance
        
        //Special constants
        int INF_ETR; // Infinite estimated time remaining
        int INF_RD; // reuse distance for perceived scans
        int MAX_RD; // reuse distance threshold for non-scan values
        
        //Sampled cache parameters
        static constexpr int SAMPLED_CACHE_WAYS = 5;
        static constexpr int LOG2_SAMPLED_CACHE_SETS = 4;
        const uint32_t SAMPLED_CACHE_TAG_BITS;
        const uint32_t PC_SIGNATURE_BITS;
        static constexpr int TIMESTAMP_BITS = 8;
        
        static constexpr double TEMP_DIFFERENCE = 1.0/16.0; //How quickly predictions are updated
        double FLEXMIN_PENALTY; // Penalty for prefetched lines
    
        // ETR counters and clocks
        int* etr; //etr counters for all cache lines
        int* etrClock; //counter for amount of accesses to that set
    
        // Reuse Distance Predictor
        unordered_map<uint32_t, int> rdp; // reuse distance predictor -- PC signature as index and stores predicted reuse distance of blocks mapping to signature
    
        int* currentTimestamp; // one timestamp per set in the LLC (numLines / llc_ways)
    
        // Sampled cache structures
        struct SampledCacheLine {
            bool valid; //is the entry in use
            uint64_t tag;
            uint64_t signature; //pc signature
            int timestamp; //when was the line last accessed
        };
    
        // Sampled cache storage
        unordered_map<uint32_t, SampledCacheLine*> sampledCache;
    
        // Helper methods
        bool isSampledSet(uint32_t set) {
            // Checking if set is sampled
            uint32_t maskLength = LOG2_LLC_SET - LOG2_SAMPLED_SETS;
            uint32_t mask = (1 << maskLength) - 1;
            return ((set & mask) == ((set >> (LOG2_LLC_SET - maskLength)) & mask));
        }
        
        // Hash function for generating signatures. Uses CRC-based algorithm
        uint64_t crcHash(uint64_t blockAddress) {
            static const uint64_t crcPolynomial = 3988292384ULL;
            uint64_t returnVal = blockAddress;
            for (uint32_t i = 0; i < 3; i++) {
                returnVal = ((returnVal & 1) == 1) ? ((returnVal >> 1) ^ crcPolynomial) : (returnVal >> 1);
            }
            return returnVal;
        }
        
        // Generating a unique signature for cache access
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
        
        // Calculating index in the sampled cache
        uint32_t getSampledCacheIndex(uint64_t full_addr){
            // Calculate the sampled cache set index
            uint64_t addr = full_addr >> LOG2_BLOCK_SIZE;
            addr = (addr << (64 - (LOG2_SAMPLED_CACHE_SETS + LOG2_LLC_SET))) >> (64 - (LOG2_SAMPLED_CACHE_SETS + LOG2_LLC_SET));
            return static_cast<uint32_t>(addr);
        }
        
        // Extracting tag for sampled cache line
        uint64_t getSampledCacheTag(uint64_t fullAddr) {
            return (fullAddr >> (LOG2_LLC_SET + LOG2_BLOCK_SIZE + LOG2_SAMPLED_CACHE_SETS)) & 
                   ((1ULL << SAMPLED_CACHE_TAG_BITS) - 1);
        }
        
        // Searching for a line in the sampled cache
        int searchSampledCache(uint64_t blockTag, uint32_t sampledSetIdx) {
            uint32_t baseIdx = sampledSetIdx * SAMPLED_CACHE_WAYS;
            
            for (uint32_t way = 0; way < SAMPLED_CACHE_WAYS; way++) {
                if (sampledCache[baseIdx + way]->valid && sampledCache[baseIdx + way]->tag == blockTag) {
                    return way;
                }
            }
            return -1; //If not found
        }
        
        // Increases RD for a line that wasn't reused
        void detrain(uint32_t sampledSetIdx, int way) {
            if (way < 0 || way >= (int)SAMPLED_CACHE_WAYS) { //invalid way
                return;
            }
            
            uint32_t idx = sampledSetIdx * SAMPLED_CACHE_WAYS + way; //convert set and way to an index
            if (!sampledCache[idx]->valid) { //invalid, nothing to update
                return;
            }
            
            uint64_t signature = sampledCache[idx]->signature; //get pc signature for the cache line
            
            // Update RDP for this signature with INF_RD if signature is valid
            if (signature < (1ULL << PC_SIGNATURE_BITS)) {
                rdp[signature] = MIN(rdp[signature] + 1, INF_RD); //increasing reused distance
            }
            
            sampledCache[idx]->valid = false; //finished learning from an entry, free it up for a new one
        }
        
        // Adjust RD smoothly
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
        
        // increment the timestamp
        uint32_t incrementTimestamp(uint32_t input) {
            input++;
            input = input % (1 << TIMESTAMP_BITS); //handles wraparounds
            return input;
        }
        
        // Calculate elapsed time between timestamps. Global=current, Local=last
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
            : numLines(_numLines), 
              numSets(_numSets), 
              numWays(_numLines/_numSets),
              LOG2_BLOCK_SIZE(ilog2(zinfo->lineSize)),
              LOG2_LLC_SET(ilog2(_numSets)),
              LOG2_LLC_SIZE(LOG2_LLC_SET + ilog2(_numLines/_numSets) + ilog2(zinfo->lineSize)),
              LOG2_SAMPLED_SETS(LOG2_LLC_SIZE - 16),
              SAMPLED_CACHE_TAG_BITS(31 - LOG2_LLC_SIZE),
              PC_SIGNATURE_BITS(LOG2_LLC_SIZE - 10) 
        {
            numCores = zinfo->numCores; //getting number of cores
            
            INF_RD = numWays * HISTORY - 1;
            INF_ETR = (numWays * HISTORY / GRANULARITY) - 1;
            MAX_RD = INF_RD - 22;
            
            // Calculate FLEXMIN_PENALTY based on core count
            FLEXMIN_PENALTY = 2.0 - log2(numCores)/4.0;
            
            // Initialize ETR counters and clocks
            etr = gm_calloc<int>(numLines);
            etrClock = gm_calloc<int>(numSets);
            
            // Initialize timestamps
            currentTimestamp = gm_calloc<int>(numSets);
            
            // Count sampled sets
            uint32_t numSampledSets = 0;
            for (uint32_t set = 0; set < numSets; set++) {
                if (isSampledSet(set)) {
                    numSampledSets++;
                }
            }
            
            // Initialize sampled cache for sampled sets
            for (uint32_t set = 0; set < numSets; set++) {
                if (isSampledSet(set)) {
                    sampledCache[set] = new SampledCacheLine[SAMPLED_CACHE_WAYS]();
                    
                    // Initialize the lines
                    for (int w = 0; w < SAMPLED_CACHE_WAYS; w++) {
                        sampledCache[set][w].valid = false;
                    }
                }
            }
            
            //Details for log file
            info("Mockingjay initialized: numCores=%d, LOG2_LLC_SIZE=%d, PC_SIGNATURE_BITS=%d, INF_RD=%d, MAX_RD=%d, FLEXMIN_PENALTY=%.2f",
                 numCores, LOG2_LLC_SIZE, PC_SIGNATURE_BITS, INF_RD, MAX_RD, FLEXMIN_PENALTY);
        }
        
        // Destructor
        ~MockingjayReplPolicy() {
            gm_free(etr);
            gm_free(etrClock);
            gm_free(currentTimestamp);
            
            // Free sampled cache memory
            for (auto& entry : sampledCache) {
                delete[] entry.second;
            }
        }
        
        // Stats initialization (not used)
        void initStats(AggregateStat* parent) override {
            //not used
        }

    void update(uint32_t id, const MemReq* req) override {
        // Handle writebacks specially
        if (req->type == PUTS || req->type == PUTX) {
            // Set immediate eviction priority for writebacks
            etr[id] = -INF_ETR;
            return;
        }
    
        uint32_t set = id / numWays;//calculate the set of the line id
        uint32_t cpuId = req->srcId;//id of cpu that issued the request
        bool isPrefetch = (req->flags & MemReq::PREFETCH);//true if prefetch
        bool isHit = (req->type == GETS || req->type == GETX); //true if a hit
        
        // Generate PC signature by hashing PC, hit/prefetch flag, and core into a compact signature
        uint64_t pcSignature = getPCSignature(req->pcAddr, isHit, isPrefetch, cpuId);
    
        //modify isSampledSet to be more explicit
        bool sampledSet = false;
        {
            uint32_t maskLength = LOG2_LLC_SET - LOG2_SAMPLED_SETS;
            uint32_t mask = (1 << maskLength) - 1;
            sampledSet = ((set & mask) == ((set >> (LOG2_LLC_SET - maskLength)) & mask));
        }
    
        if (sampledSet) {
            //checking if the set exists in sampledCache
            uint32_t sampledCacheIndex = set;//using set index
            
            //checking set exists
            if (sampledCache.find(sampledCacheIndex) == sampledCache.end()) {
                sampledCache[sampledCacheIndex] = new SampledCacheLine[SAMPLED_CACHE_WAYS]();
                
                //initialize to invalid
                for (int w = 0; w < SAMPLED_CACHE_WAYS; w++) {
                    sampledCache[sampledCacheIndex][w].valid = false;
                }
            }
    
            uint64_t sampledCacheTag = getSampledCacheTag(req->lineAddr);
            int sampledCacheWay = -1;
    
            //safe search for existing entry
            for (int w = 0; w < SAMPLED_CACHE_WAYS; w++) {
                if (sampledCache[sampledCacheIndex][w].valid && 
                    sampledCache[sampledCacheIndex][w].tag == sampledCacheTag) {
                    sampledCacheWay = w;
                    break;
                }
            }
            //if found
            if (sampledCacheWay > -1) {
                uint64_t lastSignature = sampledCache[sampledCacheIndex][sampledCacheWay].signature;//extract signature of entry
                uint64_t lastTimestamp = sampledCache[sampledCacheIndex][sampledCacheWay].timestamp;//extract timestamp
                int sample = timeElapsed(currentTimestamp[set], lastTimestamp);//elapsed time since insertion
                
                //updating only if within the valid reuse window
                if (sample <= INF_RD) {
                    // Apply FLEXMIN penalty for prefetches
                    if (isPrefetch) sample = sample * FLEXMIN_PENALTY;
                    
                    // Update RDP with observed reuse distance
                    if (rdp.count(lastSignature)) {
                        int init = rdp[lastSignature];
                        rdp[lastSignature] = temporalDifference(init, sample);
                    } else {
                        rdp[lastSignature] = sample;
                    }
                    // Clear the entry so it won't be reused again
                    sampledCache[sampledCacheIndex][sampledCacheWay].valid = false;
                }
            }
            // Find space for new entry (a victim way in the sampled cache).
            int lruWay = -1;
            int lruRd = -1;
            for (int w = 0; w < SAMPLED_CACHE_WAYS; w++) {
                if (!sampledCache[sampledCacheIndex][w].valid) {
                    //prefer empty slots immediately
                    lruWay = w;
                    lruRd = INF_RD + 1;
                    break;
                }
    
                uint64_t lastTimestamp = sampledCache[sampledCacheIndex][w].timestamp;
                int sample = timeElapsed(currentTimestamp[set], lastTimestamp);
                if (sample > INF_RD) {
                    lruWay = w;
                    lruRd = INF_RD + 1;
                    
                    // Detrain this entry
                    if (sampledCache[sampledCacheIndex][w].valid) {
                        //calculate age for occupied slots
                        uint64_t lastSignature = sampledCache[sampledCacheIndex][w].signature;
                        if (rdp.count(lastSignature)) {
                            rdp[lastSignature] = min(rdp[lastSignature] + 1, INF_RD);
                        } else {
                            rdp[lastSignature] = INF_RD;
                        }
                        sampledCache[sampledCacheIndex][w].valid = false;
                    }
                } else if (sample > lruRd) {
                    lruWay = w;
                    lruRd = sample;
                }
            }
    
             // Insert new sampled entry at that way
            if (lruWay >= 0) {
                sampledCache[sampledCacheIndex][lruWay].valid = true;
                sampledCache[sampledCacheIndex][lruWay].signature = pcSignature;
                sampledCache[sampledCacheIndex][lruWay].tag = sampledCacheTag;
                sampledCache[sampledCacheIndex][lruWay].timestamp = currentTimestamp[set];
            }
    
            // Increment timestamp
            currentTimestamp[set] = incrementTimestamp(currentTimestamp[set]);
        
        }

        // Age lines in the set at every GRANULARIty
        if (etrClock[set] == GRANULARITY) {
            uint32_t start = (id/numWays)*numWays;
            for (uint32_t i = start; i < (start + numWays); i++) {
                //only age non-scanning lines (ETR < INF_ETR, no abs)
                //and skip the currently accessed line
                if ((uint32_t) i != id && abs(etr[i]) < INF_ETR) {
                    etr[i]--;
                }
            }
            etrClock[set] = 0;
        } else {
            etrClock[set]++;//increment per-set access counter
        }

        if (id < numWays) {
            if (!rdp.count(pcSignature)) {
                etr[id] = (numCores == 1) ? 0 : INF_ETR;
            } else {
                if (rdp[pcSignature] > MAX_RD) {
                    etr[id] = INF_ETR;
                } else {
                    etr[id] = rdp[pcSignature] / GRANULARITY;
                }
            }
        }
    }

    void replaced(uint32_t id) override {
        // Reset ETR when a line is replaced
        etr[id] = 0;
    }

    //determining which cache line to evict
    template <typename C>
    uint32_t rank(const MemReq* req, C cands) {
        //loop through candidates
        for (auto ci = cands.begin(); ci != cands.end(); ci.inc()) {
            if (!cc->isValid(*ci)) return *ci; //evict invalid
        }
        //variables to track the cache line with highest ETR
        int maxEtr = -1;
        int victimLoc = 0;
        for (auto ci = cands.begin(); ci != cands.end(); ci.inc()) {
            if (abs(etr[*ci]) > maxEtr || 
                (abs(etr[*ci]) == maxEtr && etr[*ci] < 0)) {
                victimLoc = *ci;//update victim line
                maxEtr = abs(etr[*ci]);//update max etr
            }
        }
        
        return victimLoc; //return victim line
    }

    DECL_RANK_BINDINGS;
};

#endif // MOCKINGJAY_REPL_H_