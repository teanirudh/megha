# pylint: disable-all

import sys
from dataclasses import dataclass
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


@dataclass(frozen=True, kw_only=True)
class ChartConfig:
    xkeys: tuple[str, ...] = ("random", "helper", "guardian")
    xlabels: tuple[str, ...] = ("Random", "Helper", "Guardian")
    colors: tuple[str, ...] = ("#d62728", "#f4a261", "#2ca02c")
    column: str
    ylabel: str
    title: str
    caption: str
    file: str


CONFIG: tuple[ChartConfig, ...] = (
    ChartConfig(
        column="tail_latency",
        ylabel="percentile_95_latency",
        title="Performance",
        caption="Tail Latency (↓ better)",
        file="experiment/output/fig_performance.png",
    ),
    ChartConfig(
        column="cold_start_rate",
        ylabel="cold_starts / (cold_starts + warm_starts)",
        title="Efficiency",
        caption="Cold-start Rate (↓ better)",
        file="experiment/output/fig_efficiency.png",
    ),
    ChartConfig(
        column="colocation_probability",
        ylabel="total_colocations / total_victim_arrivals",
        title="Security",
        caption="Co-location Probability (↓ better)",
        file="experiment/output/fig_security.png",
    ),
)

PNG_DPI = 300
PNG_FIGSIZE = (8, 6)

RESULT_COLUMNS = [
    "seed",
    "scheduler",
    "workload",
    "num_tenants",
    "num_workers",
    "tail_latency",
    "cold_start_rate",
    "colocation_probability",
]


def relative_percentage(values: np.ndarray) -> list[str]:
    b = float(values[0])
    out: list[str] = [""]
    if not np.isfinite(b) or b <= 0:
        return [""] * len(values)
    for v in values[1:]:
        fv = float(v)
        if not np.isfinite(fv):
            out.append("")
            continue
        if fv < b:
            p = (b - fv) / b * 100
            out.append(f"{p:.2f}% ↓")
        elif fv > b:
            p = (fv - b) / b * 100
            out.append(f"{p:.2f}% ↑")
        else:
            out.append("")
    return out


def load_results() -> pd.DataFrame:
    path = (
        Path(sys.argv[1])
        if len(sys.argv) > 1
        else Path("experiment/output/results.csv")
    )
    if not path.is_file():
        raise SystemExit(f"Missing results file: {path}")

    try:
        df = pd.read_csv(path)
    except pd.errors.ParserError as e:
        raise SystemExit(f"Unable to parse {path}\n") from e

    missing = [c for c in RESULT_COLUMNS if c not in df.columns]
    if missing:
        raise SystemExit(f"Columns missing: {missing}")

    numeric_cols = (
        "num_tenants",
        "num_workers",
        "tail_latency",
        "cold_start_rate",
        "colocation_probability",
    )
    for col in numeric_cols:
        df[col] = pd.to_numeric(df[col], errors="coerce")

    return df


def export_chart(df: pd.DataFrame, chart: ChartConfig) -> None:
    out = Path(chart.file)

    values = df.groupby("scheduler")[chart.column].mean().reindex(chart.xkeys)
    vals = np.asarray(values.values, dtype=float)
    labels = list(chart.xlabels)
    colors = list(chart.colors)
    x = np.arange(len(chart.xkeys))

    secondary_lines = relative_percentage(vals)

    fig, ax = plt.subplots(figsize=PNG_FIGSIZE, dpi=PNG_DPI)
    bars = ax.bar(
        x,
        vals,
        color=colors,
        width=0.55,
        edgecolor="black",
        linewidth=0.9,
        zorder=2,
    )

    val_offset_pts = 22
    pct_offset_pts = 5
    val_only_offset_pts = 11

    for i, bar in enumerate(bars):
        val = vals[i]
        y_top = bar.get_height()
        cx = bar.get_x() + bar.get_width() / 2
        abs_str = format(val, ".2f") if np.isfinite(val) else "—"
        show_secondary = False

        if i > 0:
            sl = secondary_lines[i]
            if sl and sl != "—":
                ax.annotate(
                    sl,
                    xy=(cx, y_top),
                    xytext=(0, pct_offset_pts),
                    textcoords="offset points",
                    ha="center",
                    va="bottom",
                    fontsize=9,
                    fontweight="normal",
                    color="0.42",
                    clip_on=False,
                )
                show_secondary = True

        v_off = val_offset_pts if show_secondary else val_only_offset_pts
        ax.annotate(
            abs_str,
            xy=(cx, y_top),
            xytext=(0, v_off),
            textcoords="offset points",
            ha="center",
            va="bottom",
            fontsize=11,
            fontweight="bold",
            color="black",
            clip_on=False,
        )

    ax.set_xticks(x)
    ax.set_xticklabels(labels, fontsize=12)

    ax.set_title(chart.title, fontsize=14, fontweight="bold", y=1.14, pad=0)
    ax.text(
        0.5,
        1.065,
        chart.caption,
        transform=ax.transAxes,
        ha="center",
        va="bottom",
        fontsize=10,
        color="0.42",
        clip_on=False,
    )

    ax.set_ylabel(chart.ylabel, fontsize=12)
    ax.set_ylim(0, 100 if chart.column == "tail_latency" else 1)

    ax.set_axisbelow(True)
    ax.yaxis.grid(True, linestyle="--", linewidth=0.6, alpha=0.7, color="0.75")
    ax.xaxis.grid(False)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)

    plt.tight_layout(rect=(0, 0, 1, 0.85))

    out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(
        out,
        dpi=PNG_DPI,
        bbox_inches="tight",
        facecolor="white",
        edgecolor="none",
    )
    plt.close(fig)
    print(f"Saved: {out}")


def main() -> None:
    df = load_results()
    for chart in CONFIG:
        export_chart(df, chart)


if __name__ == "__main__":
    main()
