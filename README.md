## For CCGRID 2026 artifact reproduction instructions, see ARTIFACT.md.

# Kumo: A Modular Serverless Security Simulator

Kumo is a modular, event-driven simulator for evaluating performance and security in multi-tenant serverless platforms.

This repository contains:

- The full Kumo simulator implementation
- The artifact accompanying the IEEE CCGRID 2026 paper:

  Kumo: A Security-Focused Serverless Cloud Simulator

Artifact DOI:
https://doi.org/10.5281/zenodo.18635971



## Overview

Kumo models:

- Function cold/warm starts
- Container lifecycle (idle timeout → cooling → eviction)
- Pluggable schedulers (Spread, Random, Helper, OpenWhisk, PASch, etc.)
- Workloads: Uniform, Poisson, Burst
- Multi-function tenants
- Worker heterogeneity
- Pre-warmed container pools
- Victim/attacker co-location attacks
- Config-driven experiments and parameter sweeps
- CSV-based metrics export
- Optional detailed tracing/logging

Kumo is designed to support reproducible experiments for cloud scheduling, co-location attacks, and serverless security research.



## Project Structure
```
src/
  common/          Base types and utilities  
  config/          Config parser  
  core/            Engine, Events, Scenario, ExperimentRunner, TraceLogger  
  metrics/         Metric Collector  
  model/           Apps, functions, workers, containers  
  scheduler/       Pluggable schedulers + registry  
  workload/        Uniform, Poisson, Burst, Attack workloads  
  main_experiment.cpp  

configs/  
  Experiment configurations  

plot/  
  Python plotting scripts  

results/  
  Generated CSV logs  

tests/  
  Unit tests  
```


## Build

Build the simulator:

```bash
make
```

This produces the binary: kumo_experiment

Run an experiment:
```bash
./kumo_experiment <config-file>
```


## Core Capabilities

### Schedulers

- spread — avoid tenant overlap (security-oriented)
- random — uniform random placement
- helper — warm-aware scheduling
- openwhisk — OpenWhisk-like placement
- openwhisk_warm — warm-enhanced OpenWhisk
- pasch — tenant packing strategy

### Workloads

- Uniform — constant-rate batched arrivals
- Poisson — exponential inter-arrival times
- Burst — periodic high-rate spikes

All workloads support optional attacker traffic models.

### Tenancy & Resource Modeling

- Multi-function tenants (functions_per_tenant=N)
- Worker heterogeneity (hetero.enabled=1)
- Idle timeouts for container eviction
- Pre-warmed container pools (prewarm.enabled=1)

### Experiment Automation

Supports parameter sweeps over:

- schedulers
- random seeds
- idle timeouts

Exports metrics to CSV:

- cold/warm starts
- co-location counts
- per-tenant invocation stats
- worker stats

Optional detailed trace logging (trace.enabled=1).



## Example Usage

Run a configuration file:

./kumo_experiment configs/example.cfg

Parameter sweep example:

./kumo_experiment configs/sweep_example.cfg



## Artifact Reproduction (CCGRID 2026)

To reproduce the published CCGRID results:
```bash
make reproduce
```
See ARTIFACT.md for full reproduction instructions.



## License

Released under the MIT License.
