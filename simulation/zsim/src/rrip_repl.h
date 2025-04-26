#ifndef RRIP_REPL_H_
#define RRIP_REPL_H_

#include "repl_policies.h"

// Static RRIP
class SRRIPReplPolicy : public ReplPolicy {
    protected:
        // add class member variables here
        uint32_t rpvMax; // max value of RRPV
        uint32_t* array; // array of RRPV values
        uint32_t numLines; //cache lines
        bool* isNew; //array keeping track of which blocks are newly inserted

    public:
        // add member methods here, refer to repl_policies.h
        explicit SRRIPReplPolicy(uint32_t _numLines, uint32_t _rpvMax) : rpvMax(_rpvMax), numLines(_numLines) {
            array = gm_calloc<uint32_t>(numLines);
            isNew = gm_calloc<bool>(numLines);
            for (uint32_t i = 0; i < numLines; i++) { //initialize
              array[i] = rpvMax - 1;
              isNew[i] = false;
            }
        }

        ~SRRIPReplPolicy() {
            gm_free(array);
            gm_free(isNew);
        }

        void update(uint32_t id, const MemReq* req) {
            if(isNew[id]){
                isNew[id] = false;
            }
            else{
            array[id] = 0; //hit, so set RRPV to 0
            }
        }

        void replaced(uint32_t id) {
            array[id] = rpvMax - 1; //new, so set RPPV to max RPPV - 1
            isNew[id] = true; //update new
        }

        template <typename C> inline uint32_t rank(const MemReq* req, C cands) {
            // keep incrementing until candidate found
            while(true){
              // first, search for an rpvMax
              for (auto ci = cands.begin(); ci != cands.end(); ci.inc()) {
                  if (array[*ci] == rpvMax) {
                      return *ci;
                  }
              }
              
              // if none found, increment RRPVs
              for (auto ci = cands.begin(); ci != cands.end(); ci.inc()) {
                  if (array[*ci] < rpvMax) {
                      array[*ci]++;
                  }
              }

            }
            return *(cands.begin()); //should never reach this
        }


        // DECL_RANK_BINDINGS;
        DECL_RANK_BINDINGS;
};
#endif // RRIP_REPL_H_
