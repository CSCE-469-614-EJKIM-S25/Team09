# casim
Computer Architecture Simulation Infrastructure for CSCE 614 Computer Architecture Term Project: Team 09

##### 0. Clone into Github Repository

Using this link clone into the github repository in your workspace

```
https://github.com/CSCE-469-614-EJKIM-S25/Team09.git 
```

##### 1. WSL Set up
Navigate to Team09/simulations

```
cd Team09/simulation/
```

Use wsl-deps bash script to install dependencies and configure PIN API & benchmark scripts (they require x privileges)

```
$ bash wsl-deps.sh
```


##### 2. Unzip benchmarks files

```
zip -F benchmarks.zip --out single-benchmark.zip && unzip single-benchmark.zip && mkdir benchmarks/parsec-2.1/inputs/streamcluster
```

### 2. Environemnt setup

To set up the Python environment for the first time, run the following commands.

```
python3 -m venv venv
source venv/bin/activate
pip install scons
```

Everytime you want to build or run zsim, you need to setup the environment variables first.

```
source venv/bin/activate
source setup_env
```

##### 3. Compile zsim

```
cd zsim
scons -j4
```

You need to compile the code each time you make a change.


##### 4a. Run the entire project 

```
./run-all
```

##### 4b. Run all Benchmarks for a replacement policy by group memeber

group member: Zach, Rodrigo, Brycen

repl_policy: LFU, LRU, Mockingjay, NRU, Rand, RT-RRIP, SRRIP, TreeLRU, Vantage

```
./run-all-xpolicy <group memeber> <repl_policy>
```

##### 4c. Run Bencmark

suite: SPEC, PARSEC

SPEC Benchmarks: bzip2, cactusADM, calculix, gcc, hmmer, lbm, libquantum, mcf, namd, sjeng, soplex, xalan

PARSEC Benchmarks: blackscholes, bodytrack, canneal, fluidanimate, swaptions, x264

repl_policy: LFU, LRU, Mockingjay, NRU, Rand, RT-RRIP, SRRIP, TreeLRU, Vantage

```
./run-simulation <suite> <benchmark> <repl_policy>
```

###### For more information, check `zsim/README.md`
