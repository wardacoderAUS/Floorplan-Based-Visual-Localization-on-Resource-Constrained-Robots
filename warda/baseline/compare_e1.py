"""E1: build the ours-vs-paper table from the per-frame CSVs written by the patched eval scripts.

Usage: python compare_e1.py <results_dir>

Paper numbers are from F3Loc (Chen et al., CVPR 2024), arXiv 2403.03370v2, Tables 1-4.
Row mapping (net_type in the repo -> name in the paper):
    d      -> Ours_s  (monocular)
    mvd    -> Ours_m  (multi-view)
    comp_s -> Ours_t  (hard-threshold selection)
    comp   -> Ours_f  (learned fusion)
Verdict per the research plan: |delta| <= 1 pt OK, 1-2 pts CHECK, > 2 pts STOP and find out why.
"""
import csv
import math
import os
import sys

import numpy as np

PAPER_OBS = {  # (dataset, net) -> R@0.1m, R@0.5m, R@1m, R@1m30deg, in percent
    ("gibson_f", "d"): (4.7, 28.6, 36.6, 35.1),
    ("gibson_f", "mvd"): (13.2, 40.9, 45.2, 43.7),
    ("gibson_g", "d"): (4.3, 26.7, 33.7, 32.3),
    ("gibson_g", "mvd"): (9.3, 27.0, 31.0, 29.2),
    ("gibson_g", "comp_s"): (10.5, 34.3, 39.6, 38.0),
    ("gibson_g", "comp"): (12.2, 39.4, 44.5, 43.2),
}
PAPER_FILT = {  # net -> success@1m (%), RMSE succeeded (m), RMSE all (m)
    "d": (89.2, 0.18, 0.88),
    "comp": (94.6, 0.12, 0.51),
}
PAPER_TIMING_MEAN_S = {"comp": (0.033, 0.003, 0.037)}  # feature, matching, iteration; hardware unstated
NAMES = {"d": "Ours_s", "mvd": "Ours_m", "comp_s": "Ours_t", "comp": "Ours_f"}


def read_csv(path):
    with open(path) as f:
        meta = f.readline().strip()
        rows = list(csv.DictReader(f))
    return meta, rows


def verdict(delta_pts):
    a = abs(delta_pts)
    return "OK" if a <= 1 else ("CHECK" if a <= 2 else "STOP")


def ci95(p, n):
    """95% normal-approx half-width for a proportion p in [0,1], in percentage points."""
    return 196 * math.sqrt(max(p * (1 - p), 1e-12) / n)


def obs_section(out):
    lines = [
        "## Single-frame observation (Tables 1 and 2)",
        "",
        "| dataset | net | paper row | N | metric | ours (%) | ±95% | paper (%) | delta | verdict |",
        "|---|---|---|---|---|---|---|---|---|---|",
    ]
    for ds in ("gibson_f", "gibson_g"):
        for net in ("d", "mvd", "comp_s", "comp"):
            path = os.path.join(out, f"obs_{ds}_{net}.csv")
            if not os.path.exists(path):
                continue
            _, rows = read_csv(path)
            e = np.array([float(r["err_m"]) for r in rows])
            o = np.array([float(r["err_deg"]) for r in rows])
            n = len(e)
            ours = (
                np.mean(e < 0.1),
                np.mean(e < 0.5),
                np.mean(e < 1.0),
                np.mean((e < 1.0) & (o < 30)),
            )
            paper = PAPER_OBS.get((ds, net))
            for k, m in enumerate(("R@0.1m", "R@0.5m", "R@1m", "R@1m30°")):
                ours_pct = 100 * ours[k]
                if paper is None:
                    p, d, v = "not reported", "", ""
                else:
                    p = f"{paper[k]:.1f}"
                    d = f"{ours_pct - paper[k]:+.1f}"
                    v = verdict(ours_pct - paper[k])
                lines.append(
                    f"| {ds} | {net} | {NAMES[net]} | {n} | {m} | {ours_pct:.1f} | "
                    f"{ci95(ours[k], n):.1f} | {p} | {d} | {v} |"
                )
    return lines


def filt_section(out):
    lines = [
        "",
        "## Sequential filtering on gibson_t, 100-frame history (Table 3)",
        "",
        "| net | paper row | trajectories | success@1m ours (%) | paper | delta | RMSE succ ours / paper (m) | RMSE all ours / paper (m) | verdict |",
        "|---|---|---|---|---|---|---|---|---|",
    ]
    timing = [
        "",
        "## Per-step timing (Table 4 context; ours on our GPU, paper hardware unstated)",
        "",
        "Lab rule: distributions only. The paper's column is its reported mean, shown for orientation, not compared.",
        "",
        "| net | stage | N steps | p50 (ms) | p99 (ms) | p99.9 (ms) | max (ms) | paper mean (ms) |",
        "|---|---|---|---|---|---|---|---|",
    ]
    for net in ("d", "comp"):
        tpath = os.path.join(out, f"filt_{net}_traj.csv")
        if os.path.exists(tpath):
            _, rows = read_csv(tpath)
            rm = np.array([float(r["rmse_m"]) for r in rows])
            mx = np.array([float(r["max_last10_err_m"]) for r in rows])
            succ = mx < 1.0
            sr = 100 * succ.mean()
            p = PAPER_FILT[net]
            rs = rm[succ].mean() if succ.any() else float("nan")
            lines.append(
                f"| {net} | {NAMES[net]} | {len(rm)} | {sr:.1f} | {p[0]:.1f} | {sr - p[0]:+.1f} | "
                f"{rs:.2f} / {p[1]:.2f} | {rm.mean():.2f} / {p[2]:.2f} | {verdict(sr - p[0])} |"
            )
        spath = os.path.join(out, f"filt_{net}_steps.csv")
        if os.path.exists(spath):
            _, rows = read_csv(spath)
            for k, (col, label) in enumerate(
                (("feature_s", "feature"), ("matching_s", "matching"), ("iteration_s", "iteration"))
            ):
                x = 1000 * np.array([float(r[col]) for r in rows])
                pm = PAPER_TIMING_MEAN_S.get(net)
                pm = f"{1000 * pm[k]:.0f}" if pm else "n/a"
                q = np.percentile(x, [50, 99, 99.9])
                timing.append(
                    f"| {net} | {label} | {len(x)} | {q[0]:.2f} | {q[1]:.2f} | {q[2]:.2f} | {x.max():.2f} | {pm} |"
                )
    return lines + timing


def main():
    out = sys.argv[1]
    lines = ["# E1 results: F3Loc released checkpoints vs. paper", ""]
    prov = os.path.join(out, "provenance.txt")
    if os.path.exists(prov):
        lines += ["```", open(prov).read().strip(), "```", ""]
    lines += obs_section(out) + filt_section(out)
    print("\n".join(lines))


if __name__ == "__main__":
    main()
