# Counter-Based Performance Attribution Harness

A small, portable C harness that profiles **how a board runs any program**: for every thread,
what the code did (instructions, loads, stores, arithmetic, branches, cache misses), where
its time went, and why. Time is divided into five categories that are the same on every board:

| Category | Sub-categories (where the board can tell them apart) |
|---|---|
| **COMPUTE** | issue at full speed, waiting on arithmetic results |
| **MEMORY** | L1 / L2 / L3 misses |
| **STALLED** | other pipeline stalls |
| **KERNEL** | the operating system working for the thread |
| **WAITING** | preempted (another thread had the core), blocked (sleep, I/O) |

It also separates **visible** delays from memory latency **hidden** by out-of-order execution,
labels each thread with its main cause, and states how much of the time it can explain.
Running the same program on different boards shows how their hardware differs.

Targets: Raspberry Pi 5, Jetson Orin Nano, ESP32-P4, GAP9 (and any Linux machine).

## Folders

| Folder | What is inside | Read it for |
|---|---|---|
| [`literature-review/`](literature-review/) | the review of existing timing tools and the gap they leave | why this harness is needed |
| [`pmu_harness/`](pmu_harness/) | the code: `pmu.c`, `pmu.h`, `pmurun.c`, the validation program `workload.c`, microcontroller projects; `README.md` (method, equations, usage) and `CODE_WALKTHROUGH.md` (every part of the code explained) | how it works, how to use it |
| [`report/`](report/) | the report (PDF and LaTeX source) | the full academic description, results and analysis |
| [`experiments/`](experiments/) | one folder per experiment: raw output, conditions, tables, figures, analysis | the results |
| [`reproducibility/`](reproducibility/) | scripts and the exact procedure for every board | repeating any experiment |

## How it works, in short

1. **Hardware sweep (once, at start-up):** small loops with known behaviour measure the
   board's "prices": peak instruction rate, the wait for a dependent arithmetic result, the
   latency of each memory tier, and the cost of a miss at each cache level.
2. **Measure:** on Linux, `pmurun` launches any program unchanged and attaches the hardware
   counters to every thread before its first instruction; on microcontrollers the code runs
   inside `pmu_profile()`.
3. **Explain:** counted work × prices = expected time, compared with the measured time; the
   difference and the scheduler's records give the categories, the hidden delays and the label.

## Quick start (Linux board)

```bash
cd pmu_harness
sudo sysctl kernel.perf_event_paranoid=2
gcc -O2 pmu.c pmurun.c -o pmurun -pthread
gcc -O2 workload.c -o workload -pthread
./pmurun ./workload            # or: ./pmurun ./any_program args
```

To record a complete, repeatable experiment: `bash reproducibility/run_linux.sh <machine-name> 3`.

## Status

| Item | Status |
|---|---|
| Code (Linux, ESP32-P4, GAP9) | written; Linux build compiles (x86, ARM) and runs |
| Experiment 01: laptop, Linux in Docker, no counters | done: thread discovery, preempted / blocked split, memory tiers confirmed |
| Raspberry Pi 5, Jetson Orin Nano (full method, with counters) | next |
| ESP32-P4, GAP9 | to be confirmed on hardware |
