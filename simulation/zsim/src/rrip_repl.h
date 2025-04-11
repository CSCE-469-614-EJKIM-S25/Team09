#ifndef RRIP_REPL_H_
#define RRIP_REPL_H_

#include "repl_policies.h"

/* Static RRIP
    Victim: empty or block with maximum RRPV else age all and repeat.
    Replacement: Long Re-Reference Interval
    Hit Promotion Policy: Hit Priority
*/
class SRRIPReplPolicy : public ReplPolicy {
    protected:
        uint32_t* array;    
        uint32_t numLines; // number of blocks
        uint32_t rrpvMax;  // = 2^M - 1 : M bit configuration
        uint32_t rrpv;     // Re-Reference Prediction Value 

    public:
        // add member methods here, refer to repl_policies.h
        explicit SRRIPReplPolicy(uint32_t _numLines, uint32_t _rrpvMax): numLines(_numLines), rrpvMax(_rrpvMax) {
            array = gm_calloc<uint32_t>(numLines);
            for (uint32_t i = 0; i < numLines; ++i) {
                array[i] = rrpvMax+1; // new blocks
            }
        }

        ~SRRIPReplPolicy()  {
            gm_free(array);
        }

        void update(uint32_t id, const MemReq* req) {
            if (array[id] == rrpvMax+1) { // New block
                array[id] = rrpvMax-1; // 2^M -2 -Prevent Blocks with distant future re-reference
            }
            array[id] = 0; // Hit Priority -Promotion Policy   
        }

        void replaced(uint32_t id) {
            array[id] = rrpvMax-1; // 2^M -2 -Prevent Blocks with distant future re-reference
        }

        template <typename C> inline uint32_t rank(const MemReq* req, C cands) {
            uint32_t bestCand;
            while (1) {
                for (auto ci = cands.begin(); ci != cands.end(); ci.inc()) {
                    if (array[*ci] >= rrpvMax) {
                        bestCand = *ci;
                        return bestCand;
                    }
                }
                ageAll(cands); // age all blocks and repeat
            } 
        }

        DECL_RANK_BINDINGS;

    private:
        template <typename C> inline void ageAll(C cands)   {
            for (auto ci = cands.begin(); ci != cands.end(); ci.inc()) {
                ++array[*ci];
            }
        }
};
#endif // RRIP_REPL_H_
