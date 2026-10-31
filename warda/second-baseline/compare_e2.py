"""Compare our UnLoc run on gibson_t with the UnLoc paper (arXiv 2509.11301v2).

Reads the per-trajectory and per-step CSVs written by the [E2] patch of
evaluate.py, one folder per sequence length, and writes a Markdown table with
OK / CHECK / STOP verdicts.

Usage: python compare_e2.py RESULTS_DIR [OUT.md] [provenance.txt]
RESULTS_DIR holds T100/, T50/, T35/, T20/, T15/ (each with unloc_T*_traj.csv,
unloc_T*_steps.csv and results.npz).
"""
import csv
import math
import os
import sys

import numpy as np

T_LIST = [100, 50, 35, 20, 15]

# Paper Table 1 (Gibson(t), SR@1m %). N = number of test trajectories.
PAPER_N = {100: 37, 50: 78, 35: 111, 20: 200, 15: 268}
PAPER_SR_PP = {100: 97.3, 50: 94.9, 35: 92.8, 20: 86.5, 15: 81.3}    # "UnLoc"
PAPER_SR_FWD = {100: 97.3, 50: 92.3, 35: 88.3, 20: 70.5, 15: 65.3}   # "w/o post-processing"
# RMSE at T=100 (m): (successful, all). Same for both rows in the paper.
PAPER_RMSE_PP = (0.16, 0.28)
PAPER_RMSE_FWD = (0.16, 0.28)
# Paper Table 6, Gibson(t), UnLoc single-frame (observation only) row.
PAPER_SINGLE = {"recall_0.1m": 19.7, "recall_0.5m": 61.1, "recall_1m": 64.7,
                "recall_1m30deg": 63.8, "recall_2m": 68.7, "recall_5m": 84.4,
                "recall_10m": 97.6}
PAPER_DEPTH = {"depth_mae_m": 0.25, "depth_cos_sim": 0.981}
# Paper Table 3 (Quadro RTX 6000, means per frame) and Sec. 4 post-processing.
PAPER_TIME_MS = {"feature_ms": 174, "matching_ms": 4, "iteration_ms": None}
PAPER_PP_MS = 960

# F3Loc on the same walks: our E1 run (T=100) and the F3Loc rows of UnLoc Table 1.
F3LOC_E1_T100 = {"F3Loc mono (E1, ours)": 89.2, "F3Loc fusion (E1, ours)": 94.6}
F3LOC_PAPER = {"F3Loc mono": {100: 89.2, 50: 70.5, 35: 55.9, 20: 34.0, 15: 28.4},
               "F3Loc fusion": {100: 94.6, 50: 94.6, 35: 69.4, 20: 46.0, 15: 41.8}}


def read_csv(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def ci95(p, n):
    """Half-width of a normal-approximation 95% interval, in points."""
    if n == 0:
        return float("nan")
    p = p / 100
    return 196 * math.sqrt(max(p * (1 - p), 1 / (4 * n * n)) / n)


def verdict_rate(ours, paper, n):
    """Success rates move in steps of 100/n points (one trajectory)."""
    d = abs(ours - paper)
    if d <= 0.1 + 1e-9:
        return "OK"
    step = 100 / n
    if d <= max(2.0, step) + 0.05 + 1e-9:
        return "CHECK"
    return "STOP"


def verdict_value(ours, paper, ok, check):
    d = abs(ours - paper)
    if d <= ok + 1e-9:
        return "OK"
    if d <= check + 1e-9:
        return "CHECK"
    return "STOP"


def pct(rows, key):
    return 100 * sum(int(r[key]) for r in rows) / len(rows)


def quantiles(x):
    x = np.asarray(x, dtype=float)
    return [np.percentile(x, q) for q in (50, 99, 99.9)] + [x.max()]


def main():
    root = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(root, "E2_table.md")
    prov = sys.argv[3] if len(sys.argv) > 3 else os.path.join(root, "provenance.txt")

    runs = {}
    for T in T_LIST:
        d = os.path.join(root, f"T{T}")
        f = os.path.join(d, f"unloc_T{T}_traj.csv")
        if os.path.exists(f):
            runs[T] = dict(traj=read_csv(f),
                           steps=read_csv(os.path.join(d, f"unloc_T{T}_steps.csv")),
                           npz=os.path.join(d, "results.npz"))

    L = ["# E2 results: UnLoc released checkpoint vs. paper (Gibson(t))", ""]
    if os.path.exists(prov):
        L += ["```", open(prov).read().strip(), "```", ""]
    missing = [T for T in T_LIST if T not in runs]
    if missing:
        L += [f"**Not run yet:** T = {', '.join(map(str, missing))}.", ""]

    # --- consistency with the script's own saved arrays ---
    consistency = []
    for T, r in runs.items():
        if not os.path.exists(r["npz"]):
            consistency.append(f"T{T}: results.npz missing")
            continue
        z = np.load(r["npz"])
        i1 = list(z["success_thresholds_m"]).index(1.0)
        a = 100 * z["success_flags"][:, i1].mean()
        b = pct(r["traj"], "fwd_success_1m")
        c = 100 * z["refined_success_flags"][:, i1].mean() if z["refined_success_flags"].size else float("nan")
        e = pct(r["traj"], "pp_success_1m") if r["traj"][0]["pp_success_1m"] != "" else float("nan")
        same = abs(a - b) < 1e-9 and (math.isnan(c) and math.isnan(e) or abs(c - e) < 1e-9)
        consistency.append(f"T{T}: CSV {'matches' if same else 'DOES NOT match'} results.npz "
                           f"(forward {b:.1f} vs {a:.1f}, post-processed {e:.1f} vs {c:.1f})")

    # --- Table 1 ---
    L += ["## Sequential localization, SR@1m (paper Table 1)", "",
          "| T | N ours / paper | row | ours (%) | ±95% | paper (%) | delta | verdict |",
          "|---|---|---|---|---|---|---|---|"]
    verdicts = []
    for T in T_LIST:
        if T not in runs:
            continue
        rows = runs[T]["traj"]
        n = len(rows)
        nv = "OK" if n == PAPER_N[T] else "STOP"
        for label, key, paper in [("UnLoc w/o post-processing", "fwd_success_1m", PAPER_SR_FWD[T]),
                                  ("UnLoc", "pp_success_1m", PAPER_SR_PP[T])]:
            ours = pct(rows, key)
            v = verdict_rate(ours, paper, n)
            if nv == "STOP":
                v = "STOP"
            verdicts.append(v)
            L.append(f"| {T} | {n} / {PAPER_N[T]} | {label} | {ours:.1f} | {ci95(ours, n):.1f} | "
                     f"{paper:.1f} | {ours - paper:+.1f} | {v} |")
    L += ["", "Verdict: OK = within 0.1 pt; CHECK = off by at most one trajectory "
          "(100/N pt) or 2 pt; STOP = more, or a different number of trajectories.", ""]

    # --- RMSE at T=100 ---
    if 100 in runs:
        rows = runs[100]["traj"]
        L += ["## RMSE at T=100 (m, last 10 frames)", "",
              "| row | RMSE successful ours / paper | RMSE all ours / paper | verdict |",
              "|---|---|---|---|"]
        for label, sk, rk, paper in [("UnLoc w/o post-processing", "fwd_success_1m", "fwd_rmse_m", PAPER_RMSE_FWD),
                                     ("UnLoc", "pp_success_1m", "pp_rmse_m", PAPER_RMSE_PP)]:
            succ = [float(r[rk]) for r in rows if int(r[sk])]
            allr = [float(r[rk]) for r in rows]
            rs = float(np.mean(succ)) if succ else float("nan")
            ra = float(np.mean(allr))
            v = max([verdict_value(rs, paper[0], 0.01, 0.05), verdict_value(ra, paper[1], 0.01, 0.05)],
                    key=["OK", "CHECK", "STOP"].index)
            verdicts.append(v)
            L.append(f"| {label} | {rs:.2f} / {paper[0]:.2f} | {ra:.2f} / {paper[1]:.2f} | {v} |")
        L += ["", "RMSE is the script's own: per trajectory over the last 10 frames, then averaged. "
              "OK within 0.01 m, CHECK within 0.05 m.", ""]

        # --- Single frame (Table 6), from the T=100 run ---
        nfr = len(rows) * 100
        L += ["## Single-frame localization, observation only (paper Table 6, Gibson(t))", "",
              f"From the T=100 run ({nfr} frames). The paper does not state which trajectory length "
              "its Table 6 used, so these verdicts are a cross-check, not part of the main criterion.", "",
              "| metric | ours | paper | delta | verdict |", "|---|---|---|---|---|"]
        for k, paper in PAPER_SINGLE.items():
            ours = 100 * float(np.mean([float(r[k]) for r in rows]))
            L.append(f"| {k.replace('recall_', 'R@')} (%) | {ours:.1f} | {paper:.1f} | {ours - paper:+.1f} | "
                     f"{verdict_value(ours, paper, 0.1, 1.0)} |")
        for k, paper in PAPER_DEPTH.items():
            ours = float(np.mean([float(r[k]) for r in rows]))
            ok, chk = (0.005, 0.02) if k == "depth_mae_m" else (0.0005, 0.005)
            nd = 2 if k == "depth_mae_m" else 3
            L.append(f"| {k} | {ours:.{nd}f} | {paper:.{nd}f} | {ours - paper:+.{nd}f} | "
                     f"{verdict_value(ours, paper, ok, chk)} |")
        L.append("")

    # --- Same table, extended: F3Loc and UnLoc on the same walks ---
    L += ["## F3Loc and UnLoc on the same gibson_t walks (SR@1m %)", "",
          "| method | source | " + " | ".join(f"T={T}" for T in T_LIST) + " |",
          "|---|---|" + "---|" * len(T_LIST)]
    for name, v in F3LOC_E1_T100.items():
        L.append(f"| {name.split(' (')[0]} | ours (E1) | {v:.1f} | " + " | ".join("–" for _ in T_LIST[1:]) + " |")
    for name, d in F3LOC_PAPER.items():
        L.append(f"| {name} | UnLoc paper | " + " | ".join(f"{d[T]:.1f}" for T in T_LIST) + " |")
    for label, key in [("UnLoc w/o post-processing", "fwd_success_1m"), ("UnLoc", "pp_success_1m")]:
        cells = [f"{pct(runs[T]['traj'], key):.1f}" if T in runs else "–" for T in T_LIST]
        L.append(f"| {label} | ours (E2) | " + " | ".join(cells) + " |")
    L.append("")

    # --- Timing (context only) ---
    L += ["## Per-step timing (context only; ours on Colab, paper on a Quadro RTX 6000)", "",
          "Lab rule: distributions only. The paper column is its reported mean, for orientation.", "",
          "| T | stage | N steps | p50 (ms) | p99 (ms) | p99.9 (ms) | max (ms) | paper mean (ms) |",
          "|---|---|---|---|---|---|---|---|"]
    for T in T_LIST:
        if T not in runs:
            continue
        st = runs[T]["steps"]
        for k in ["feature_ms", "matching_ms", "iteration_ms"]:
            q = quantiles([float(s[k]) for s in st])
            p = PAPER_TIME_MS[k]
            L.append(f"| {T} | {k[:-3]} | {len(st)} | " + " | ".join(f"{v:.2f}" for v in q) +
                     f" | {p if p is not None else 'n/a'} |")
        pp = [float(r["pp_ms"]) for r in runs[T]["traj"] if r["pp_ms"] != ""]
        if pp:
            q = quantiles(pp)
            L.append(f"| {T} | post-processing (per walk) | {len(pp)} | " +
                     " | ".join(f"{v:.1f}" for v in q) + f" | {PAPER_PP_MS} |")
    L.append("")

    n_ok = verdicts.count("OK")
    n_chk = verdicts.count("CHECK")
    n_stop = verdicts.count("STOP")
    L += ["## Summary", "",
          f"Main criterion (Table 1 rates and T=100 RMSE): {n_ok} OK, {n_chk} CHECK, {n_stop} STOP.", ""]
    L += [f"- {c}" for c in consistency]
    L.append("")

    open(out, "w").write("\n".join(L))
    print("\n".join(L))


if __name__ == "__main__":
    main()
