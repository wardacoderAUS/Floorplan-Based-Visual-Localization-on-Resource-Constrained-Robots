# E1 results: F3Loc released checkpoints vs. paper

```
session_start_utc: 2026-10-24T22:27:22+00:00
platform: Google Colab
f3loc_commit: 9e8027d9219ca505078283ebfd925580f476ab97
python: 3.13.16
torch: 2.11.0+cu130 (cuda 13.0)
torchvision: 0.26.0+cu130  lightning: 2.5.6  numpy: 2.1.3  scipy: 1.16.3  opencv: 4.14.0
gpu: Tesla T4
ckpt mono.ckpt: sha256 950d1b9e6e9cd31532619767ed6c8ab9d4cb1870decf3d43eecfcf86bf8a86fb
ckpt mv.ckpt: sha256 793b3d2b77a208c9bfa13426b5914e9edc89fd31a61161f2f5365198d8cb0d26
ckpt comp.ckpt: sha256 5e5c6c764062b4a2ecda1accd8435639b6b070b5d62103b5e9345300a27b5246
zip desdf: 29364839 bytes, 9 files
zip gibson_t: 16429264687 bytes, 51370 files
zip gibson_f: 31274999793 bytes, 99593 files
zip gibson_g: BAD (BadZipFile: File is not a zip file)
gibson_t: 9 test scenes, 4061 poses, 37 trajectories of 100, missing DESDF: none
gibson_f: 9 test scenes, 8028 poses, 2007 4-frame samples, missing DESDF: none
session_start_utc: 2026-10-25T14:57:18+00:00
platform: Google Colab
f3loc_commit: 9e8027d9219ca505078283ebfd925580f476ab97
python: 3.13.16
torch: 2.11.0+cu130 (cuda 13.0)
torchvision: 0.26.0+cu130  lightning: 2.5.6  numpy: 2.1.3  scipy: 1.16.3  opencv: 4.14.0
gpu: Tesla T4
ckpt mono.ckpt: sha256 950d1b9e6e9cd31532619767ed6c8ab9d4cb1870decf3d43eecfcf86bf8a86fb
ckpt mv.ckpt: sha256 793b3d2b77a208c9bfa13426b5914e9edc89fd31a61161f2f5365198d8cb0d26
ckpt comp.ckpt: sha256 5e5c6c764062b4a2ecda1accd8435639b6b070b5d62103b5e9345300a27b5246
zip desdf: 29364839 bytes, 9 files
zip gibson_t: 16429264687 bytes, 51370 files
zip gibson_f: 31274999793 bytes, 99593 files
zip gibson_g: 61321577890 bytes, 198709 files
gibson_g: 9 test scenes, 16056 poses, 4014 4-frame samples, missing DESDF: none
```

## Single-frame observation (Tables 1 and 2)

| dataset | net | paper row | N | metric | ours (%) | ±95% | paper (%) | delta | verdict |
|---|---|---|---|---|---|---|---|---|---|
| gibson_f | d | Ours_s | 2007 | R@0.1m | 4.7 | 0.9 | 4.7 | -0.0 | OK |
| gibson_f | d | Ours_s | 2007 | R@0.5m | 28.6 | 2.0 | 28.6 | -0.0 | OK |
| gibson_f | d | Ours_s | 2007 | R@1m | 36.6 | 2.1 | 36.6 | -0.0 | OK |
| gibson_f | d | Ours_s | 2007 | R@1m30° | 35.0 | 2.1 | 35.1 | -0.1 | OK |
| gibson_f | mvd | Ours_m | 2007 | R@0.1m | 13.3 | 1.5 | 13.2 | +0.1 | OK |
| gibson_f | mvd | Ours_m | 2007 | R@0.5m | 40.8 | 2.2 | 40.9 | -0.1 | OK |
| gibson_f | mvd | Ours_m | 2007 | R@1m | 45.2 | 2.2 | 45.2 | -0.0 | OK |
| gibson_f | mvd | Ours_m | 2007 | R@1m30° | 43.7 | 2.2 | 43.7 | -0.0 | OK |
| gibson_f | comp_s | Ours_t | 2007 | R@0.1m | 13.3 | 1.5 | not reported |  |  |
| gibson_f | comp_s | Ours_t | 2007 | R@0.5m | 40.8 | 2.2 | not reported |  |  |
| gibson_f | comp_s | Ours_t | 2007 | R@1m | 45.2 | 2.2 | not reported |  |  |
| gibson_f | comp_s | Ours_t | 2007 | R@1m30° | 43.7 | 2.2 | not reported |  |  |
| gibson_f | comp | Ours_f | 2007 | R@0.1m | 14.2 | 1.5 | not reported |  |  |
| gibson_f | comp | Ours_f | 2007 | R@0.5m | 42.1 | 2.2 | not reported |  |  |
| gibson_f | comp | Ours_f | 2007 | R@1m | 47.4 | 2.2 | not reported |  |  |
| gibson_f | comp | Ours_f | 2007 | R@1m30° | 45.6 | 2.2 | not reported |  |  |
| gibson_g | d | Ours_s | 4014 | R@0.1m | 4.3 | 0.6 | 4.3 | +0.0 | OK |
| gibson_g | d | Ours_s | 4014 | R@0.5m | 26.6 | 1.4 | 26.7 | -0.1 | OK |
| gibson_g | d | Ours_s | 4014 | R@1m | 33.6 | 1.5 | 33.7 | -0.1 | OK |
| gibson_g | d | Ours_s | 4014 | R@1m30° | 32.2 | 1.4 | 32.3 | -0.1 | OK |
| gibson_g | mvd | Ours_m | 4014 | R@0.1m | 9.3 | 0.9 | 9.3 | -0.0 | OK |
| gibson_g | mvd | Ours_m | 4014 | R@0.5m | 27.0 | 1.4 | 27.0 | -0.0 | OK |
| gibson_g | mvd | Ours_m | 4014 | R@1m | 30.9 | 1.4 | 31.0 | -0.1 | OK |
| gibson_g | mvd | Ours_m | 4014 | R@1m30° | 29.2 | 1.4 | 29.2 | -0.0 | OK |
| gibson_g | comp_s | Ours_t | 4014 | R@0.1m | 10.5 | 0.9 | 10.5 | -0.0 | OK |
| gibson_g | comp_s | Ours_t | 4014 | R@0.5m | 34.2 | 1.5 | 34.3 | -0.1 | OK |
| gibson_g | comp_s | Ours_t | 4014 | R@1m | 39.6 | 1.5 | 39.6 | -0.0 | OK |
| gibson_g | comp_s | Ours_t | 4014 | R@1m30° | 38.0 | 1.5 | 38.0 | +0.0 | OK |
| gibson_g | comp | Ours_f | 4014 | R@0.1m | 12.2 | 1.0 | 12.2 | -0.0 | OK |
| gibson_g | comp | Ours_f | 4014 | R@0.5m | 39.3 | 1.5 | 39.4 | -0.1 | OK |
| gibson_g | comp | Ours_f | 4014 | R@1m | 44.4 | 1.5 | 44.5 | -0.1 | OK |
| gibson_g | comp | Ours_f | 4014 | R@1m30° | 43.1 | 1.5 | 43.2 | -0.1 | OK |

## Sequential filtering on gibson_t, 100-frame history (Table 3)

| net | paper row | trajectories | success@1m ours (%) | paper | delta | RMSE succ ours / paper (m) | RMSE all ours / paper (m) | verdict |
|---|---|---|---|---|---|---|---|---|
| d | Ours_s | 37 | 89.2 | 89.2 | -0.0 | 0.18 / 0.18 | 0.88 / 0.88 | OK |
| comp | Ours_f | 37 | 94.6 | 94.6 | -0.0 | 0.12 / 0.12 | 0.51 / 0.51 | OK |

## Per-step timing (Table 4 context; ours on our GPU, paper hardware unstated)

Lab rule: distributions only. The paper's column is its reported mean, shown for orientation, not compared.

| net | stage | N steps | p50 (ms) | p99 (ms) | p99.9 (ms) | max (ms) | paper mean (ms) |
|---|---|---|---|---|---|---|---|
| d | feature | 3552 | 240.84 | 336.23 | 367.98 | 754.62 | n/a |
| d | matching | 3552 | 4.68 | 9.67 | 11.96 | 156.50 | n/a |
| d | iteration | 3552 | 248.25 | 342.69 | 378.72 | 945.36 | n/a |
| comp | feature | 3552 | 294.88 | 367.84 | 402.47 | 3257.11 | 33 |
| comp | matching | 3552 | 4.58 | 9.82 | 11.67 | 181.73 | 3 |
| comp | iteration | 3552 | 302.20 | 374.97 | 412.14 | 3458.43 | 37 |
