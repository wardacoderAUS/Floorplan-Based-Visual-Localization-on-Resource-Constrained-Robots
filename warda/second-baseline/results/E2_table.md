# E2 results: UnLoc released checkpoint vs. paper (Gibson(t))

```
session_start_utc: 2026-10-30T12:53:00+00:00
platform: Google Colab
unloc_commit: 3c7a301aa08049f6b1bb86f110e8e6e675e2300b
depth_anything_v2_commit: a561b849ebae10a6f5ef49e26c83cbbcd36c71bf
python: 3.13.16
torch: 2.11.0+cu130 (cuda 13.0)
torchvision: 0.26.0+cu130  lightning: 2.5.6  numpy: 2.1.3  scipy: 1.16.3  opencv: 5.0.0
xformers: not installed (Depth Anything uses plain PyTorch attention)
gpu: colab's T4 or A100
ckpt unloc_gibson_vitl.ckpt: sha256 48883fbdbe2a3f1f0b4f513038ba3cc75f780656d4186fa55604906323d4aac4
ckpt depth_anything_v2_metric_hypersim_vitl.pth: sha256 6f82ff2bc543ac02ddff4aa31fa363676a8305dd3ccf04e80e2af115a044cb6d
zip gibson_t: 16429264687 bytes, 51609 files
zip desdf: 29364839 bytes, 19 files
gibson_t: 9 test scenes, 4061 poses, walks per length {100: 37, 50: 78, 35: 111, 20: 200, 15: 268}
```

## Sequential localization, SR@1m (paper Table 1)

| T | N ours / paper | row | ours (%) | ±95% | paper (%) | delta | verdict |
|---|---|---|---|---|---|---|---|
| 100 | 37 / 37 | UnLoc w/o post-processing | 97.3 | 5.2 | 97.3 | -0.0 | OK |
| 100 | 37 / 37 | UnLoc | 97.3 | 5.2 | 97.3 | -0.0 | OK |
| 50 | 78 / 78 | UnLoc w/o post-processing | 92.3 | 5.9 | 92.3 | +0.0 | OK |
| 50 | 78 / 78 | UnLoc | 94.9 | 4.9 | 94.9 | -0.0 | OK |
| 35 | 111 / 111 | UnLoc w/o post-processing | 88.3 | 6.0 | 88.3 | -0.0 | OK |
| 35 | 111 / 111 | UnLoc | 92.8 | 4.8 | 92.8 | -0.0 | OK |
| 20 | 200 / 200 | UnLoc w/o post-processing | 70.5 | 6.3 | 70.5 | +0.0 | OK |
| 20 | 200 / 200 | UnLoc | 86.5 | 4.7 | 86.5 | +0.0 | OK |
| 15 | 268 / 268 | UnLoc w/o post-processing | 65.3 | 5.7 | 65.3 | -0.0 | OK |
| 15 | 268 / 268 | UnLoc | 81.3 | 4.7 | 81.3 | +0.0 | OK |

Verdict: OK = within 0.1 pt; CHECK = off by at most one trajectory (100/N pt) or 2 pt; STOP = more, or a different number of trajectories.

## RMSE at T=100 (m, last 10 frames)

| row | RMSE successful ours / paper | RMSE all ours / paper | verdict |
|---|---|---|---|
| UnLoc w/o post-processing | 0.16 / 0.16 | 0.28 / 0.28 | OK |
| UnLoc | 0.20 / 0.16 | 0.33 / 0.28 | CHECK |

RMSE is the script's own: per trajectory over the last 10 frames, then averaged. OK within 0.01 m, CHECK within 0.05 m.

## Single-frame localization, observation only (paper Table 6, Gibson(t))

From the T=100 run (3700 frames). The paper does not state which trajectory length its Table 6 used, so these verdicts are a cross-check, not part of the main criterion.

| metric | ours | paper | delta | verdict |
|---|---|---|---|---|
| R@0.1m (%) | 19.7 | 19.7 | +0.0 | OK |
| R@0.5m (%) | 61.1 | 61.1 | +0.0 | OK |
| R@1m (%) | 64.7 | 64.7 | +0.0 | OK |
| R@1m30deg (%) | 63.8 | 63.8 | -0.0 | OK |
| R@2m (%) | 68.7 | 68.7 | +0.0 | OK |
| R@5m (%) | 84.4 | 84.4 | -0.0 | OK |
| R@10m (%) | 97.6 | 97.6 | +0.0 | OK |
| depth_mae_m | 0.25 | 0.25 | +0.00 | OK |
| depth_cos_sim | 0.981 | 0.981 | +0.000 | OK |

## F3Loc and UnLoc on the same gibson_t walks (SR@1m %)

| method | source | T=100 | T=50 | T=35 | T=20 | T=15 |
|---|---|---|---|---|---|---|
| F3Loc mono | ours (E1) | 89.2 | – | – | – | – |
| F3Loc fusion | ours (E1) | 94.6 | – | – | – | – |
| F3Loc mono | UnLoc paper | 89.2 | 70.5 | 55.9 | 34.0 | 28.4 |
| F3Loc fusion | UnLoc paper | 94.6 | 94.6 | 69.4 | 46.0 | 41.8 |
| UnLoc w/o post-processing | ours (E2) | 97.3 | 92.3 | 88.3 | 70.5 | 65.3 |
| UnLoc | ours (E2) | 97.3 | 94.9 | 92.8 | 86.5 | 81.3 |

## Per-step timing (context only; ours on Colab, paper on a Quadro RTX 6000)

Lab rule: distributions only. The paper column is its reported mean, for orientation.

| T | stage | N steps | p50 (ms) | p99 (ms) | p99.9 (ms) | max (ms) | paper mean (ms) |
|---|---|---|---|---|---|---|---|
| 100 | feature | 3700 | 595.66 | 623.00 | 638.67 | 2895.27 | 174 |
| 100 | matching | 3700 | 5.34 | 12.24 | 12.67 | 208.44 | 4 |
| 100 | iteration | 3700 | 605.24 | 631.83 | 646.04 | 3334.38 | n/a |
| 100 | post-processing (per walk) | 37 | 458.1 | 701.3 | 720.2 | 722.3 | 960 |
| 50 | feature | 3900 | 595.11 | 626.71 | 639.69 | 1934.90 | 174 |
| 50 | matching | 3900 | 5.35 | 12.32 | 13.00 | 106.16 | 4 |
| 50 | iteration | 3900 | 604.71 | 636.14 | 646.98 | 2145.10 | n/a |
| 50 | post-processing (per walk) | 78 | 466.7 | 582.5 | 681.9 | 693.0 | 960 |
| 35 | feature | 3885 | 596.41 | 612.07 | 642.91 | 1021.72 | 174 |
| 35 | matching | 3885 | 5.24 | 12.25 | 13.51 | 111.58 | 4 |
| 35 | iteration | 3885 | 605.87 | 621.62 | 650.02 | 1240.14 | n/a |
| 35 | post-processing (per walk) | 111 | 454.7 | 540.5 | 661.1 | 675.9 | 960 |
| 20 | feature | 4000 | 595.99 | 611.63 | 642.19 | 906.77 | 174 |
| 20 | matching | 4000 | 5.30 | 12.26 | 13.10 | 129.35 | 4 |
| 20 | iteration | 4000 | 605.49 | 619.20 | 649.93 | 1151.11 | n/a |
| 20 | post-processing (per walk) | 200 | 448.4 | 529.4 | 625.7 | 645.4 | 960 |
| 15 | feature | 4020 | 596.42 | 612.19 | 641.66 | 907.64 | 174 |
| 15 | matching | 4020 | 5.32 | 12.35 | 13.68 | 127.31 | 4 |
| 15 | iteration | 4020 | 605.72 | 620.59 | 650.73 | 1147.83 | n/a |
| 15 | post-processing (per walk) | 268 | 455.0 | 541.0 | 635.2 | 664.0 | 960 |

## Summary

Main criterion (Table 1 rates and T=100 RMSE): 11 OK, 1 CHECK, 0 STOP.

- T100: CSV matches results.npz (forward 97.3 vs 97.3, post-processed 97.3 vs 97.3)
- T50: CSV matches results.npz (forward 92.3 vs 92.3, post-processed 94.9 vs 94.9)
- T35: CSV matches results.npz (forward 88.3 vs 88.3, post-processed 92.8 vs 92.8)
- T20: CSV matches results.npz (forward 70.5 vs 70.5, post-processed 86.5 vs 86.5)
- T15: CSV matches results.npz (forward 65.3 vs 65.3, post-processed 81.3 vs 81.3)
