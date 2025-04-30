import h5py
import numpy as np
import pandas as pd
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

def computeSpeedUp(replc1, replc2):
    return replc1 / replc2

def computeAvgSpeedUpMean(metrics):
    avg_speedup = {}

    for policy in metrics:
        raw_speedups = metrics[policy].get("speedUpOverLRU", [])

        # Convert to percentage and filter out NaNs
        clean_speedups = [
            s * 100 for s in raw_speedups if s is not None and not np.isnan(s)
        ]

        if clean_speedups:
            avg_speedup[policy] = np.mean(clean_speedups)
        else:
            avg_speedup[policy] = float('nan')

    return avg_speedup

def plotNumCycles(metrics, benchmarks, colors, width, plotName, legend=True):
    plt.clf()
    plt.cla()
    bar_width = width
    index = np.arange(len(benchmarks))
    labels = list(metrics.keys())  # replc policies data

    # Plot each policy
    for i, label in enumerate(labels):
        numCycles = metrics[label]['cycles']  # grab the cycles list
        plt.bar(index + i*bar_width, numCycles, bar_width, color=colors[label], label=label)

    plt.xlabel("Benchmarks")
    plt.ylabel("Number of Cycles (1e8)")
    plt.xticks(index + bar_width / 2, benchmarks, rotation=75)
    plt.tight_layout()
    if legend: plt.legend()
    plt.savefig(plotName, dpi=500, bbox_inches="tight")

def plotIPC(metrics, benchmarks, colors, width, plotName, legend=True):
    plt.clf()
    plt.cla()
    bar_width = width
    index = np.arange(len(benchmarks))
    labels = list(metrics.keys())

    for i, label in enumerate(labels):
        ipc_values = metrics[label]['ipc']
        plt.bar(index + i * bar_width, ipc_values, bar_width, color=colors[label], label=label)

    plt.xlabel("Benchmarks")
    plt.ylabel("Instructions Per Cycle")
    plt.xticks(index + (bar_width * (len(labels) - 1) / 2), benchmarks, rotation=75)  # Center labels
    plt.tight_layout()
    if legend: plt.legend()
    plt.savefig(plotName, dpi=500, bbox_inches="tight")

def plotMPKI(metrics, benchmarks, colors, width, plotName, legend=True):
    plt.clf()
    plt.cla()
    bar_width = width
    index = np.arange(len(benchmarks))
    labels = list(metrics.keys())

    for i, label in enumerate(labels):
        mpki = metrics[label]['mpki']
        plt.bar(index + i * bar_width, mpki, bar_width, color=colors[label], label=label)

    plt.xlabel("Benchmarks")
    plt.ylabel("Misses Per 1K Instructions")
    plt.xticks(index + bar_width, benchmarks, rotation=75)
    plt.tight_layout()
    if legend: plt.legend()
    plt.savefig(plotName, dpi=500, bbox_inches="tight")
    plt.show()

def plotSpeedUpOverLRU(metrics, benchmarks, colors, width, plotName, legend=True):
    plt.clf()
    plt.cla()

    bar_width = width
    index = np.arange(len(benchmarks))  # Make sure this is a 1D array
    selected_policies = ["Mockingjay", "RT-RRIP", "SRRIP"]

    for i, policy in enumerate(selected_policies):
        if "speedUpOverLRU" not in metrics[policy]:
            print(f"Warning: 'speedUpOverLRU' not found for {policy}")
            continue

        speedups = metrics[policy]["speedUpOverLRU"]

        # Ensure the data aligns with benchmarks
        if len(speedups) != len(benchmarks):
            print(f"Length mismatch for {policy}: {len(speedups)} values, {len(benchmarks)} benchmarks")
            continue

        plt.bar(index + i * bar_width,
                speedups,
                bar_width,
                color=colors[policy],
                label=policy)

    plt.xlabel("Benchmarks")
    plt.ylabel("Speedup Over LRU")

    # Properly center the benchmark labels under the grouped bars
    xtick_positions = index + bar_width * (len(selected_policies) - 1) / 2
    plt.xticks(xtick_positions, benchmarks, rotation=75)

    plt.tight_layout()
    if legend:
        plt.legend()
    plt.savefig(plotName, dpi=500, bbox_inches="tight")
    plt.show()

def main():
    suite = ["SPEC", "PARSEC"]
    spec = ["bzip2", "gcc", "mcf", "hmmer", "xalan", "cactusADM",
            "namd", "calculix", "sjeng", "libquantum", "soplex", "lbm"]
    parsec = ["blackscholes", "bodytrack", "fluidanimate", "streamcluster",
            "swaptions", "canneal", "x264"]
    replc = ["LFU", "LRU", "Mockingjay", "NRU", "Rand", "RT-RRIP", "SRRIP",
            "TreeLRU", "Vantage"]
    replc_colors = {
        "LFU": "#1f77b4",       # blue
        "LRU": "#ff7f0e",       # orange
        "Mockingjay": "#17becf",# teal
        "NRU": "#2ca02c",       # green
        "Rand": "#d62728",      # red
        "RT-RRIP": "#9467bd",   # purple
        "SRRIP": "#bcbd22",     # yellowish green
        "TreeLRU": "#8c564b",   # brown
        "Vantage": "#e377c2"    # pink
    }
    benchmarks = spec + parsec

    metrics = {
        "LRU": {"cycles": [], "ipc": [], "mpki": [], "speedUpOverLRU" : []},
        "LFU": {"cycles": [], "ipc": [], "mpki": [], "speedUpOverLRU" : []},
        "Mockingjay": {"cycles": [], "ipc": [], "mpki": [], "speedUpOverLRU" : []},
        "NRU": {"cycles": [], "ipc": [], "mpki": [], "speedUpOverLRU" : []},
        "Rand": {"cycles": [], "ipc": [], "mpki": [], "speedUpOverLRU" : []},
        "RT-RRIP": {"cycles": [], "ipc": [], "mpki": [], "speedUpOverLRU" : []},
        "SRRIP": {"cycles": [], "ipc": [], "mpki": [], "speedUpOverLRU" : []},
        "TreeLRU": {"cycles": [], "ipc": [], "mpki": [], "speedUpOverLRU" : []},
        "Vantage": {"cycles": [], "ipc": [], "mpki": [], "speedUpOverLRU" : []}
    }

    for benchmark in benchmarks:
        suffix = "" if benchmark in spec else "_8c_simlarge"
        for policy in replc:
            pwd = "../simulation/zsim/outputs/hw4/" + policy + "/" + benchmark + suffix + "/zsim-ev.h5"
            metrics[policy]["cycles"].append(computeCycles(pwd))
            metrics[policy]["ipc"].append(computeIPC(pwd))
            metrics[policy]["mpki"].append(computeMPKI(pwd))

    for policy in replc:
        metrics[policy]["speedUpOverLRU"] = []
        for i in range(len(benchmarks)):
            lru_cycles = metrics["LRU"]["cycles"][i]
            policy_cycles = metrics[policy]["cycles"][i]

            if policy == "LRU":
                speedup = 1.0
            else:
                speedup = lru_cycles / policy_cycles if policy_cycles != 0 else float('nan')

            metrics[policy]["speedUpOverLRU"].append(speedup)

    avg_speedup_percent = computeAvgSpeedUpMean(metrics)  # already returns values as % now
    avg_speedup_df = pd.DataFrame([avg_speedup_percent], index=["Average Speedup (%)"])
    avg_speedup_table = avg_speedup_df.to_latex(float_format="%.1f",
                                                caption="Average Speedup Over LRU (in Percentage)",
                                                label="tab:avgSpeedup")

    print(avg_speedup_table)
    with open("avgSpeedup_table.tex", "w") as f:
        f.write(avg_speedup_table)

    numOfCycles_data = {policy: metrics[policy]['cycles'] for policy in replc}
    numOfCycles_df = pd.DataFrame(numOfCycles_data, index=benchmarks)
    numOfCycles_table = numOfCycles_df.to_latex(index=True, float_format="%.2f",
                            caption="Number of Cycles", label="tab:numOfCycles")

    print(numOfCycles_table)
    with open("numOfCycles_table.tex", "w") as f:
        f.write(numOfCycles_table)

    ipc_data = {policy: metrics[policy]['ipc'] for policy in replc}
    ipc_df = pd.DataFrame(ipc_data, index=benchmarks)
    ipc_table = ipc_df.to_latex(index=True, float_format="%.2f",
                            caption="IPC", label="tab:ipc")

    print(ipc_table)
    with open("ipc_table.tex", "w") as f:
        f.write(ipc_table)

    mpki_data = {policy: metrics[policy]['mpki'] for policy in replc}
    mpki_df = pd.DataFrame(mpki_data, index=benchmarks)
    mpki_table = mpki_df.to_latex(index=True, float_format="%.2f",
                            caption="MPKI", label="tab:mpki")

    print(mpki_table)
    with open("mpki_table.tex", "w") as f:
        f.write(mpki_table)

    plotNumCycles(metrics, benchmarks, replc_colors, 0.1, "numOfCycles.png")
    plotIPC(metrics, benchmarks, replc_colors, 0.1, "IPC.png", False) # omit legend to view chart
    plotMPKI(metrics, benchmarks, replc_colors, 0.1, "MPKI.png")

    # recall benchmarks = spec + parsec
    specMetrics = {}
    for policy in metrics:
      specMetrics[policy] = {
        'cycles': metrics[policy]['cycles'][0:len(spec)],
        'ipc'   : metrics[policy]['ipc'][0:len(spec)],
        'mpki'  : metrics[policy]['mpki'][0:len(spec)]
      }    

    plotNumCycles(specMetrics, spec, replc_colors, 0.095, "specNumOfCycles.png")
    plotIPC(specMetrics, spec, replc_colors, 0.095, "specIPC.png")
    plotMPKI(specMetrics, spec, replc_colors, 0.095, "specMPKI.png")

    # recall benchmarks = spec + parsec
    parsecMetrics = {}
    for policy in metrics:
        parsecMetrics[policy] = {
        'cycles': metrics[policy]['cycles'][len(spec):],
        'ipc'   : metrics[policy]['ipc'][len(spec):],
        'mpki'  : metrics[policy]['mpki'][len(spec):]
        }
  
    plotNumCycles(parsecMetrics, parsec, replc_colors, 0.095, "parsecNumOfCycles.png", False)
    plotIPC(parsecMetrics, parsec, replc_colors, 0.095, "parsecIPC.png", False)
    plotMPKI(parsecMetrics, parsec, replc_colors, 0.095, "parsecMPKI.png")
    plotSpeedUpOverLRU(metrics, benchmarks, replc_colors, 0.25, "SpeedUpOverLRU.png")

if __name__ == "__main__": main()