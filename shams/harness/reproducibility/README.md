# Reproducibility

How to repeat every experiment exactly, on any of the boards, and what must be recorded so
that results can be compared and checked.

## What is in this folder

| File | What it does |
|---|---|
| `run_linux.sh` | On any Linux machine (Raspberry Pi 5, Jetson Orin Nano, a laptop, or Linux in Docker): records the machine's conditions, builds the harness and the validation program, profiles the program N times, saves everything in `experiments/<date>-<name>/` |
| `run_mac_docker.sh` | On a Mac: records the Mac's details, then runs `run_linux.sh` inside a Linux container (Docker image `gcc:14`) |

## Why

A timing or profiling result is only meaningful if the conditions it was measured under are
known: CPU, kernel, frequency governor, which counters were available, compiler, temperature.
The literature review (`../literature-review`) found that most tools do not record these. The
scripts record them automatically next to every result, use the same compiler flags
everywhere, run the program several times, and never overwrite earlier results.

## What is recorded

| File | Contents |
|---|---|
| `system.txt` | date; kernel; OS (and Jetson release); CPU model and topology (`lscpu`); number of cores; frequency governor of every core; `perf_event_paranoid`; counter devices and the ARM events the kernel lists; compiler version; temperatures before and after |
| `host.txt` (Mac only) | macOS version, chip, cores, memory, Docker version and the CPUs/memory given to Docker |
| `output.txt` | everything the harness printed: counters found, prices, memory tiers, and the four tables for every run |

## Procedure on each board

### Common rules

- Use the same source: the files in `../pmu_harness`, unchanged.
- Build with `gcc -O2 … -pthread` (the scripts do this).
- Run at least **3 times**; report the mean and the range.
- Leave the machine idle, on mains power, during the runs.
- On Linux boards, fix the clock speed (the harness measures the cycle length once):
  `sudo cpupower frequency-set -g performance` or, without `cpupower`,
  `echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor`.
- Allow the counters to be read: `sudo sysctl kernel.perf_event_paranoid=2`.

### Raspberry Pi 5 / Jetson Orin Nano

```bash
sudo apt install -y build-essential            # compiler, once
sudo sysctl kernel.perf_event_paranoid=2
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
# Jetson only, for maximum, fixed clocks:  sudo nvpmodel -m 0 && sudo jetson_clocks
cd shams/harness
bash reproducibility/run_linux.sh raspberry-pi-5 3        # or jetson-orin-nano
```

### Mac (Linux in Docker, no counters)

1. Install Docker Desktop, open it, wait for **Engine running**; Settings → VM resources → CPU limit ≥ 4.
2. From `shams/harness`: `bash reproducibility/run_mac_docker.sh macbook-air 3`

### ESP32-P4 (ESP-IDF)

```bash
cd shams/harness/pmu_harness/esp
idf.py set-target esp32p4
idf.py build flash monitor | tee ../../experiments/esp32-p4-output.txt
```

The settings the harness needs (1 ms RTOS tick, FreeRTOS run-time statistics, PSRAM for
large buffers) are in `esp/sdkconfig.defaults` and are applied automatically. Record the
ESP-IDF version (`idf.py --version`) with the output.

### GAP9 (GAP SDK)

```bash
cd shams/harness/pmu_harness/gap
cmake -B build && cmake --build build --target run | tee ../../experiments/gap9-output.txt
```

Record the GAP SDK version with the output.

## Checking a result

For the validation program (`../pmu_harness/workload.c`) the correct labels are known in
advance; a result is correct when:

| Thread | Expected |
|---|---|
| `compute` | COMPUTE (with counters) or RUNNING (without) |
| `rand-mem` | MEMORY / last cache level (with counters) or RUNNING (without) |
| `seq-mem` | MEMORY, with a large hidden share on Pi 5 / Jetson |
| `sleeper`, main | WAITING / blocked |
| `shared1..3` | WAITING / preempted, about 2/3 each |

and the shares of the three runs agree within a few percent. Any difference should be
explained in the experiment's README.
