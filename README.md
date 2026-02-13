
## For CCGRID 2026 artifact reproduction instructions, see ARTIFACT.md.


# **Kumo: A Modular Serverless Security Simulator**

Kumo is a modular, event-driven simulator for evaluating **performance and security** in multi-tenant serverless platforms.
It models:

* Function cold/warm starts
* Container lifecycle (idle timeout → cooling → eviction)
* Schedulers (Spread, Random, Helper, OpenWhisk, PASch, etc.)
* Workloads: Uniform, Poisson, Burst
* Multi-function tenants
* Worker heterogeneity
* Pre-warmed container pools
* Victim/attacker co-location attacks
* Config-driven experiments + parameter sweeps
* CSV results
* Optional detailed tracing/logging

Kumo is designed to support reproducible experiments for cloud scheduling, co-location attacks, and serverless security research.



# **Project Structure**

```
src/
  common/          - Base types and utilities
  config/          - Config parser
  core/            - Engine, Events, Scenario, ExperimentRunner, TraceLogger
  metrics/         - Metric Collector
  model/           - Apps, functions, workers, containers
  scheduler/       - Pluggable schedulers + registry
  workload/        - Uniform, Poisson, Burst, Attack workloads
  main_experiment.cpp
  main.cpp
configs/
  ... cfg experiment configurations.
results/
  ... CSV logs, trace logs, etc.
plot/
  ... Python plotting scripts
tests/
  ... unit tests
```



# **Build**

```bash
make
```

Then run experiments with:

```bash
./kumo_experiment <config-file>
```



# **Key Features**

### **Schedulers**

* `spread` — avoid tenant overlap (security-oriented)
* `random` — uniform random placement
* `helper` — warm-aware, reduce cold starts
* `openwhisk` — OpenWhisk-like placement
* `openwhisk_warm` — warm-enhanced
* `pasch` — tenant packing strategy



### **Workloads**

* **Uniform** — constant-rate batched arrivals
* **Poisson** — Exponential inter-arrival times
* **Burst** — Periodic high-rate spikes
* All workloads can be overlaid with an **attacker traffic model**



### **Tenancy & Resources**

* **Multi-function tenants** (`functions_per_tenant=N`)
* **Worker heterogeneity** (`hetero.enabled=1`)
* **Idle timeouts** for container eviction
* **Pre-warmed containers** (`prewarm.enabled=1`)



### **Experiment Automation**

* Parameter sweeps over:

  * schedulers
  * seeds
  * idle timeouts
* Export metrics to CSV:

  * cold/warm starts
  * co-location counts
  * per-tenant invocation stats
  * worker stats
* Optional detailed trace logging (`trace.enabled=1`)



# **Running Examples**

Below are minimal runnable examples that demonstrate each major feature.



## **1. Uniform Workload + Spread Scheduler + Attacker**

**Config:** `configs/example.cfg`

```ini
scheduler=spread
workload=uniform

num_tenants=3
num_workers=3
total_invocations=200
batch_size=10
time_step=5
seed=123

attacker.enabled=1
attacker.tenant=999
attacker.function=100
attacker.attack_per_victim=1.0

output_csv=results/spread_attack.csv
```

**Run:**

```bash
./kumo_experiment configs/example.cfg
```



## **2. Poisson Example**

**Config:** `configs/poisson_example.cfg`

```ini
scheduler=spread
workload=poisson

num_tenants=3
num_workers=3
total_invocations=200
batch_size=10
time_step=5
seed=123

arrival_rate=0.5

attacker.enabled=1
attacker.tenant=999
attacker.function=100
attacker.attack_per_victim=1.0

output_csv=results/spread_poisson_attack.csv
```

**Run:**

```bash
./kumo_experiment configs/poisson_example.cfg
```



## **3. Burst Workload Example**

**Config:** `configs/burst_example.cfg`

```ini
scheduler=spread
workload=burst

num_tenants=3
num_workers=3
total_invocations=200
batch_size=10
time_step=5
seed=123

burst.base_rate=0.1
burst.burst_rate=3.0
burst.period=60
burst.duty_cycle=0.25

attacker.enabled=1
attacker.tenant=999
attacker.function=100
attacker.attack_per_victim=1.0

output_csv=results/spread_burst_attack.csv
```

**Run:**

```bash
./kumo_experiment configs/burst_example.cfg
```



## **4. Worker Heterogeneity Example**

**Config:** `configs/hetero_example.cfg`

```ini
scheduler=spread
workload=poisson

num_tenants=3
functions_per_tenant=2
num_workers=4
total_invocations=120
batch_size=10
time_step=5
seed=123

arrival_rate=0.5
idle_timeout=60

# Attacker
attacker.enabled=1
attacker.tenant=999
attacker.function=100
attacker.attack_per_victim=1.0

# Worker capacity
worker.cpu_cores=4.0
worker.memory_mb=4096
worker.storage_mb=200

# Heterogeneous cluster: half workers are "small"
hetero.enabled=1
hetero.small_frac=0.5
hetero.small_cpu_scale=0.5
hetero.small_mem_scale=0.5

output_csv=results/hetero_spread_poisson.csv
```

**Run:**

```bash
./kumo_experiment configs/hetero_example.cfg
```



## **5. Pre-warmed Containers**

Add to any config:

```ini
prewarm.enabled=1
prewarm.per_function=2
```

This reduces cold starts for exercised functions.



## **6. Tracing / Logging**

Enable detailed trace logs:

```ini
trace.enabled=1
trace.file=results/kumo_trace.log
```

Traces include:

* Placement decisions
* Cold/warm starts
* Execution events
* Completion events
* Container cooling
* Co-location events



## **7. Full Parameter Sweep**

**Config:** `configs/sweep_example.cfg`

```ini
scheduler=spread        # used if no sweep.schedulers
workload=poisson

num_tenants=3
num_workers=3
total_invocations=200
batch_size=10
seed=1
arrival_rate=0.5
idle_timeout=60

attacker.enabled=1
attacker.tenant=999
attacker.function=100
attacker.attack_per_victim=1.0

output_csv=results/sweep_poisson_attack.csv

# Sweeps:
sweep.schedulers=spread,random,helper
sweep.seeds=1,2,3
sweep.idle_timeouts=10,60
```

**Run:**

```bash
./kumo_experiment configs/sweep_example.cfg
```

This produces all combinations (3 schedulers × 3 seeds × 2 timeouts = 18 total runs), each appended to the CSV.