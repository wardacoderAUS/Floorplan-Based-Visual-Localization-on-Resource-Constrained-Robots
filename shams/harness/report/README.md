# Report

The written report on the harness: what problem it solves, how the method works, how it
generalizes to any program and any board, the validation program, the first experiment and
its analysis, the evaluation plan and the limitations.

## What is in this folder

| File | What it is |
|---|---|
| `Counter_Based_Harness_Report.pdf` | the report (8 pages, two-column) |
| `report.tex` | its LaTeX source, so it can be edited and rebuilt |

## Contents of the report

| Section | Covers |
|---|---|
| Abstract | the problem, the approach, the targets, the first result |
| 1. Introduction | why a run time is not enough; the gap in existing tools; contributions |
| 2. Background | hardware counters; memory hierarchy; in-order vs out-of-order cores (hidden latency); running, preempted and blocked time |
| 3. Harness design | requirements; architecture (figure); counters; hardware sweep (with code); finding every thread; attribution equations; categories and sub-categories; visible and hidden delays; output; platform layer for each board |
| 4. Generalisation | why it works for any code and any hardware: eight design decisions; detail available per board |
| 5. Validation program | design principles; the threads, their code and their expected labels |
| 6. Preliminary test | the laptop experiment: setup, code used, results (tables and figure), analysis |
| 7. Evaluation plan | what will be measured on the four target boards |
| 8. Limitations | what the method cannot yet do |
| 9. Conclusion | |

## How to rebuild the PDF

With a LaTeX installation (TeX Live or MacTeX):

```bash
pdflatex report.tex && pdflatex report.tex      # twice, for references
```

The source uses only standard packages (`tikz`, `pgfplots`, `listings`, `tabularx`, …) and
needs no other files.

## Where the numbers come from

Every result in Section 6 comes from `../experiments/01-laptop-macbook-air-docker/raw/output.txt`;
the means and ranges are in `../experiments/01-laptop-macbook-air-docker/summary.csv`.
