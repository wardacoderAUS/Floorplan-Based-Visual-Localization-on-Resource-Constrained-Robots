# Counter-Based Stage Attribution Harness

We built a small, portable C harness that reports **how long** each stage of a program takes on a
board, and **why**: compute, memory (and which part of memory), other stalls, or waiting.

It works with **any program** (only the stage boundaries are marked) and on **any
board** (Linux laptops and single-board computers, ESP32-P4, GAP9), using the most detail
each board can provide.

---

## Goal

When the same program runs on different boards, its stages slow down for different
reasons: one board is limited by arithmetic, another by slow external memory, another by a
small cache. A stage time alone cannot tell these apart. The tools we reviewed either report
times without causes, or explain causes but are tied to one platform, add large overhead, or
need the program analysed beforehand.

This harness labels each stage of an unknown program with its limiting resource, in the same
vocabulary on every board, and states how accurate each label is.

---

## How it works

Two phases. Neither reads or analyses the program's code.

### Phase 1: hardware sweep (once, at start-up)

Measures the **board**, not the program, and produces its "prices":

| Step | How | Result |
|---|---|---|
| Peak compute rate | Loop of 8 independent integer adds (no memory, no dependencies) | `IPC_peak = instructions / cycles` |
| Memory curve | Pointer-chasing loop (each read depends on the previous) over 4 KB, 16 KB, 64 KB, ... in every memory region | time, cycles and misses per access at each size |
| Memory tiers | New tier where time per access jumps more than 1.5x, or the memory region changes | the board's memory levels (needs no counters) |
| Miss prices | Least-squares fit over all sizes: `cycles/access = c0 + p_near * near_misses/access + p_far * far_misses/access` | `p_near`, `p_far` = extra cycles one miss costs |

### Phase 2: per-stage measurement (every run)

The program under test wraps each stage:

```c
pmu_init();                        // hardware sweep
...
pmu_begin(0); stage0(); pmu_end(0);
pmu_begin(1); stage1(); pmu_end(1);
...
pmu_report(names, n_stages);       // print the attribution table
```

At `pmu_begin` / `pmu_end` the harness reads the hardware counters and two clocks and
accumulates the differences. Nothing is printed or allocated while measuring.

### What is counted

| Symbol | Counter | Meaning |
|---|---|---|
| `C` | cycles | clock cycles while the stage ran |
| `I` | instructions | instructions completed |
| `M_near` | first-level misses | data not found in the fastest memory |
| `M_far` | slowest-memory misses | data fetched from main memory / external RAM |
| `T_wall` | wall clock | real time |
| `T_cpu` | thread CPU clock | time the stage was actually running |

Every counter is optional; the harness uses whichever ones the board has.

### The attribution (expected vs measured)

For each stage, the counted work is turned into **expected cycles** using the prices from
the sweep, and compared with the **measured cycles** `C`:

$$C_{compute} = \min\left(\frac{I}{IPC_{peak}},\ C\right) \qquad C_{lost} = C - C_{compute}$$

$$C_{mem} = p_{near} M_{near} + p_{far} M_{far} \qquad C_{other} = C_{lost} - \min(C_{mem},\ C_{lost})$$

Accuracy of the explanation:

$$E = \frac{C_{compute} + C_{mem}}{C}$$

- `E ≈ 100%`: the counted work fully explains the measured time.
- `E < 100%`: time the counters do not see (branch mispredictions, dependency chains).
- `E > 100%`: overlapping misses (prices are measured for dependent misses), so memory is capped at `C_lost`.

Each part becomes a share of wall time with the running fraction `r = T_cpu / T_wall`:

| Share | Value |
|---|---|
| compute | `r * C_compute / C` |
| memory: near / slowest | `r * p_near*M_near / C`, `r * p_far*M_far / C` (after capping) |
| other stalls | `r * C_other / C` |
| waiting | `1 - r` |

**Label** = the largest share: `COMPUTE`, `MEMORY (near memory)`, `MEMORY (slowest memory)`,
`STALLED` or `WAIT`. Without an instruction counter the cause of running time is unknown, so
the column becomes `running` and the label `RUNNING`.

---

## Why it generalizes

- **Any code:** expected time comes from work the hardware counted, so the program is never
  inspected. Each stage boundary costs only a few counter reads.
- **Any hardware:** `pmu.c` has a small platform layer (counters, clocks, memory regions),
  chosen automatically at compile time, and a method that is identical everywhere.
- **Generic counter roles** ("first-level miss", "slowest-memory miss") instead of cache names.
- **Every counter is checked at start-up:** missing, not advancing, or never registering misses → disabled. The output says less rather than something wrong.
- **Prices are measured** on the board, never taken from a datasheet.
- **Memory probed by kind and size:** caches by working-set size on Linux; TCM / SRAM / PSRAM / L2 measured separately on microcontrollers.
- **Tiers need no counters**, so even a counter-less board shows its memory structure.
- 32-bit counter wrap-around and small memories are handled.

| Board | Cycles | Instructions | Memory detail | Waiting |
|---|---|---|---|---|
| Linux laptop, Jetson, Raspberry Pi | yes | yes | first-level + main-memory misses | yes |
| ESP32-P4 | yes | yes (RISC-V counter, to confirm on hardware) | L1 + PSRAM/flash misses (ESP-IDF cache counters) | no |
| GAP9 (main core) | yes | yes | not on the main core | no |

A board (or virtual machine) that exposes no counters still gets the clock-only result:
waiting vs running, plus its memory tiers.

---

## Validation workload (`demo.c`)

Four stages, each built to exercise one resource, so the correct label is known in advance.
The stages tell the harness nothing; it sees only counters and clocks.

| Stage | What it does | Expected |
|---|---|---|
| `wait` | sleeps 2 ms | WAIT |
| `compute` | 4 independent float multiply-add chains in registers | COMPUTE |
| `seq-mem` | reads a buffer in the slowest memory, in order (bandwidth) | MEMORY |
| `rand-mem` | reads the same buffer at random positions (latency) | MEMORY |

---

## Files

| File | Purpose |
|---|---|
| `pmu.h` | the API: `pmu_init`, `pmu_begin`, `pmu_end`, `pmu_report` |
| `pmu.c` | platform layer + hardware sweep + measurement + attribution |
| `demo.c` | the validation workload |
| `esp/` | ESP-IDF project for ESP32 / ESP32-P4 |
| `gap/` | GAP SDK project for GAP9 |
| `CODE_WALKTHROUGH.md` | what each part of the code does |

---

## Build and run

**Linux (laptop with Ubuntu, Jetson, Raspberry Pi)**

```bash
sudo sysctl kernel.perf_event_paranoid=2      # allow reading hardware counters
gcc -O2 pmu.c demo.c -o demo
taskset -c 0 ./demo                           # keep it on one core
```

**ESP32-P4** (ESP-IDF installed; a recent version provides the cache counters)

```bash
cd esp && idf.py set-target esp32p4 && idf.py build flash monitor
```

**GAP9** (GAP SDK installed)

```bash
cd gap && cmake -B build && cmake --build build --target run
```

Status: the Linux build compiles and runs; the ESP32-P4 and GAP9 builds are written against
the documented ESP-IDF and GAP SDK APIs. Validation on the target boards is the next step.
