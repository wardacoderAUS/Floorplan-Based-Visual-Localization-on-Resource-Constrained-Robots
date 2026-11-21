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
  baseline/            F3Loc reproduction (done)
  second-baseline/     second localization method (done)
  shrinking/           smaller, quantized model for the microcontrollers (in progress)
```
