# Term Project Team 09
##### * Zach Assad, Brycen Craig, Rodrigo Orozco
This team repository is used to implement, analyze, and compare the Mockingjay and RT-RRIP cache replacement policies. The results are compared against the following policies in zsim: LFU, LRU, NRU, Rand, TreeLRU, Vantage, and SRRIP (implemnted in Homework 4). 

##### Project Description
Caching plays a critical role in overall system performance and the performance of the cache is largely affected by the type of caching policy used. The LLC (Last-Level Cache) is essential since it is the final level of cache before memory. The ideal cache replacement policy is known as Belady's MIN Policy which always evicts the cache block that will be used farthest in the future. The two policies RT-RRIP and Mockingjay work to mimic Belady's MIN Policy through reuse distance prediction.

##### Project Goal
The goal of this project is to implement, analyze, and compare two modern cache replacement policies --Mockingjay and RT-RRIP-- against several other cache replacement policies in zsim. 


#### - Mockingjay (Reuse-Distance Prediction-Based, Re-reference Prediction-Based)
Predicts the reuse distance of cache lines using sampled sets and program counter (PC) signatures.
It dynamically assigns eviction priorities based on an Estimated Time Remaining (ETR) metric, aggressively evicting data predicted to have long or unlikely reuse.
This design improves cache efficiency for complex, irregular memory access patterns while keeping hardware overhead low.Mockingjay predicts the reuse distance of cache lines using sampled sets and program counter (PC) signatures.
It dynamically assigns eviction priorities based on an Estimated Time Remaining (ETR) metric, aggressively evicting data predicted to have long or unlikely reuse.
This design improves cache efficiency for complex, irregular memory access patterns while keeping hardware overhead low.


#### - RT-RRIP (Re-reference Prediction-Based)
Predicts re-reference intervals for cache lines using a combination of static RRPV counters and recency filtering.
It prioritizes evicting blocks that are both infrequently accessed (high RRPV) and older than the average access time.
This design allows RT-RRIP to adapt better than plain SRRIP, improving cache decisions for workloads with changing reuse patterns.


#### - SRRIP (Re-referece Prediction Based)
Assigns a static "re-reference prediction value" (RRPV) to each block and evicts those least likely to be reused soon. New blocks are typically inserted with a high RRPV to age out unless they quickly prove useful. It strikes a balance between simple implementation and predictive power across many workloads.


#### - LFU (Frequency Based)
Evicts the cache block with the fewest accesses over time. It assumes that blocks accessed rarely are less useful and can be safely replaced. However, it can suffer from cache pollution when old but once-frequently-used blocks linger unnecessarily.


#### - LRU (Recency Based)
LRU evicts the cache block that was accessed the longest time ago. It works well for workloads where recently used data is likely to be reused again soon. True LRU can be costly to implement exactly in hardware.


#### - NRU (Recency Based)
Approximates LRU by using a single "recently used" bit per cache line. Periodically, all bits are cleared, and blocks with cleared bits are evicted first. It reduces the overhead of maintaining full LRU order at the cost of eviction accuracy.


#### - Rand (Randomized)
Selects a random cache block to evict regardless of access history. It is extremely simple and low-overhead but can make poor eviction decisions, hurting performance for workloads with strong locality.


#### - TreeLRU (Recency Based)
Approximates LRU by using a binary tree to track recent accesses across cache blocks. Each internal node tracks which subtree was used more recently, allowing quick eviction decisions. It scales better than exact LRU for highly associative caches.


#### - Vantage (Partition Based)
Dynamically partitions the cache into logical regions, adjusting each partition’s size based on runtime access pressure using coarse-grained timestamps and feedback control.
Blocks are demoted from overcrowded partitions into an unmanaged shared region to rebalance cache usage.
Vantage improves cache efficiency under multi-application workloads without using profiling or machine learning.


To run the entire project simulation and analysis:


```
$ ./simulation/zsim/run-all
$ python /analysis/main.py
```

### Simulation - Data Collection
To view simulation intructions see:

```
simulation/README.md
```


The following files were modified to add support for PC propagation to LLC:

```
simulation/zsim/src/decoder.*
simulation/zsim/src/filter_cache.h
simulation/zsim/src/memory_hierachy.h
simulation/zsim/src/ooo_core.*
```


The following files were implemented/modified to implement Mockingjay:

```
simulation/zsim/cofigs/hw4/Mockingjay/
simulation/zsim/src/init.cpp
simulation/zsim/src/mockingjay_repl.h
```


The following files were implemented/modified to implement RT-RRIP:

```
simulation/zsim/configs/hw4/RT-RRIP
simulation/zsim/src/init.cpp
simulation/zsim/src/rt-rrip.h
```


The following config files were implemented for zsim built-in policies:

```
simulation/zsim/src/configs/hw4/NRU
simulation/zsim/src/configs/hw4/Rand
simulation/zsim/src/configs/hw4/TreeLRU
simulation/zsim/src/configs/hw4/Vantage
```


### Analysis - Data Plots and Tables
To view enviroment instructions and run analysis instructions see:

```
analysis/README.md
```


NOTE: The team utilized google drive to store simulation results and mounted a google collab notebook to run a program to analyze data and create charts/tables see:

```
analysis/analysis.ipynb
```
