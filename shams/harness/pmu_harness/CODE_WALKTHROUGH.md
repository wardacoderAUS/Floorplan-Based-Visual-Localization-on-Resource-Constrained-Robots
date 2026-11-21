# Code walkthrough

This file explains every part of the code, in the order it runs. See `README.md` for the
goal and the equations.

---

## `pmu.h`: the API

| Function | Purpose |
|---|---|
| `pmu_init()` | Run once. Finds the board's counters and runs the hardware sweep. The calling thread is registered as `main`. |
| `pmu_run(argv)` | Linux: run a program unchanged and profile every thread it creates (used by `pmurun`). |
| `pmu_profile(name, fn)` | Any board: profile one call of `fn` (the way to profile any code on a microcontroller). |
| `pmu_begin(s)` / `pmu_end(s)` | Optional stage markers (0–15) inside a program. |
| `pmu_report(names, n)` | Print the tables for the stage markers. |
| `pmu_thread(name)`, `pmu_thread_done()` | Register / unregister a thread for stage markers (optional: `pmu_begin` registers unnamed threads). |
| `pmu_spawn(n, name, fn)`, `pmu_join()` | Test helper: start `n` threads/tasks on the current core, named `name1..nameN`; wait for them. |
| `pmu_sleep_us()`, `pmu_big_buffer()` | Test helpers: portable sleep; a buffer in the slowest memory. |

---

## `pmu.c`

Two parts:

- **Part A, platform layer:** the only code that knows which board it is on.
- **Part B, the method:** identical on every board. Run mode is at the end (Linux only).

### Top of the file: shared definitions

| Code | Purpose |
|---|---|
| `enum { CYC, INS, M1, M2, M3, STL, LD, ST, IOP, FOP, VOP, BR, BMP, NC }` | The counters: cycles, instructions, cache misses at levels 1–3, memory-stall cycles, then the instruction mix: loads, stores, integer / floating-point / SIMD ops, branches, mispredicted branches. |
| `have[NC]` | Whether this board has each counter. Checked everywhere, so missing counters are skipped. |
| `have_cpu_clock` | Whether the board can tell running from waiting. |
| `snap_t`, `delta_t` | A photo of all counters and clocks; the difference of two photos. |
| `core_t` | A photo of who used the core: each thread/task's running time so far, and the core's idle time. |
| `thr_t`, `threads[]`, `me` | Everything kept for one thread using stage markers; `me` is the calling thread's slot (thread-local). |

### Part A: platform layer

The compiler keeps one section: `ESP_PLATFORM` → ESP32-P4; `pmsis.h` present → GAP9;
`__linux__` → Linux. Each section provides the same items:

| Item | Linux | ESP32-P4 | GAP9 |
|---|---|---|---|
| `PMU_STEPS` (reads per measurement) | 2,000,000 | 200,000 | 200,000 |
| `WRAP32` (32-bit counters?) | no | yes | yes |
| `regions[]` | one "RAM" up to 64 MB (caches found by size) | TCM, internal SRAM, PSRAM | L2 |
| `now_ns()` wall clock | `CLOCK_MONOTONIC_RAW` | `esp_timer_get_time` | `pi_time_get_us` |
| `cpu_ns()` running time | `CLOCK_THREAD_CPUTIME_ID` | FreeRTOS per-task run time (`vTaskGetInfo`, after `taskYIELD()` so the current slice is included) | = wall |
| `counters_open()` / `counters_read()` | `perf_event_open`, see below | cycle count, `rdinstret`, ESP-IDF cache counters | `pi_perf` cycles + instructions |
| `thread_open()` / `thread_close()` | open/close the same events for another thread | nothing (counters are per core) | nothing |
| `core_read()` | `/proc/self/task/*/schedstat` + this core's `cpuidle` time | `uxTaskGetSystemState`: every task on this core | nothing |
| `pmu_spawn()` | threads pinned to this core | tasks pinned to this core, same priority | returns 0 |

**Linux counters.**

- `perf_open_tid(type, config, tid, kernel)` asks the kernel for one counter on one thread
  (`tid` 0 = the caller), counting only the program's own code unless `kernel` is set.
- It also asks for the times the counter was *enabled* and *running*. The Pi 5 and Jetson
  cores have 6 counters; when more are open, Linux takes turns, and `perf_value()` scales
  each count by `enabled / running`.
- `counters_open` walks a table of attempts and fills each slot with the first that works:

  | Slot | ARM event | Number |
  |---|---|---|
  | `M1`, `M2`, `M3` | L1D / L2D / L3D_CACHE_REFILL | `0x03`, `0x17`, `0x2A` |
  | `STL` | STALL_BACKEND_MEM (A78AE), else STALL_BACKEND (A76) | `0x4005`, `0x24` |
  | `LD`, `ST` | LD_SPEC, ST_SPEC | `0x70`, `0x71` |
  | `IOP`, `FOP`, `VOP` | DP_SPEC, VFP_SPEC, ASE_SPEC | `0x73`, `0x75`, `0x74` |
  | `BR`, `BMP` | BR_RETIRED, BR_MIS_PRED_RETIRED (or BR_MIS_PRED) | `0x21`, `0x22` (`0x10`) |

  `arm_has(name)` first checks that the kernel lists the event under
  `/sys/bus/event_source/devices/armv8*/events/`: the kernel lists only events the core
  implements, so a missing event is never opened. Other Linux machines use generic events.
- The chosen events are remembered in `ev[]`, so other threads can open the same ones.

**ESP32-P4 counters.** Cycles: `esp_cpu_get_cycle_count()`. Instructions: `rdinstret`.
Misses: line fills of the L1 cache (`M1`) and of the L2 PSRAM/flash cache (`M2`) from
`esp_cache_cnt.h`. No stall counter is needed: the core is in-order.

**GAP9 counters.** The chip counts cycles plus one other event; we configure instructions.

An unknown platform stops compilation with an `#error` instead of producing wrong results.

### Part B, step 0: snapshots and threads

| Code | Purpose |
|---|---|
| `take(t, s)` | Photo of thread `t`'s counters and both clocks. |
| `sub(a, b)` | After − before. 32-bit counters are subtracted in 32-bit arithmetic, so a wrap-around still gives the right difference. `run` = cycles if counted, else CPU nanoseconds. |
| `pmu_thread(name)` | Gives the calling thread a slot in `threads[]` (reusing a free slot with the same name, so a thread started every frame keeps adding to one total), opens its counters, stores the slot in `me`. |

### Step 1: calibration loops

| Code | Purpose |
|---|---|
| `busy_loop()` | 8 independent running sums, no memory → the core's top instruction rate. The empty `asm volatile` emits no instructions; it stops the compiler from collapsing or vectorizing the loop. |
| `fp_chain()` | One floating-point multiply-add chain: each op needs the previous result → how long a dependent FP op waits (`fp_wait`). The `asm` keeps the value in an FP register. |
| `make_chain()` | Arranges a buffer as one random cycle through all its 64-byte lines (Sattolo's shuffle). |
| `chase()` | `x = p[x*16]`: each read gives the next address, so reads cannot overlap. |
| `fit_costs()` | Least squares: `cycles/access = c0 + p1·m1 + p2·m2 + p3·m3` over all sweep points, by the normal equations and Gaussian elimination. Negative prices are set to 0. |

### Step 2: `pmu_init()`, the hardware sweep

1. Register the caller as `main`; `counters_open()`.
2. `busy_loop` once unmeasured (clock settles), once measured → `peak_ipc`, `ns_per_cycle`.
   If the instruction counter barely moved, disable it. Record `stall_busy`.
3. If an FP counter exists: `fp_chain` → `fp_wait` = cycles per FP op − `1/peak_ipc`.
4. For each memory region: largest buffer that fits; for sizes 4 KB, 16 KB, …: build the
   chain, warm up, chase between two photos; store time, cycles and misses per access.
5. Checks: a miss counter below 0.05 misses per access everywhere is disabled; a stall
   counter is kept only if below 0.2 per cycle on the add loop and above 0.5 on the largest chase.
6. `fit_costs()`; print the counters found, the prices and the memory tiers.

### Step 3: measurement

- **Stage markers.** `pmu_begin` stores a photo in the caller's slot; `pmu_end` subtracts and
  adds to that stage's totals. For `main`, `core_read` is called before the first photo and
  after the second, so reading the thread list is never part of the stage.
- **`pmu_profile(name, fn)`**: `pmu_begin(0)`, `fn()`, `pmu_end(0)`, `pmu_report`.

### Step 4: explanation

| Function | What it does |
|---|---|
| `explain(c, cycles)` | In cycles: `model` = Σ misses × prices (total memory latency); `mem` = min(model, measured stalls, cycles) (exposed); `lv[]` = `mem` split by level in proportion to cost; `compute` = I / peak_ipc (issue); `arith` = (FP + SIMD ops) × `fp_wait`, at most what is left; `other` = the rest; `overlap` = model / mem; `explained` = (compute + arith + mem) / cycles. |
| `row_t` | One row: a name, counts, running time, time waiting to run (−1 = unknown), wall time, switch counts. |
| `split_time(row)` | Fractions of wall time: `user` = cycles × `ns_per_cycle`; KERNEL = run − user; WAITING = wall − run, split into preempted (time waiting to run) and blocked; COMPUTE = issue + arith, MEMORY = exposed memory, STALLED = other, each scaled by user / wall. |
| `label_of()` | The largest category, with its largest sub-category: `MEMORY / L3 misses`, `COMPUTE / waiting on results`, `WAITING / preempted`, … |
| `print_tables()` | The four tables for any list of rows: what the code did; categories + label; sub-categories + explained + overlap; delays (memory total / exposed / hidden, preempted, blocked, switch counts). |
| `report_thread()` / `pmu_report()` | Stage markers: one row per stage (per run), then "who used the core" for `main`. |

### Run mode: `pmu_run(argv)` (Linux)

1. `fork()`. The child calls `ptrace(PTRACE_TRACEME)`, stops itself, then `execvp`s the program.
2. The parent sets `PTRACE_O_TRACECLONE | TRACEEXEC | TRACEEXIT | EXITKILL`, then loops on `waitpid(-1, …, __WALL)`:
   - **exec**: the program starts → `trk_add(pid)` (main thread's counters).
   - **clone**: a new thread exists and is still stopped → `trk_add(new tid)` opens its counters (`perf_open_tid`) and a software task-clock (its running time in ns) before it runs.
   - **exit**: a thread is about to end → `trk_end` reads its counters, its task-clock, `/proc/<pid>/task/<tid>/schedstat` (time run, time waiting to run), and `status` (name, forced and voluntary switch counts).
   - The program's own signals are passed on unchanged.
3. `report_run()` builds one `row_t` per thread plus an `ALL` row and calls `print_tables`.

If the kernel does not record time waiting to run, `delay` is −1 and WAITING is not split.

---

## `pmurun.c`

`pmu_init()` (hardware sweep), then `pmu_run(argv + 1)`.

## `workload.c`: validation program with no harness calls

| Thread | Code | Expected |
|---|---|---|
| `compute` | 4 independent float chains `a = a*0.9999 + 1e-4` | COMPUTE |
| `rand-mem` | `buf[x % words]`, `x` from a linear congruential generator | MEMORY / last level |
| `seq-mem` | sum every word of the buffer, 8 times | MEMORY (latency largely hidden) |
| `sleeper` | 50 × (sleep 2 ms + small loop) | WAITING / blocked |
| `shared1..3` | `compute`, all pinned to one core | WAITING / preempted |

The only platform-specific part is starting threads: pthreads (Linux), FreeRTOS tasks on one
core (ESP32-P4), or one after another (GAP9). `SCALE` reduces the work on microcontrollers.

## `mcu_main.c`

`app_main` (ESP-IDF) or `pmsis_kickoff` (GAP9): `pmu_init()`, then
`pmu_profile("workload", …)`. Replace `workload_main` with any code to profile it.

## `demo.c`: stage-marker validation

Stages `wait`, `compute`, `seq-mem`, `rand-mem`, `shared-cpu` (with two competitor threads
started by `pmu_spawn`, each marking its own `comp-work` stage), 50 frames each.
