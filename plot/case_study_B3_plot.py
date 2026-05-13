import pandas as pd, numpy as np
import matplotlib.pyplot as plt

df = pd.read_csv("results/case_study_B3_result.csv")

agg = (
    df.groupby("num_workers")
    .agg(
        runs=("seed", "count"),
        drop_mean=("victim_drop_rate", "mean"),
        drop_std=("victim_drop_rate", "std"),
        p95_mean=("victim_p95_latency", "mean"),
        p95_std=("victim_p95_latency", "std"),
    )
    .reset_index()
)

agg["drop_sem"] = agg["drop_std"] / np.sqrt(agg["runs"])
agg["p95_sem"] = agg["p95_std"] / np.sqrt(agg["runs"])
agg = agg.sort_values("num_workers")

plt.rcParams.update(
    {
        "axes.grid": True,
        "axes.axisbelow": True,
        "grid.linestyle": "--",
        "font.size": 11,
        "axes.titlesize": 12,
        "axes.labelsize": 11,
    }
)

plt.figure(figsize=(10, 3.5))

plt.subplot(1, 2, 1)
plt.errorbar(
    agg["num_workers"], agg["drop_mean"], marker="o", capsize=3, color="#f4a261"
)
plt.xlabel("Number of workers")
plt.ylabel("Victim drop rate")
plt.ylim(0, 0.07)
plt.title("(a) Victim drop rate vs number of workers")

plt.subplot(1, 2, 2)
plt.errorbar(
    agg["num_workers"], agg["p95_mean"], marker="o", capsize=3, color="#f4a261"
)
plt.xlabel("Number of workers")
plt.ylabel("Victim p95 latency (time units)")
plt.title("(b) Victim p95 latency vs number of workers")

plt.tight_layout()
plt.savefig("figs/case_study_B3_fig.pdf")
