# Code walkthrough

This file explains every part of the code, in the order it runs. See `README.md` for the
goal and the equations.

---

## `pmu.h`: what a program sees

| Function | Purpose |
|---|---|
| `pmu_init()` | Run once. Finds the board's counters and runs the hardware sweep. |
| `pmu_begin(s)` / `pmu_end(s)` | Placed around stage `s` (0–15) of the program being measured. |
| `pmu_report(names, n)` | Print the attribution table. |
| `pmu_sleep_us()`, `pmu_big_buffer()` | Helpers so the test program is portable (sleep; buffer in the slowest memory). |

---

## `pmu.c`

The file has two parts:

- **Part A, platform layer:** the only code that knows which board it is on.
- **Part B, the method:** identical on every board.

### Top of the file: shared definitions

| Code | Purpose |
|---|---|
| `enum { CYC, INS, NEAR, FAR, NC }` | Names for the four counters: cycles, instructions, first-level misses, slowest-memory misses. |
| `have[NC]` | Whether this board has each counter. Checked everywhere, so missing counters are skipped. |
| `have_cpu_clock` | Whether the board can tell running from waiting. |
| `region_t` | One kind of memory: name, how to allocate it, largest size to test. |

### Part A: platform layer

The compiler keeps one section: `ESP_PLATFORM` → ESP32; `pmsis.h` present → GAP9;
`__linux__` → Linux. Each section provides the same items.

| Item | Linux | ESP32 / ESP32-P4 | GAP9 |
|---|---|---|---|
| `PMU_STEPS` (reads per measurement) | 2,000,000 | 200,000 | 200,000 |
| `WRAP32` (32-bit counters?) | no | yes | yes |
| `regions[]` | one "RAM" up to 64 MB (caches found by size) | TCM, internal SRAM, PSRAM | L2 |
| `region_alloc` / `region_free` | `aligned_alloc` (64-byte lines) | `heap_caps_aligned_alloc` per memory type | `pi_l2_malloc` |
| `now_ns()` wall clock | `CLOCK_MONOTONIC_RAW` (never adjusted) | `esp_timer_get_time` | `pi_time_get_us` |
| `cpu_ns()` running time | `CLOCK_THREAD_CPUTIME_ID` | not available (= wall) | not available (= wall) |
| `pmu_sleep_us()` | `usleep` | `vTaskDelay` | `pi_time_wait_us` |
| `counters_open()` / `counters_read()` | `perf_event_open`, see below | cycle count, `rdinstret`, ESP-IDF cache counters | `pi_perf` cycles + instructions |

**Linux counters.** `perf_open(type, config)` asks the kernel for one counter, counting only
this thread's own code (`exclude_kernel`). Each counter is then read like a file.
`counters_open` walks a list of attempts, generic events first and chip-specific backups after:

- cycles, instructions
- L1 data read misses → `NEAR`
- last-level read misses → `FAR`. Backups:
  - ARM raw event `0x17` (L2 refill, the last level on the Jetson Nano's Cortex-A57)
  - the generic `cache-misses` event on x86

A counter that opens sets `have[k] = 1`.

**ESP32-P4 counters.**

- Cycles: `esp_cpu_get_cycle_count()`.
- Instructions: the RISC-V `rdinstret` instruction.
- Misses: if ESP-IDF provides `esp_cache_cnt.h`, the line fills (true misses) of each cache unit belonging to this core are added up, level 1 into `NEAR` and level 2 (PSRAM/flash cache) into `FAR`.

**GAP9 counters.** The chip counts cycles plus one other event, so we configure cycles + instructions.

An unknown platform stops compilation with an `#error` instead of producing wrong results.

### Part B, step 0: snapshots

| Code | Purpose |
|---|---|
| `snap_t` | A "photo" of one instant: all counter values + both clocks. |
| `take()` | Takes the photo. |
| `sub()` | After − before for every counter. 32-bit counters are subtracted in 32-bit arithmetic, so a wrap-around still gives the right difference. `run` = cycles if counted, else CPU nanoseconds. |

### Step 1: the three calibration loops

| Code | Purpose |
|---|---|
| `busy_loop()` | 8 independent running sums, no memory → the core's top instruction rate. The empty `asm volatile` emits no instructions; it stops the compiler from collapsing or vectorizing the loop. |
| `make_chain()` | Arranges a buffer as one random cycle through all its 64-byte lines (Sattolo's shuffle), so following it never stays in a small, cached loop. |
| `chase()` | `x = p[x*16]`: each read gives the next address, so the CPU cannot overlap reads. Every step pays the full latency of the memory holding the buffer. |

### Step 1b: `fit_costs()`, miss prices

Each sweep point has: cycles per access, near misses per access, far misses per access.
The model `cycles/access = c0 + p_near·near + p_far·far` is solved by least squares:

1. Only counters that exist become unknowns.
2. Build the normal equations (a 3×3 system).
3. Solve them by Gaussian elimination.

Negative results are physically impossible and set to 0. Fitting means we never need to know
which physical cache a counter refers to.

### Step 2: `pmu_init()`, the hardware sweep

1. `counters_open()`.
2. Run `busy_loop` once unmeasured so the clock speed settles, then once measured →
   `peak_ipc = instructions / cycles`.
3. **Check:** if the instruction counter barely moved during millions of instructions, disable it.
4. For each memory region:
   - Allocate the largest buffer that fits, halving the size until it does.
   - For sizes 4 KB, 16 KB, 64 KB, …:
     - build the chain
     - warm up
     - chase while taking before/after photos
     - store region, size, ns per access, cycles per access, misses per access
5. **Check:** a miss counter that never reached 0.05 misses per access, even on the largest
   buffer, is disabled.
6. `fit_costs()`.
7. Print the counters found, the prices, and the memory tiers. A new tier starts where the region
   changes or time per access jumps more than 1.5×.

`pmu_big_buffer()` returns a buffer of the largest tested size in the slowest region (used by the demo).

### Step 3: `pmu_begin()` / `pmu_end()`

`begin` stores a photo. `end` takes another, subtracts, and adds the difference to that stage's
totals, counting the runs.

### Step 4: `pmu_report()`, expected vs measured

For each stage:

| Line | Meaning |
|---|---|
| `compute = I / peak_ipc` (capped at `run`) | **Expected compute cycles**: what the counted instructions need at peak rate. |
| `lost = run - compute` | Cycles the core was held up. |
| `mem_near = M_near·p_near`, `mem_far = M_far·p_far` | **Expected memory cycles** from the counted misses. |
| `explained = (compute + mem_near + mem_far) / run` | Agreement between expected and measured (accuracy check). |
| cap `mem_near + mem_far` at `lost` | Overlapping misses cannot take more time than was lost. |
| `other = lost - mem_near - mem_far` | Held-up time memory does not explain. |
| `r = cpu / wall` | Fraction of real time the stage was running (capped at 1, since the two clocks are read a few ns apart). |
| shares | compute, near, far, other as `r·x/run`; waiting = `1 - r`. |
| label | The largest share; MEMORY names the memory level with the larger cost. |

Without an instruction counter, the running time cannot be split, so the `other` column is
printed as `running` and the label is `RUNNING`.

---

## `demo.c`: the validation workload

| Stage | Code | Expected |
|---|---|---|
| `wait_2ms` | `pmu_sleep_us(2000)` | WAIT |
| `arithmetic` | 4 independent float chains `a = a*0.9999 + 1e-4` | COMPUTE |
| `read_in_order` | sum every word of the big buffer | MEMORY |
| `read_random` | sum words at pseudo-random positions | MEMORY |

`demo_main`:

1. Calls `pmu_init`.
2. Gets the big buffer and fills it.
3. Runs 50 frames of the 4 stages, each wrapped in `pmu_begin` / `pmu_end`.
4. Prints the table and the expected labels.

The last lines adapt the entry point: `app_main` for ESP-IDF, `pmsis_kickoff` for GAP9,
`main` for Linux.
