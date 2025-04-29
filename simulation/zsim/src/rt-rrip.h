#ifndef RT_RRIP_REPL_H_
#define RT_RRIP_REPL_H_
#include "repl_policies.h"

class RT_RRIPReplPolicy : public ReplPolicy {
    protected: 
        // rrip variables
        uint32_t* rrpvArray; // has rrpv values for each entry 
        uint32_t numLines; // number of blocks
        uint32_t rrpvMax;  // = 2^M - 1 : M bit configuration
        bool* isNewBlock; // new block flag

        // recency time variables
        uint32_t* recencyTimeArray; // recency time of each block
        uint32_t recencyTime; // recency time of the block being accessed
        uint32_t threshold; // threshold for recency time

        public: 

            // add member methods here, refer to repl_policies.h
            explicit RT_RRIPReplPolicy(uint32_t _numLines, uint32_t _rrpvMax) : numLines(_numLines), rrpvMax(_rrpvMax) {
                rrpvArray = gm_calloc<uint32_t>(numLines);
                recencyTimeArray = gm_calloc<uint32_t>(numLines);
                isNewBlock = gm_calloc<bool>(numLines);
                recencyTime = 0; // recency time of the block being accessed
                threshold = 0; // threshold for recency time

                for (uint32_t i = 0; i < numLines; ++i) {
                    rrpvArray[i] = rrpvMax-1;
                    recencyTimeArray[i] = 0;
                    isNewBlock[i] = false; // new blocks
                }
            }
            
            ~RT_RRIPReplPolicy() {
                gm_free(rrpvArray);
                gm_free(recencyTimeArray);
                gm_free(isNewBlock);
            }

            void update(uint32_t id, const MemReq* req) {
                // recency time values
                recencyTime++;
                recencyTimeArray[id] = recencyTime;

                // rrip values
                if (isNewBlock[id]) {
                    isNewBlock[id] = false; // reset new block flag
                } else {
                    rrpvArray[id] = 0; // cache hit rrpv=0   
                }
            }

            void replaced(uint32_t id) {
                isNewBlock[id] = true; // reset new block flag
                rrpvArray[id] = rrpvMax-1; // 2^M -2 -Prevent Blocks with distant future re-reference
                recencyTimeArray[id] = recencyTime;
            }

            template <typename C> inline uint32_t rank(const MemReq* req, C cands) {
                // recency time filter
                vector<uint32_t> filteredCands;
                threshold = getThreshold(cands); // returns average recency time
                for (auto ci = cands.begin(); ci != cands.end(); ci.inc()) {
                    if (recencyTimeArray[*ci] <= threshold) {
                        filteredCands.push_back(*ci); // gets all candidate with recency time over the average
                    }
                }

                if (filteredCands.empty()) {
                    for(auto ci = cands.begin(); ci != cands.end(); ci.inc()) {
                        filteredCands.push_back(*ci);
                    }
                }

                // RRIP
                while(true) {
                    for (auto fi: filteredCands) {
                        if (rrpvArray[fi] >= rrpvMax) {
                            return fi; // return first candidate that exceeds rrpvMax
                        }
                    }
                    // age filtered blocks if no candidate exceeds rrpvMax
                    for (auto fi : filteredCands) {
                        rrpvArray[fi]++;
                    }
                }
            }
            DECL_RANK_BINDINGS;

        private:
            // gets average of all recenency times
            template <typename C> inline uint32_t getThreshold(C cands) {
                uint32_t count = 0;
                uint32_t totalRecencyTime = 0;
                for (auto ci = cands.begin(); ci != cands.end(); ci.inc()) {
                    count++;
                    totalRecencyTime += recencyTimeArray[*ci];
                }
                if (count == 0) return 0; // avoid division by zero
                return totalRecencyTime / count; // average recency time
            }

};
#endif // RT_RRIP_REPL_H_
