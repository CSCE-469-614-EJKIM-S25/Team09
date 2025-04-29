# Term Project Team 09
##### * Zach Assad, Brycen Craig, Rodrigo Orozco 

Use to run the entire project simulation and analysis:

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
