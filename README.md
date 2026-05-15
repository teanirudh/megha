# Megha: Serverless Security Simulator

## Overview

This codebase is derived from [Kumo](https://github.com/weishao-sec/kumo), a security-focused serverless cloud simulator, and was adapted into **Megha**.

The project was developed as part of **CS6630: Secure Processor Microarchitecture** at the Indian Institute of Technology Madras during the Jan–May 2026 semester.

## Environment

- **Simulator:** `c++17` compiler (`g++` or `clang++`) and `make`.
- **Visualization:** `python3` with `numpy`, `pandas`, `matplotlib`.

## Experiment

- Run all commands from the repository root.
- Configurations can be modified in [configs.cfg](experiment/input/configs.cfg).
- Generated outputs are stored in [output/](experiment/output/).

```bash
make build    # compile the simulator and generate the binary
make run      # run the experiment sweep and generate results
make plot     # generate plots from the results
make clean    # remove generated binaries and output files
```
