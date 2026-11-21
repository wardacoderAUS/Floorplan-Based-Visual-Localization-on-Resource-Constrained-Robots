# Experiments

Every run of the harness on a machine gets its own folder here, so that results are never
overwritten and every number in the report can be traced back to its raw output.

## Experiments so far

| Folder | Machine | Counters | What it tested | Status |
|---|---|---|---|---|
| [`01-laptop-macbook-air-docker`](01-laptop-macbook-air-docker/) | MacBook Air, Linux in Docker | none | thread discovery, running / preempted / blocked, memory tiers, missing-counter handling | done |
| `02-raspberry-pi-5` | Raspberry Pi 5 (Cortex-A76) | full | the complete method | planned |
| `03-jetson-orin-nano` | Jetson Orin Nano (Cortex-A78AE) | full | the complete method | planned |
| `04-esp32-p4` | ESP32-P4 | cycles, instructions, L1/L2 | microcontroller path (`pmu_profile`) | planned |
| `05-gap9` | GAP9 | cycles, instructions | single-core path | planned |

## What each experiment folder contains

| File | What it is |
|---|---|
| `README.md` | purpose, setup, exact commands, results tables, analysis |
| `raw/output.txt` (or `output.txt`) | the complete, unedited output of every run |
| `system.txt` (and `host.txt` on a Mac) | the conditions of the run: OS, kernel, CPU, cores, frequency governor, counters available, compiler, temperatures before and after |
| `summary.csv` | per-thread numbers over the runs (mean, min, max) |
| figures | plots used in the README and the report |

## How to add an experiment

From the `harness/` folder:

```bash
bash reproducibility/run_linux.sh raspberry-pi-5 3        # on a Linux board
bash reproducibility/run_mac_docker.sh macbook-air 3      # on a Mac
```

The script creates `experiments/<date>-<name>/` with `system.txt` and `output.txt`. Rename
the folder with the next number (for example `02-raspberry-pi-5`), and add a `README.md`
following experiment 01. See [`../reproducibility`](../reproducibility/) for the full procedure.
