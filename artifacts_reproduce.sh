#!/usr/bin/env bash
set -euo pipefail

# Always run relative to repo root
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

# Optional: log everything
mkdir -p logs results figs

# Build the experiment binary if it doesn't exist
if [ ! -f kumo_experiment ]; then
    echo "[INFO] Building kumo_experiment..."
    make
fi

# Run experiments (sorted to ensure consistent order)
for exp in $(ls -1 ./configs/case_study*.cfg | sort); do
  echo "[INFO] Running experiment with config: $exp"
  ./kumo_experiment "$exp" > "logs/$(basename "${exp%_config.cfg}").log"
done
echo "[INFO] All experiments completed."

# Generate plots (sorted)
for plot in $(ls -1 ./plot/case_study*.py | sort); do
  echo "[INFO] Generating plot: $plot"
  python3 "$plot"
done
echo "[INFO] All plots generated."

# Sanity check expected outputs
echo "[INFO] Checking expected outputs..."
test -f results/case_study_A_result.csv
test -f results/case_study_B1_result.csv
test -f results/case_study_B2_result.csv
test -f results/case_study_B3_result.csv
test -f figs/case_study_A_fig.pdf
test -f figs/case_study_B1_fig.pdf
test -f figs/case_study_B2_fig.pdf
test -f figs/case_study_B3_fig.pdf
echo "[INFO] Sanity check passed."