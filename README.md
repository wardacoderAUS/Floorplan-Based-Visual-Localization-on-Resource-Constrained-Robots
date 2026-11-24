# Floorplan-Based Visual Localization on Resource-Constrained Robots

A visual localization pipeline that finds a camera's position by matching its images against a building floorplan, characterized and deployed on four embedded platforms: ESP32-P4, GAP9, Raspberry Pi 5 and Jetson Orin Nano.

Each student’s experiments, research activities, and milestones are organized in their respective folders.

## Structure

```
env/                   pinned software versions
shams/
  harness/             counter-based performance attribution harness (done; first experiment done)
    literature-review/   review of existing timing tools and the gap they leave
    pmu_harness/         code: profiles any program on all four boards, with walkthrough
    report/              report (PDF and LaTeX source)
    experiments/         results of every run, with analysis
    reproducibility/     scripts and procedure to repeat every experiment
warda/
  baseline/            F3Loc reproduction (done; all 26 paper numbers within 0.1 pt)
    E1_F3Loc_Gibson_Colab.ipynb   full run on Colab, with outputs
    f3loc-e1-logging.patch        our changes to F3Loc's test scripts (per-frame logs, dropped-frame check)
    compare_e1.py                 our numbers vs. the paper, with OK / CHECK / STOP verdicts
    results/                      results table, per-frame CSVs, run details
  second-baseline/     second localization method: UnLoc (done; all 10 Table 1 numbers match the paper)
    E2_UnLoc_Gibson_Colab.ipynb   full run on Colab, with outputs
    unloc-e2-logging.patch        our changes to UnLoc's test script (per-walk logs, checks)
    compare_e2.py                 our numbers vs. the paper, with OK / CHECK / STOP verdicts
    results/                      results table and run details
  shrinking/           smaller, quantized model for the microcontrollers (in progress)
```
