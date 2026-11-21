# Counter-Based Performance Attribution Harness: the code

This folder holds the source code and its documentation. The report is in `../report`,
the experiments and their results in `../experiments`, and the exact procedure to repeat
them in `../reproducibility`.

We built a small, portable C harness that profiles **how a board runs any program**: for
every thread, what the code did (instructions, loads, stores, arithmetic, branches, cache
misses), where its time went (compute, memory, other stalls, the operating system, waiting),
which delays were visible and which were hidden by pipelining, and a label that names the
main cause. The output has the same categories on every board, so running the same program
on different boards shows how their hardware differs.

Targets: Raspberry Pi 5, Jetson Orin Nano, ESP32-P4, GAP9, and any other Linux machine.

---

## Goal

When the same program runs on different boards, it slows down for different reasons: one
board is limited by arithmetic, another by slow external memory, another by a small cache or
by the scheduler sharing a core. A run time alone cannot tell these apart. The tools we
reviewed either report times without causes, or explain causes but are tied to one platform,
add large overhead, or need the program analysed beforehand.

This harness:

- works with **any program, unchanged**: on Linux it launches the program and finds every
  thread by itself; on microcontrollers the code is compiled with it and run in one call;
- works on **any board**, using the most detail each board's counters can provide;
- explains, **for every thread**, what the code did and where its time went, separating
  **visible** delays (the core stopped, or the thread was not running) from **hidden** ones
  (memory latency covered by other work);
- uses **categories that are the same on every board** (COMPUTE, MEMORY, STALLED, KERNEL,
  WAITING), with **sub-categories** (L1/L2/L3, preempted/blocked, ...) where a board can tell
  them apart;
- states **how much of the time it can explain**.

---

## How to use it

| Way | Program changes | Boards | Result |
|---|---|---|---|
| **Run mode**: `./pmurun ./program args` | none | Linux (Pi 5, Jetson) | every thread, from its first instruction to its exit |
| **Profile one call**: `pmu_profile("name", fn)` | compile with the harness | all (the way to profile any code on ESP32-P4, GAP9) | the whole call; every task that used the core |
| **Stage markers**: `pmu_begin(s)` / `pmu_end(s)` | mark stages | all | each stage of each thread, per run |

---

## How it works

### Phase 1: hardware sweep (once, at start-up)

Measures the **board**, not the program, and produces its "prices":

| Step | How | Result |
|---|---|---|
| Peak compute rate | Loop of 8 independent integer adds (no memory, no dependencies) | `IPC_peak = instructions / cycles`, and the clock speed |
| Arithmetic wait | One chain of dependent floating-point multiply-adds | `fp_wait` = extra cycles an FP op waits for the previous result |
| Memory curve | Pointer-chasing loop (each read depends on the previous) over 4 KB, 16 KB, 64 KB, ... in every memory region | time, cycles and misses per access at each size |
| Memory tiers | New tier where time per access jumps more than 1.5x, or the memory region changes | the board's memory levels (needs no counters) |
| Miss prices | Least-squares fit over all sizes: `cycles/access = c0 + p1*m1 + p2*m2 + p3*m3` (misses per access at cache levels 1, 2, 3) | `p1, p2, p3` = extra cycles one miss at each level costs |
| Counter checks | Instruction and FP counters must advance; a miss counter must register misses; a stall counter must be ~0 on the add loop and high on the largest chase | broken counters are disabled |

A read that reaches main memory misses at every level, so its cost is `p1 + p2 + p3`: each
level's price is the extra time of going one level further.

### Phase 2: measurement

- **Run mode (Linux).** `pmurun` starts the program under `ptrace`. Linux stops every new
  thread before its first instruction; the harness attaches that thread's counters, then lets
  it run. When a thread exits, the harness reads its counters, its running time, how long it
  waited to run (`/proc/<pid>/task/<tid>/schedstat`) and how often it was switched out. The
  program does not need to know about the harness, and its thread count does not need to be known.
- **Profile one call / stage markers (all boards).** The counters and clocks are read before
  and after; on microcontrollers, every task's running time is read from FreeRTOS too.

### What is counted

| Symbol | Counter | Meaning |
|---|---|---|
| `C` | cycles | clock cycles in the thread's own code |
| `I` | instructions | instructions completed |
| `M1`, `M2`, `M3` | cache misses at level 1, 2, 3 | data not found at that level |
| `S` | memory-stall cycles | cycles the core could not proceed because it was waiting for memory |
| `LD, ST, INT, FP, SIMD, BR, MISP` | instruction mix | loads, stores, integer / floating-point / SIMD operations, branches, mispredicted branches |
| `T_wall` | wall clock | the thread's lifetime (or the stage's duration) |
| `T_run` | thread running time | time the thread was on a core (its own code + the OS working for it) |
| `T_delay` | time waiting to run | ready, but another thread had the core (Linux scheduler statistics) |

Every counter is optional; the harness uses whichever ones the board has. A core has only a
few counters (6 on the Pi 5 and Jetson); when more are requested, Linux takes turns and the
harness scales each count by the share of time it was counted.

### Overlap: visible and hidden delays

On an **out-of-order** core (Jetson Orin Nano, Raspberry Pi 5), the CPU keeps executing
independent instructions while a load waits, and keeps several misses in flight at once. Ten
overlapping misses may cost little more than one. Misses × prices then gives the **total**
memory latency, but the time the core actually lost is smaller. These cores count the cycles
in which they were stuck waiting for memory (`S`): that is the **exposed** delay. The
difference is the **hidden** delay. On an **in-order** core (ESP32-P4, GAP9) a miss stops the
core, so nearly all memory latency is exposed.

### The attribution

For each thread (or stage), in cycles:

$$C_{model} = p_1 M_1 + p_2 M_2 + p_3 M_3 \qquad \text{(total memory latency: misses one at a time)}$$

$$C_{mem} = \min(C_{model},\ S,\ C) \qquad \text{(exposed; }S\text{ only where the board counts it)}$$

$$C_{issue} = \min\left(\frac{I}{IPC_{peak}},\ C - C_{mem}\right) \qquad C_{arith} = \min\big(fp\_wait \cdot (FP + SIMD),\ C - C_{mem} - C_{issue}\big)$$

$$C_{stalled} = C - C_{mem} - C_{issue} - C_{arith}$$

Then in time, with `τ` the length of one cycle (measured in the sweep):

$$T_{user} = \tau C \qquad T_{kernel} = T_{run} - T_{user} \qquad T_{waiting} = T_{wall} - T_{run} = T_{preempted} + T_{blocked}$$

with `T_preempted = T_delay`. Each cycle share is scaled by `T_user / T_wall`.

### Categories, sub-categories and labels

| Category (same on every board) | Sub-categories (where the board can tell them apart) |
|---|---|
| **COMPUTE** | issue (instructions at full speed), waiting on results (dependent arithmetic) |
| **MEMORY** | L1 misses, L2 misses, L3 misses (the levels the board has) |
| **STALLED** | other pipeline stalls the counters do not explain |
| **KERNEL** | the operating system working for the thread |
| **WAITING** | preempted (ready, another thread had the core), blocked (sleep, I/O, locks) |

**Label** = the largest category, with its largest sub-category, for example
`MEMORY / L3 misses`, `COMPUTE / waiting on results`, `WAITING / preempted`. A board with no
L3 still has a MEMORY category, so boards can always be compared at the category level.
Without an instruction counter, running time cannot be split and is shown as `RUNNING`.

Accuracy figures:

- **explained** $= (C_{issue} + C_{arith} + C_{mem}) / C$: share of the running cycles the counted work accounts for.
- **overlap** $= C_{model} / C_{mem}$: about 1x when misses happened one at a time; above 1 when they overlapped.

### Output

For every thread (run mode) or stage (markers), four tables:

1. **what the code did**: millions of instructions, IPC, loads/stores/int/fp/simd/branches as % of instructions, mispredictions as % of branches, misses per 1000 instructions at each level;
2. **categories**: COMPUTE, MEMORY, STALLED, KERNEL, WAITING as % of wall time, and the label;
3. **sub-categories**: issue, arith, L1/L2/L3, preempted, blocked, explained, overlap;
4. **delays** (ms): memory total / exposed / hidden / hidden %, preempted, blocked, and how often the thread was switched out by force or gave up the core.

Run mode adds an `ALL` row (the program's threads added up). Stage markers add, for the
thread that called `pmu_init`, **who used the core** during each stage: every thread or task
as a share of the time, plus other programs/OS and idle where the core's idle time is known.

---

## Counters used on the four target boards

| Board | Core | Cycles, instructions | Miss counters | Memory-stall counter | Instruction mix | Waiting split |
|---|---|---|---|---|---|---|
| Jetson Orin Nano | Cortex-A78AE (out-of-order) | `perf` generic events | L1 `0x03`, L2 `0x17`, L3 `0x2A` | `STALL_BACKEND_MEM` `0x4005` | `LD_SPEC 0x70`, `ST_SPEC 0x71`, `DP_SPEC 0x73`, `VFP_SPEC 0x75`, `ASE_SPEC 0x74`, `BR_RETIRED 0x21`, `BR_MIS_PRED_RETIRED 0x22` | preempted / blocked |
| Raspberry Pi 5 | Cortex-A76 (out-of-order) | `perf` generic events | L1 `0x03`, L2 `0x17`, L3 `0x2A` | `STALL_BACKEND` `0x24` (all back-end stalls; non-memory stalls are excluded by the `min` with `C_model`) | same as Jetson | preempted / blocked |
| ESP32-P4 | RISC-V HP core (in-order) | cycle counter, `rdinstret` | ESP-IDF cache counters: L1 and L2 (PSRAM/flash) line fills | not needed (in-order) | not available | waiting (FreeRTOS per-task run time) |
| GAP9 | RISC-V fabric controller (in-order) | `pi_perf` cycles + instructions | none on the main core | not needed (in-order) | not available | single thread |

On ARM boards the harness opens an event only if the kernel lists it under
`/sys/bus/event_source/devices/armv8*/events/`, which shows exactly the events the core
implements. Other Linux machines use the generic `perf` events.

On the ESP32-P4 the counters belong to the core, not to a task, so a profile covers
everything the core ran during the call; each task's share of the core comes from FreeRTOS.

---

## Why it generalizes

- **Any code:** the program is never inspected or changed. In run mode the harness finds
  every thread itself; expected time comes only from work the hardware counted.
- **Any hardware:** `pmu.c` has a small platform layer (counters, clocks, threads, memory
  regions), chosen automatically at compile time, and a method that is identical everywhere.
- **Same categories everywhere, detail where available:** COMPUTE / MEMORY / STALLED /
  KERNEL / WAITING on every board; sub-categories only where the board's counters allow.
- **In-order and out-of-order cores:** measured stall cycles are used where they exist;
  misses × prices where misses do not overlap; hidden delay is reported either way.
- **Every counter is checked at start-up:** missing, not advancing, or not behaving as
  expected → disabled. The output says less rather than something wrong.
- **Prices are measured** on the board, never taken from a datasheet.
- **Memory probed by kind and size:** caches by working-set size on Linux; TCM / SRAM / PSRAM / L2 measured separately on microcontrollers.
- **Tiers need no counters**, so even a counter-less board shows its memory structure.
- 32-bit counter wrap-around, counter turn-taking and small memories are handled.

---

## Validation workloads

**`workload.c`: an ordinary multi-threaded program with no harness calls.** The harness has
to find its threads and explain them. Each thread has one known personality:

| Thread | What it does | Expected label |
|---|---|---|
| `compute` | floating-point arithmetic in registers | COMPUTE |
| `rand-mem` | reads a large buffer (64 MB on Linux, 4 MB PSRAM on ESP32-P4) at random positions | MEMORY / last level; high hidden % on out-of-order cores |
| `seq-mem` | reads the same buffer in order | MEMORY, with much of its latency hidden by prefetching |
| `sleeper` | sleeps 2 ms, then a little work, 50 times | WAITING / blocked |
| `shared1..3` | the compute work, all three forced onto one core | WAITING / preempted (about 2/3 each) |
| main thread | starts the threads and waits | WAITING / blocked |

**`demo.c`: the stage-marker test** (`wait`, `compute`, `seq-mem`, `rand-mem`, `shared-cpu`),
which also exercises per-thread stage tables and the "who used the core" table.

---

## Files

| File | Purpose |
|---|---|
| `pmu.h` | the API |
| `pmu.c` | platform layer + hardware sweep + measurement + attribution + run mode |
| `pmurun.c` | the `pmurun` launcher (Linux) |
| `workload.c` | validation program with no harness calls |
| `mcu_main.c` | microcontroller entry: `pmu_profile("workload", ...)` |
| `demo.c` | stage-marker validation |
| `esp/` | ESP-IDF project (ESP32-P4) |
| `gap/` | GAP SDK project (GAP9) |
| `CODE_WALKTHROUGH.md` | what each part of the code does |


---

## Build and run

**Linux (Raspberry Pi 5, Jetson Orin Nano, any Linux machine)**

```bash
sudo sysctl kernel.perf_event_paranoid=2          # allow reading hardware counters
gcc -O2 pmu.c pmurun.c -o pmurun -pthread          # the launcher
gcc -O2 workload.c -o workload -pthread            # the validation program (or any program)
./pmurun ./workload                                # profile it
./pmurun python3 my_script.py                      # ... or anything else
```

For the most accurate times, fix the clock speed while measuring:
`sudo cpupower frequency-set -g performance` (or set the `performance` governor).

Stage markers: `gcc -O2 pmu.c demo.c -o demo -pthread && ./demo`.

**ESP32-P4** (ESP-IDF installed; a recent version provides the cache counters). Builds
`workload.c` + `mcu_main.c`; replace `workload_main` with any code to profile it.

```bash
cd esp && idf.py set-target esp32p4 && idf.py build flash monitor
```

**GAP9** (GAP SDK installed)

```bash
cd gap && cmake -B build && cmake --build build --target run
```

Status: the Linux build compiles (x86 and ARM) and runs, including run mode; the ESP32-P4 and
GAP9 builds are written against the documented ESP-IDF and GAP SDK APIs. Validation on the
target boards is the next step.
