# Floorplan-Based Visual Localization on Resource-Constrained Robots

American University of Sharjah · Supervisor: Dr. Mohamed Hassan

A visual localization pipeline that finds a camera's position by matching its images against a building floorplan, characterized and deployed on four embedded platforms: ESP32-P4, GAP9, Raspberry Pi 5 and Jetson Orin Nano.

## Team

| Warda | Shams |
|---|---|
| Perception | Platform |
| Accuracy of the localization, from the baselines to a model small enough for a microcontroller | Everything measured, from the timing harness to the ports on each platform |

The experiments and milestones of the research plan are organized in each student's folder.

## Structure

```
env/                   pinned software versions
shams/
  timing-harness/      timing harness
warda/
  baseline/            F3Loc reproduction (done: matches the paper within 0.1 points)
  second-baseline/     second localization method
  shrinking/           smaller, quantized model for the microcontrollers
```
