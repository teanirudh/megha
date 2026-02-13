import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

dfA = pd.read_csv(f"results/case_study_A_result.csv")

def prepare_metrics(df: pd.DataFrame) -> pd.DataFrame:
    df = df.copy()

    denom = df["cold_count"] + df["warm_count"]
    df["cold_start_rate_f1"] = np.where(denom > 0, df["cold_count"] / denom, np.nan)

    df["co_loc_prob"] = np.where(
        df["victim_arrivals"] > 0,
        df["coloc_count"] / df["victim_arrivals"],
        np.nan,
    )

    # keep raw per-run ttf; assume:
    #   >=0 : observed time
    #   <0 or NaN : censored (never observed in run)
    if "time_to_first_coloc" in df.columns:
        df["ttf_coloc"] = df["time_to_first_coloc"]
    else:
        df["ttf_coloc"] = np.nan

    return df

dfA = prepare_metrics(dfA)

scheduler_order = ["spread", "random", "helper", "openwhisk"]
sched_colors = {
    "DoubleDip":     "#0077b6",
    "Random":     "#2a9d8f",
    "Helper":     "#f4a261",
    "OpenWhisk":  "#e76f51",
}


def sem(x: pd.Series) -> float:
    x = x.dropna()
    if len(x) <= 1:
        return np.nan
    return x.std(ddof=1) / np.sqrt(len(x))

def mean_observed_ttf(x: pd.Series) -> float:
    x = x.dropna()
    x = x[x >= 0]
    return x.mean() if len(x) > 0 else np.nan

def sem_observed_ttf(x: pd.Series) -> float:
    x = x.dropna()
    x = x[x >= 0]
    return sem(x)

aggA = (
    dfA.groupby("scheduler")
       .agg(
           runs=("scheduler", "size"),

           mean_cold_rate=("cold_start_rate_f1", "mean"),
           sem_cold_rate=("cold_start_rate_f1", sem),

           mean_co_loc_prob=("co_loc_prob", "mean"),
           sem_co_loc_prob=("co_loc_prob", sem),

           mean_ttf_coloc=("ttf_coloc", mean_observed_ttf),
           sem_ttf_coloc=("ttf_coloc", sem_observed_ttf),

           # Optional: how often co-location was *ever observed* in a run
           ttf_obs_runs=("ttf_coloc", lambda s: np.sum((s.dropna() >= 0).astype(int))),
       )
       .reset_index()
)

aggA = aggA.set_index("scheduler").reindex(scheduler_order).reset_index()

# change labels for plotting
aggA["scheduler"] = aggA["scheduler"].replace({"spread": "DoubleDip"})
aggA["scheduler"] = aggA["scheduler"].replace({"openwhisk": "OpenWhisk"})
aggA["scheduler"] = aggA["scheduler"].replace({"helper": "Helper"})
aggA["scheduler"] = aggA["scheduler"].replace({"random": "Random"})


plt.rcParams.update({
    "axes.grid": False,
    "axes.axisbelow": True,
    "grid.linestyle": "--",
    "font.size": 11,
    "axes.titlesize": 12,
    "axes.labelsize": 11,
})

fig, axes = plt.subplots(2, 2, figsize=(10, 7))
(ax_co, ax_cold), (ax_trade, ax_ttf) = axes

xs = np.arange(len(aggA))
colors = [sched_colors[s] for s in aggA["scheduler"]]

# (a) Co-location probability
ax_co.bar(xs, aggA["mean_co_loc_prob"].fillna(0.0),
          capsize=3, color=colors, width=0.175, edgecolor='black', linewidth=0.8)
ax_co.set_xticks(xs)
ax_co.set_xticklabels(aggA["scheduler"], rotation=30, ha="center")
ax_co.set_ylabel("P(co-location | victim invocation)")
# ax_co.set_ylim(0, 0.05)
ax_co.set_title("(a) Co-location probability")

# (b) Cold-start rate
ax_cold.bar(xs, aggA["mean_cold_rate"].fillna(0.0),
            capsize=3, color=colors, width=0.175, edgecolor='black', linewidth=0.8)
ax_cold.set_xticks(xs)
ax_cold.set_xticklabels(aggA["scheduler"], rotation=30, ha="center")
ax_cold.set_ylabel("Cold start rate")
ax_cold.set_ylim(0, 1.01)
ax_cold.set_title("(b) Cold-start rate")

# (c) Tradeoff scatter
ax_trade.grid(True, linestyle='--')
for _, row in aggA.iterrows():
    sched = row["scheduler"]
    x = row["mean_cold_rate"]
    y = row["mean_co_loc_prob"]
    if pd.isna(x) or pd.isna(y):
        continue
    ax_trade.scatter(x, y, color=sched_colors[sched])
    if sched == "DoubleDip":
        ax_trade.text(x, y, "  "+sched, fontsize=9, ha="left", va="bottom")
    elif sched == "OpenWhisk":
        ax_trade.text(x, y, "  OpenWhisk", fontsize=9, ha="left", va="bottom")
    else:
        ax_trade.text(x, y, "  "+sched, fontsize=9, ha="left", va="bottom")
    

    

ax_trade.set_xlabel("Cold start rate")
ax_trade.set_ylabel("P(co-location | victim invocation)")
ax_trade.set_xlim(0, 1)
ax_trade.set_ylim(-0.01, 0.4)
ax_trade.set_title("(c) Security–performance tradeoff")

# (d) Time-to-first (only those with observed events)
ttf_subset = aggA[~aggA["mean_ttf_coloc"].isna()].copy()
xs2 = np.arange(len(ttf_subset))
colors2 = [sched_colors[s] for s in ttf_subset["scheduler"]]

ax_ttf.bar(xs2, ttf_subset["mean_ttf_coloc"], 
           capsize=3, color=colors2, width=0.125, edgecolor='black', linewidth=0.8)
ax_ttf.set_xticks(xs2)
ax_ttf.set_xticklabels(ttf_subset["scheduler"], rotation=30, ha="center")
ax_ttf.set_ylabel("Time to first co-location (time units)")
ax_ttf.set_ylim(0, max(ttf_subset["mean_ttf_coloc"])*1.2)
ax_ttf.set_title("(d) Time to first co-location")

plt.tight_layout()
plt.savefig(f"figs/case_study_A_fig.pdf", bbox_inches="tight")