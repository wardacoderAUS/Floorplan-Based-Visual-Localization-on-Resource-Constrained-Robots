# Literature review

The review that motivated the harness.

## What is in this folder

| File | What it is |
|---|---|
| `Timing_Harness_Review_and_Design.pdf` | *Per-Stage Timing Harnesses for Real-Time Visual Localization on Heterogeneous Embedded Platforms: A Review, Gap Analysis, and Proposed Design* (11 pages) |

## What it covers

- **Purpose and principles of a timing harness:** why ad hoc timing is not enough (means
  hide the tail, run conditions are not recorded, printing disturbs the timing, accuracy is
  assumed rather than shown), and measurement versus worst-case analysis.
- **History** of timing and measurement tools, from synthetic benchmarks to tracing
  frameworks, real-time latency tests, machine-learning benchmark suites and SLAM benchmarks.
- **The four localization papers** that define the target workload (LASER, F3Loc, UnLoc,
  eSLAM): all report cost as stage averages on one platform, without tail latency or memory
  figures, and none of the floorplan methods has been timed on embedded hardware.
- **Ten tools and tool families** compared against five requirements: per-stage and
  per-frame resolution, microcontroller support, one output format across platforms,
  demonstrated accuracy, and automatically recorded run conditions.

## Why it matters for the harness

The review found that **no existing tool meets all five requirements**, and none verifies its
own accuracy. The harness in `../pmu_harness` answers this gap and goes further: it explains
*why* each part of a program takes its time (compute, memory by level, other stalls, the
operating system, waiting), the same way on every board, and states how much of the time it
can explain. The report in `../report` describes how.
