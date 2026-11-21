# Shams

This part of the project: measuring and explaining how the localization pipeline, and any
other program, runs on the four target boards (ESP32-P4, GAP9, Raspberry Pi 5, Jetson Orin
Nano).

## Contents

| Folder | What is inside |
|---|---|
| [`harness/`](harness/) | the counter-based performance attribution harness |
| ├─ [`literature-review/`](harness/literature-review/) | review of existing timing tools and the gap they leave |
| ├─ [`pmu_harness/`](harness/pmu_harness/) | the code, with method description and line-by-line walkthrough |
| ├─ [`report/`](harness/report/) | the report (PDF and LaTeX source) |
| ├─ [`experiments/`](harness/experiments/) | every experiment: raw output, conditions, results, analysis |
| └─ [`reproducibility/`](harness/reproducibility/) | scripts and procedure to repeat every experiment |

Start with [`harness/README.md`](harness/README.md).
