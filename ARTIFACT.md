# Kumo: Security-Focused Serverless Cloud Simulator

This repository contains the artifact accompanying the IEEE CCGRID 2026 paper:

Kumo: A Security-Focused Serverless Cloud Simulator

Artifact DOI:
https://doi.org/10.5281/zenodo.18635971


## 1. Quick Start (Reproduce All Results)

### Build the simulator

make

### Install Python dependencies

pip install -r requirements.txt

### Reproduce all experiments and figures

make reproduce

## 2. Expected Outputs

After successful reproduction:

CSV results in `results/`
- case_study_A_result.csv
- case_study_B1_result.csv
- case_study_B2_result.csv
- case_study_B3_result.csv

Figures in `figs/`
- Case Study A (Figure 2)
- Case Study B1 (Figure 3)
- Case Study B2 (Figure 4)
- Case Study B3 (Figure 5)

Estimated runtime: 30–60 minutes on a commodity desktop.



## 3. System Requirements

### Hardware
- Standard laptop or desktop
- Single CPU core sufficient
- No GPU required

### Software
- Linux or macOS
- g++ with C++17 support (GCC ≥ 7 recommended)
- Python 3.9+
- Python packages: numpy, pandas, matplotlib

Install Python dependencies:

pip install -r requirements.txt



## 4. Repository Structure

src/        C++17 simulator implementation  
configs/    Experiment configurations  
plot/       Python plotting scripts  
results/    Generated CSV outputs  
figs/       Generated figures  



## 5. Mapping to Paper Results

Figure 2(a–d)  → configs/case_study_A.cfg  
Figure 3       → configs/case_study_B1.cfg  
Figure 4       → configs/case_study_B2.cfg  
Figure 5       → configs/case_study_B3.cfg  
Table I        → Execution logs  



## 6. License

Released under the MIT License.



## 7. Citation

If you use this artifact, please cite:

Kumo: A Security-Focused Serverless Cloud Simulator, IEEE CCGRID 2026.
