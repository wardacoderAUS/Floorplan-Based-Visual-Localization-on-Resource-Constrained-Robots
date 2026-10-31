# Second baseline: UnLoc on Gibson

UnLoc (Wüest et al., ICLR 2026), run from the authors' code with their released Gibson checkpoint, on the same `gibson_t` test walks as our F3Loc run. Nothing is trained. Approved by Dr. Hassan on 29 Oct 2026 in place of LASER, which has no public code.

## Result: reproduced

All 10 success rates of the paper's Table 1 match exactly (SR@1m %, Colab):

| Walk length (frames) | 100 | 50 | 35 | 20 | 15 |
|---|---|---|---|---|---|
| UnLoc (ours = paper) | 97.3 | 94.9 | 92.8 | 86.5 | 81.3 |
| UnLoc w/o post-processing (ours = paper) | 97.3 | 92.3 | 88.3 | 70.5 | 65.3 |
| F3Loc mono, same walks (UnLoc paper) | 89.2 | 70.5 | 55.9 | 34.0 | 28.4 |

- Walk counts equal the paper's (37 / 78 / 111 / 200 / 268); nothing dropped. Single-frame numbers (Table 6) also match.
- One difference: RMSE at 100 frames for the post-processed row is 0.20 / 0.33 m vs. 0.16 / 0.28 in the paper. Our w/o-post-processing RMSE is exactly 0.16 / 0.28, and the paper prints the same RMSE for both rows, so it likely repeated that value. Not confirmed.
- Full table, timing and setup: `results/E2_table.md`, `results/provenance.txt`.

## How to run

Open `E2_UnLoc_Gibson_Colab.ipynb` in Colab, choose a GPU **T4 GPU** or **A100**, then **Run all**. It reads `gibson_t.zip` and `desdf.zip` from `MyDrive/Thesis/` and writes results to `MyDrive/Thesis/E2/results/unloc_v1/`. About 3.5 hours; if Colab disconnects, Run all again and finished runs are skipped.

- Code: `matthias-wueest/UnLoc` @ `3c7a301`, Depth Anything V2 @ `a561b84`.
- Checkpoints: `unloc_gibson_vitl.ckpt` (authors' Google Drive, 15 MB) and Depth Anything V2 Metric-Hypersim Large (Hugging Face, 1.3 GB). Not in this repository.

## Files

| File | Purpose |
|---|---|
| `E2_UnLoc_Gibson_Colab.ipynb` | The whole run, with outputs |
| `unloc-e2-logging.patch` | Changes to `evaluate.py`, each tagged `[E2]`: per-walk and per-step CSVs, dropped-walk check, checkpoint-weight check |
| `compare_e2.py` | Ours vs. paper with OK / CHECK / STOP verdicts |
| `results/` | Results table and setup record |

## Notes

- The trained part of UnLoc is small (15 MB); the rest is the frozen Depth Anything Large network, which takes almost all of the time per frame. This is the part to shrink.
- The authors' script evaluates with the network in training mode, so the BatchNorm in UnLoc's head uses each image's own statistics. The paper numbers were made this way; a shrunk version must keep this or show it doesn't matter.
- Colab timing is context only, as in the F3Loc run.
