
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

plt.rcParams.update({
    "axes.grid": True,
    "axes.axisbelow": True,
    "grid.linestyle": "--",
    "font.size": 11,
    "axes.titlesize": 12,
    "axes.labelsize": 11,
})

df = pd.read_csv(f"results/case_study_B1_result.csv")
df["co_loc_prob"] = np.where(
        df["victim_arrivals"] > 0,
        df["coloc_count"] / df["victim_arrivals"],
        np.nan,
    )

group_cols = ["scheduler", "attack_intensity"]
metrics = ["victim_drop_rate", "victim_p95_latency", "attacker_drop_rate"]

agg = df.groupby(group_cols).agg(
    runs=("seed", "count"),
    mean_victim_drop=("victim_drop_rate", "mean"),
    std_victim_drop=("victim_drop_rate", "std"),
    mean_victim_p95=("victim_p95_latency", "mean"),
    std_victim_p95=("victim_p95_latency", "std"),
    mean_att_drop=("attacker_drop_rate", "mean"),
    std_att_drop=("attacker_drop_rate", "std"),
    mean_co_loc_prob=("co_loc_prob", "mean"),
    
).reset_index()

# standard error
for col in ["victim_drop", "victim_p95", "att_drop"]:
    s = agg[f"std_{col}"].fillna(0.0)
    n = agg["runs"].clip(lower=1)
    agg[f"sem_{col}"] = s / np.sqrt(n)

# change scheduler names for plotting
agg["scheduler"] = agg["scheduler"].replace({"spread": "DoubleDip"})
agg["scheduler"] = agg["scheduler"].replace({"openwhisk": "OpenWhisk"})
agg["scheduler"] = agg["scheduler"].replace({"helper": "Helper"})
agg["scheduler"] = agg["scheduler"].replace({"random": "Random"})

plt.figure(figsize=(10,3.5))

plt.subplot(1,2,1)
for sched in ["DoubleDip", "Helper", "Random"]:
    if sched == "DoubleDip":
        color="#0077b6"
    elif sched == "Helper":
        color="#f4a261"
    else:
        color="#2a9d8f"
    sub = agg[agg["scheduler"] == sched].sort_values("attack_intensity")
    plt.errorbar(sub["attack_intensity"], sub["mean_victim_drop"],
                 marker="o", capsize=3, label=sched, color=color)
plt.xlabel("Attacker intensity (attacks per benign invocation)")
plt.ylabel("Victim drop rate")
plt.title("(a) Victim drop rate vs attack intensity")
plt.legend()

plt.subplot(1,2,2)
for sched in ["DoubleDip", "Helper", "Random"]:
    if sched == "DoubleDip":
        color="#0077b6"
    elif sched == "Helper":
        color="#f4a261"
    else:
        color="#2a9d8f"
    sub = agg[agg["scheduler"] == sched].sort_values("attack_intensity")
    plt.errorbar(sub["attack_intensity"], sub["mean_victim_p95"],
                 marker="o", capsize=3, label=sched, color=color)
plt.xlabel("Attacker intensity (attacks per benign invocation)")
plt.ylabel("Victim p95 latency (time units)")
plt.title("(b) Victim p95 latency vs attack intensity")

plt.legend()
plt.tight_layout()
plt.savefig("figs/case_study_B1_fig.pdf")