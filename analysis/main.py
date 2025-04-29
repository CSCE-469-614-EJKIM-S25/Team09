import h5py
import numpy as np
from matplotlib import pyplot as plt

def computeCycles(path):
    f = h5py.File(path, "r")

    dset = f["stats"]["root"]
    stats = dset[-1]
    coreStats = stats["westmere"]

    # from 8 cores add across all the processors
    return np.sum(coreStats["cycles"] + coreStats["cCycles"])

def computeIPC(path):
    # Since IPC is defined as a ratio, add all cycles and intr. first
    f = h5py.File(path, "r")

    dset = f["stats"]["root"]
    stats = dset[-1]
    coreStats = stats["westmere"]

    cycles = np.sum(coreStats["cycles"] + coreStats["cCycles"])  # Cycles
    intructions = np.sum(coreStats["instrs"])

    return intructions / cycles

def computeMPKI(path):
    f = h5py.File(path, "r")

    dset = f["stats"]["root"]
    stats = dset[-1]

    coreStats = stats["westmere"]
    intructions = np.sum(coreStats["instrs"])

    l3Stats = stats["l3"]
    misses = np.sum(l3Stats["mGETS"] + l3Stats["mGETXIM"] + l3Stats["mGETXSM"])

    return (misses/intructions) * 1000

def writeMetricsToFile(metrics, benchmarks, output_file="metrics_output.tsv"):
    policies = metrics.keys()
    metric_types = ["cycles", "ipc", "mpki"]

    with open(output_file, 'w') as f:
        header = ["Benchmark"]
        for policy in policies:
            for m in metric_types:
                header.append(f"{policy}_{m}")
        f.write("\t".join(header) + "\n")

        for i, benchmark in enumerate(benchmarks):
            row = [benchmark]
            for policy in policies:
                for m in metric_types:
                    value = metrics[policy][m][i]
                    row.append(f"{value:.4f}" if isinstance(value, float) else str(value))
            f.write("\t".join(row) + "\n")

    print(f"Metrics written to {output_file}")

def main():
    suite = ["SPEC", "PARSEC"]
    spec = ["bzip2", "gcc", "mcf", "hmmer", "xalan", "cactusADM", 
            "leslie3d", "namd", "calculix", "sjeng", "libquantum", "soplex", "lbm"]
    parsec = ["blackscholes", "bodytrack", "fluidanimate", "streamcluster", "swaptions", "canneal", "x264"]
    replc = ["LRU", "LFU", "SRRIP", "RT-RRIP", "Mockingjay", ""]
    benchmarks = spec + parsec

    metrics = {
        "LRU": {"cycles": [], "ipc": [], "mpki": []},
        "LFU": {"cycles": [], "ipc": [], "mpki": []},
        "SRRIP": {"cycles": [], "ipc": [], "mpki": []},
        "ST-RRIP": {"cycles": [], "ipc": [], "mpki": []},
        "WeiSub": {"cycles": [], "ipc": [], "mpki": []}
    }

    for benchmark in benchmarks:
        suffix = "" if benchmark in spec else "_8c_simlarge"
        for policy in replc:
            path = f"simulation/zsim/outputs/hw4/{policy}/{benchmark}{suffix}/zsim-ev.h5"
            metrics[policy]["cycles"].append(computeCycles(path))
            metrics[policy]["ipc"].append(computeIPC(path))
            metrics[policy]["mpki"].append(computeMPKI(path))

    return 0


if __name__ == "__main__": main()