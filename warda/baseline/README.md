# E1: F3Loc baseline reproduction (Warda, Phase 1)

**Result: reproduced.** Using the authors' released checkpoints and their own evaluation scripts, every metric we can compare is within **0.1 points** of the F3Loc paper (26 of 26 OK, 0 CHECK, 0 STOP). This meets the plan's E1 criterion ("within noise of the published table") and Warda's part of Gate 1.

- Paper: Chen, Wang, Vogel, Pollefeys, *F3Loc: Fusion and Filtering for Floorplan Localization*, CVPR 2024 ([arXiv 2403.03370](https://arxiv.org/abs/2403.03370)), Tables 1 to 3.
- Code: authors' repository [`felix-ch/f3loc`](https://github.com/felix-ch/f3loc), pinned to commit `9e8027d9219ca505078283ebfd925580f476ab97` (31 Aug 2024, the latest).
- Run: `gibson_v1`, 24–25 Oct 2026, Google Colab, Tesla T4.

## Scope: Gibson instead of Structured3D

The plan names the Structured3D test split. The authors release checkpoints and evaluation scripts for **Gibson only**: there is no Structured3D checkpoint or Structured3D evaluation script, and the paper reports Structured3D for one row only (monocular, single frame). Evaluating on Structured3D would require training, which E1 does not allow, and we do not have Structured3D access yet. E1 was therefore run on Gibson, as agreed with Dr. Hassan.

## What was run

F3Loc predicts, from camera images, the distance to the nearest wall in several directions across the view. It compares those distances against a precomputed floorplan lookup (`desdf`: the exact wall distances for every 10 cm position and every 10° heading) and picks the best-matching pose. A histogram filter then combines these matches over time as the camera moves.

The Gibson Floorplan Localization Dataset (collected by the F3Loc authors in the Gibson simulator) has three test sets, all on the same **9 official test scenes**:

| Test set | What it tests | Paper table | Test samples | Models in the paper | Models we ran |
|---|---|---|---|---|---|
| `gibson_f` | single-frame localization, mostly forward motion | Table 1 | 2,007 four-frame samples | mono, multi-view | all 4 (hard selection and fusion are extras) |
| `gibson_g` | single-frame localization, general motion (turns, sideways) | Table 2 | 4,014 four-frame samples | mono, multi-view, hard selection, fusion | all 4 |
| `gibson_t` | sequential localization with the histogram filter | Table 3 | 37 trajectories of 100 frames | mono + filter, fusion + filter | both |

Models (all released checkpoints). In the paper, "Ours" means the F3Loc authors' method and the letter is the variant; in our tables, "Our run" means our reproduction:

| Script name | What it is | Row name in the paper |
|---|---|---|
| `d` | monocular: one image | Ours_s |
| `mvd` | multi-view: current image plus 3 previous ones | Ours_m |
| `comp_s` | hard selection: multi-view if the camera moved enough, else mono | Ours_t |
| `comp` | learned fusion of both | Ours_f |

Evaluation used the authors' scripts `eval_observation.py` (Tables 1 and 2) and `eval_filtering.py` (Table 3). Our patch [`f3loc-e1-logging.patch`](f3loc-e1-logging.patch) changes no algorithm; it only:
- writes one CSV row per frame or trajectory,
- asserts that the number of evaluated frames equals the dataset length (no silently dropped frames; the plan's usual suspect),
- replaces the unmaintained `attrdict` package with a two-line equivalent, since `attrdict` does not import on Python 3.10+.

## At a glance

Numbers are our run / paper. `gibson_f` and `gibson_g`: R@1m (share of samples within 1 m). `gibson_t`: success@1m (share of 100-frame walks whose last 10 estimates are all within 1 m).

| | **gibson_f** | **gibson_g** | **gibson_t** |
|---|---|---|---|
| Letter stands for | **f**orward | **g**eneral | **t**rajectory |
| One sample is | a clip of 4 images | a clip of 4 images | a walk of 100 frames |
| Camera motion | mostly straight ahead | turning, sideways, rotating | continuous walk |
| Question | where am I from one moment? (easy case) | where am I from one moment? (hard case) | can I track where I am over a whole walk? |
| Filter used? | no | no | yes |
| Paper table | Table 1 | Table 2 | Table 3 |
| Test size | 2,007 clips | 4,014 clips | 37 walks |
| **mono**: one image only | 36.6 / 36.6 | 33.6 / 33.7 | **89.2 / 89.2** (with filter) |
| **multi-view**: current image plus the 3 previous ones and the camera motion between them, like stereo; needs the camera to change position | **45.2** / 45.2 | 30.9 / 31.0 | not tested |
| **hard switch**: simple rule, multi-view if the camera moved enough, otherwise mono | 45.2 / not in paper | 39.6 / 39.6 | not tested |
| **fusion**: small trained network that decides per frame how much to trust mono vs. multi-view | **47.4** / not in paper | **44.4** / 44.5 | **94.6 / 94.6** (with filter) |

The **filter** keeps a "where could I be?" map over the walk: each step it shifts the map by the camera's motion and weighs it by how well each position matches the current view, so wrong positions fade out.

## Results

Recall = percentage of test samples whose estimated position is within the stated distance of the true position. Success rate = percentage of trajectories whose last 10 estimates are all within 1 m.

| Paper table | Test set | Model (row name in the paper) | Metric | Our run | Paper |
|---|---|---|---|---|---|
| 1 | gibson_f | mono (paper: Ours_s) | R@1m | 36.6 | 36.6 |
| 1 | gibson_f | multi-view (paper: Ours_m) | R@1m | 45.2 | 45.2 |
| 2 | gibson_g | mono (paper: Ours_s) | R@1m | 33.6 | 33.7 |
| 2 | gibson_g | multi-view (paper: Ours_m) | R@1m | 30.9 | 31.0 |
| 2 | gibson_g | hard selection (paper: Ours_t) | R@1m | 39.6 | 39.6 |
| 2 | gibson_g | fusion (paper: Ours_f) | R@1m | 44.4 | 44.5 |
| 3 | gibson_t | mono + filter (paper: Ours_s) | success@1m | 89.2 | 89.2 |
| 3 | gibson_t | fusion + filter (paper: Ours_f) | success@1m | **94.6** | **94.6** |

Table 3 RMSE also matches: 0.12 m (succeeded) and 0.51 m (all) for fusion; 0.18 m and 0.88 m for mono. All metrics (R@0.1m, R@0.5m, R@1m, R@1m 30°, with 95% intervals) are in [`results/gibson_v1/E1_table.md`](results/gibson_v1/E1_table.md).

**Why 0.1 points is "within noise".** The sampling uncertainty from the test-set size is ±0.6 to ±2.2 points (95%). The run is deterministic (same checkpoints, same frames), so the residual 0.1 comes from a different GPU and newer library versions, not from a different setup.

### Observations

- **Single frames are ambiguous; the filter resolves it.** The best single-frame model places only ~45% of samples within 1 m (many places look alike); with the filter over 100 frames, 94.6% of trajectories succeed. On constrained hardware the filter cannot be dropped.
- **Mono and multi-view fail in different cases.** Forward motion favours multi-view (45.2 vs 36.6); general motion with rotation favours mono (33.7 vs 31.0 in the paper), because multi-view needs camera translation. Fusion is best in both. These are the paper's findings; our run confirms them.
- **Not in the paper:** on `gibson_f`, fusion reaches R@1m 47.4, and hard selection gives exactly the multi-view numbers, because forward motion always exceeds the selection threshold.
- **Small trajectory test set:** 37 trajectories, so one trajectory is 2.7 points (94.6% = 35/37). Relevant when comparing shrunk models in E9.
- **Timing does not reproduce** (context only; Colab is a shared GPU and plan Appendix C cannot be applied): filter step p50 302 ms vs. the paper's 37 ms mean, almost all in the front end; matching 4.6 ms vs. 3 ms. The upstream timing uses `time.time()` without GPU synchronization and includes host/device copies. Handed to Shams (E3/E4).

## E1 against the plan

| Plan requirement | Status |
|---|---|
| Authors' repository | Done (`felix-ch/f3loc`, latest commit) |
| Check for a more recently maintained fork | Searched, none found; the actively maintained derivative is UnLoc (builds on F3Loc). Fork list still to be checked by hand |
| Released checkpoints, no training | Done |
| Released test split | Done, Gibson official split (agreed) |
| Table next to the paper's + running log | Done (this file, [`../log.md`](../log.md)) |
| Within noise | Met, all within 0.1 pt |
| Usual causes ruled out (preprocessing, split, dropped frames) | Same code and released data; official split; frame-count assertion passed (4014/4014, 2007/2007, 37/37) |
| Pinned environment | [`../../env/requirements-e1.lock`](../../env/requirements-e1.lock) records the exact versions of the run. Colab sets Python (3.13.16) |

## How to reproduce

1. Get the data (about 109 GB). Download the four folders of the [Gibson Floorplan Localization Dataset](https://libdrive.ethz.ch/index.php/s/dvKdj8WhmZuIaNw) **one at a time** in the browser (`desdf`, `gibson_t`, `gibson_f`, `gibson_g`; earlier attempts produced empty zips, downloading each folder separately worked) and upload the zips to Google Drive as `MyDrive/Thesis/{desdf,gibson_t,gibson_f,gibson_g}.zip`. Check that each zip opens before uploading; our first `gibson_g` upload was truncated.
2. Open [`E1_F3Loc_Gibson_Colab.ipynb`](E1_F3Loc_Gibson_Colab.ipynb) in Colab, select a T4 GPU, set `DATA_FOLDER_ID` in step 1 to your Drive folder's ID, and Run all. Approve both Google sign-in prompts.
3. The notebook downloads the code at the pinned commit and the checkpoints, checks each zip, and evaluates one test set at a time (stream zip from Drive → unzip → evaluate → delete), since Colab's ~75 GB disk cannot hold all of it. Results are written to Drive as they finish; rerunning skips finished evaluations.

Run time on a T4: about 20 minutes per single-frame model on `gibson_g` (4,014 samples); the full set of 10 evaluations plus unzipping takes several hours and may need more than one Colab session.

The notebook in this folder is the one that produced the results, with its outputs saved (steps 8 to 10 show the zip check, frame counts and recalls). It embeds the patch and [`compare_e1.py`](compare_e1.py).

## Files

| File | Purpose |
|---|---|
| `E1_F3Loc_Gibson_Colab.ipynb` | The complete E1 run, with outputs |
| `f3loc-e1-logging.patch` | Logging, frame-count assertion and `attrdict` shim applied to upstream; no algorithm change |
| `compare_e1.py` | Builds the ours-vs-paper table from the per-frame CSVs |
| `results/gibson_v1/E1_table.md` | Full comparison table, all metrics |
| `results/gibson_v1/provenance.txt` | Commit, versions, GPU, checkpoint SHA-256, zip sizes, test-set counts |
