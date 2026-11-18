#!/usr/bin/env python3
"""Collect every experiment's results as CSV into one folder for the paper.

    exp/paper_export.py [paper_results]

For each results/<exp> grid with schema-1 run records this writes
    <out>/<exp>/runs.csv      one row per run (design, platform, variant,
                              seed, HPWL global / post-DPL / final, routed
                              wirelength, overflow, timing, runtimes, memory)
    <out>/<exp>/summary.csv   one row per (design, variant): n, mean and
                              half-range over seeds of the main metrics
and copies the experiment-specific exports (wide tables, ratio summaries,
STATUS notes) that exist next to the raw results.  <out>/README.md indexes
the folders and names the paper section each one belongs to.

Re-run after any grid changes; it overwrites.
"""

import csv
import glob
import json
import os
import shutil
import statistics
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# experiment folder -> (group, what it is for the paper, extra files to copy)
#   P  main results of the paper (Sec. "Where Min-Cut Placement Wins")
#   A  ablation and attribution (Table "component ablation", tables 8/9)
#   H  hypotheses H1-H3 (Sec. "Why the Square-Die Gap Persists")
#   D  development runs that preceded the paper's configuration (kept for completeness)
GROUPS = {"P": "Main results", "A": "Ablation and attribution", "H": "Square-die hypotheses (H1-H3)",
          "D": "Development runs (not in the paper)"}
EXPERIMENTS = {
    # ---- P
    "x3_mega": ("P", "RePlAce (`gpl`), RB (TP) (`cutnet_tp`) and RB+k (TP) (`kway_tp`) on the 180 few-row and the 20 square "
                "floorplans, seed 1. RePlAce reference of Tables II / III and Figs. few-row and square; RB+k column everywhere", []),
    "e17_krow": ("P", "Few-row set: Capo and RB with the four partitioners (`tp`, `hm`, `mk`, `fp`) on the 180 floorplans, "
                 "seed 1; RePlAce (`gpl`) with routing. Table II few-row rows, Fig. few-row, routed wirelength (table 7)",
                 ["e17_krow_results.csv", "summary.csv", "summary_hpwl.md", "summary_hpwl_dpl.md",
                  "summary_grt_wl.md", "summary_t_place.md", "summary_t_grt.md", "STATUS.md"]),
    "e23_base": ("P", "Square set: Capo and RB with the four partitioners on the 20 square floorplans, seed 1 "
                 "(megaboom and mempool_group placed but not routed). Table II square row, Fig. square", []),
    "x4_ar_sweep": ("P", "Aspect-ratio x utilization sweep: RePlAce, RB (TP), RB+k (TP) on 182 floorplans "
                      "(8 designs x AR {0.03, 0.06, 0.09, 1} x util {60, 70, 80 %} x 2 PDKs), 3 seeds. Fig. axis, Fig. sweep, table 9",
                      ["x4_floorplans.csv", "x4_runs.csv", "SUMMARY.md", "STATUS.md"]),
    "x5_heldout": ("P", "Held-out designs (b19, ethernet, fft_256, jpegencode, tate_151, tate_pairing, vga_lcd): RePlAce and "
                    "RB+k (TP) on few-row and square floorplans, seed 1 (footnote of the benchmark section; table 10)", []),
    # ---- A
    "e26b_norevisit": ("A", "Component ladder `a0_base` .. `a4_positions` with TritonPart on the 180 few-row floorplans, seed 1 "
                       "(leaf revisiting off). Few-row column of the ablation table; the last two rungs are `x3_mega` "
                       "`cutnet_tp` and `kway_tp`", []),
    "e26_ablation": ("A", "Same ladder with leaf revisiting on at every rung (reference for `e26b_norevisit`)", []),
    "r5_ablation_square": ("A", "The same ladder on the 8 macro-free square floorplans of gcd, aes, ibex, jpeg. "
                           "Square column of the ablation table", []),
    "e27_capo_attr": ("A", "Leave-one-out of RB (TP) against Capo on the few-row set (exact bisection, leaf branch-and-bound, "
                      "row ironing, order-only search) and the partitioner at the base rung (table 8)", []),
    "r1_fewrow_oracle": ("A", "RePlAce-derived cell-to-region assignments with our whitespace allocation and leaf placement "
                         "(`oA`) on the jpeg / swerv / cva6 few-row floorplans at 70 % utilization (\"Does legalization explain "
                         "the advantage?\")", []),
    # ---- H
    "i1_info": ("H", "H1 / H2 / H3 on the square floorplans, 2 seeds: stronger partitioning (`imp`), degraded bisections (`deg*`), "
                "oracle terminals (`oT`), HPWL-estimating objective (`hp`, `hp_oT`), oracle assignments at every level (`oA`). "
                "Fig. anatomy (a), table 6.7", ["STATUS.md"]),
    "am1": ("H", "American maps: RePlAce-derived assignments through the min-cut tree, with the map above / below a level "
            "and min-cut at one level (Fig. anatomy (b), `AMERICAN_MAPS_STUDY.md`, table 5)", []),
    # ---- D
    "e0_baselines": ("D", "RePlAce modes (default / timing / routability driven) and partitioner strength on the first placer", []),
    "e3_objectives_moves50": ("D", "First objectives x k pass (TritonPart 50-move budget)", ["summary.csv"]),
    "e3_objectives": ("D", "Objectives x k with the full flow", ["summary.csv"]),
    "e5_reserve": ("D", "Whitespace policies (minlocal / mixed / congestion, padding)", ["summary.csv"]),
    "e7_hybrid": ("D", "Hybrids without a seed (min-cut then RePlAce / incremental)", ["summary.csv"]),
    "e4_seed": ("D", "Seeded decomposition (QP and RePlAce seeds, assignment models)", []),
    "e6_endgame": ("D", "Leaf placement ablation (exact / optimal / ironing)", []),
    "e8_seedlevels": ("D", "Per-level attribution of the gap (RePlAce seed down to level L)", []),
    "e9_positions": ("D", "Optimal in-row positions", []),
    "e10_leaffb": ("D", "Leaf feedback passes", []),
    "e11_feedback": ("D", "Placement feedback passes and seed-located terminals", []),
    "e12_dpo": ("D", "Detailed placement optimization (DPO) on both flows", []),
    "e13_engines": ("D", "hMETIS vs TritonPart under the then-current flow", []),
    "e14_capo": ("D", "Capo 10.5 settings on the development checkpoints", []),
    "e15_fastpart": ("D", "FastPart as the partitioning engine", []),
    "e16_compare": ("D", "The six placers on the five development checkpoints", ["summary.csv"]),
    "e18_endgame_dpo": ("D", "Hybridization: analytical decides, min-cut commits (leaf placement + DPO)", ["summary.md"]),
    "e19_overflow": ("D", "Hybridization: overflow threshold of the seed", ["summary.md"]),
    "e19b_refine": ("D", "Hybridization: min-cut refinement of analytical positions", ["summary.md"]),
    "e20a_objective": ("D", "Hybridization: objective of the refinement", ["summary.md"]),
    "e20b_seedmodel": ("D", "Hybridization: seed model", ["summary.md"]),
    "e21a_oracle": ("D", "Hybridization: terminal propagation from the QP oracle", ["summary.md"]),
    "e21b_anchor_acg": ("D", "Hybridization: anchors and ACG whitespace", ["summary.md"]),
    "e21c_acg_tol": ("D", "Hybridization: ACG tolerance", ["summary.md"]),
    "e21d_adaptive": ("D", "Hybridization: adaptive rules", ["summary.md"]),
    "e21e_engines": ("D", "Hybridization: four partitioners, baseline -> hybrid", ["summary.md"]),
    "e24_hyb_krow": ("D", "Hybrid on the few-row floorplans (vs `e17_krow`)", []),
    "e24_hyb_base": ("D", "Hybrid on the square floorplans (vs `e23_base`)", []),
    "e25_objectives": ("D", "Partitioner objectives and net weights on 9 floorplans",
                       ["e25_objectives.csv", "e25_objectives_summary.md", "STATUS.md"]),
    "x3_dev": ("D", "RB and RB+k on the three development checkpoints, 3 seeds, no DPO", []),
    "x1a_kway": ("D", "k-way: from-scratch top-level k-way construction", ["STATUS.md"]),
    "x1b_kway": ("D", "k-way: whole-level k-way refinement", ["STATUS.md"]),
    "x5_iccad04": ("D", "ICCAD04 ibm benchmarks (Bookshelf, imported): RePlAce / RB / RB+k; not used in the paper", []),
}
EXTRA_FILES = {  # files from elsewhere -> (dest folder, dest name)
    "results/viz/e17/summary_table.md": ("e17_krow", "viz_summary_table.md"),
    "results/viz/hybrid/summary_tables.md": ("hybridization_e18_e24", "summary_tables.md"),
    "scratch/ar_sweep/meta/summary.csv": ("x4_ar_sweep", "replace_baseline_floorplan_author.csv"),
    "AMERICAN_MAPS_STUDY.md": ("am1", "AMERICAN_MAPS_STUDY.md"),
}
FINAL = ["hpwl", "hpwl_global", "hpwl_dpl", "grt_wl", "overflow", "max_overflow_h", "max_overflow_v",
         "wns", "tns", "t_place", "t_dpl", "t_dpo", "t_grt", "t_total", "mem_peak_mb",
         "dpl_moves", "dpl_displacement_mean", "violations", "routing"]
SUMMARY_COLS = ["hpwl", "hpwl_dpl", "hpwl_global", "grt_wl", "t_place", "t_grt", "mem_peak_mb"]


def load_runs(folder):
    runs = []
    for path in sorted(glob.glob(os.path.join(folder, "*.json"))):
        if path.endswith((".eval.json", ".metrics.json", ".mcp.json")):
            continue
        try:
            d = json.load(open(path))
        except (OSError, ValueError):
            continue
        if d.get("schema") != 1 or "final" not in d:
            continue
        k = d.get("knobs") or {}
        fin = d["final"]
        r = dict(run=os.path.basename(path)[:-5], design=d.get("design"), platform=d.get("platform"),
                 variant=k.get("variant", d.get("variant")), kind=k.get("kind", d.get("variant")),
                 seed=d.get("seed"), threads=d.get("threads"), dpo=k.get("dpo"), host=k.get("host"),
                 cells=(d.get("netlist") or {}).get("movable"), nets=(d.get("netlist") or {}).get("nets"))
        for c in FINAL:
            r[c] = fin.get(c)
        r["options"] = k.get("args", "")
        runs.append(r)
    return runs


def half_range(vals):
    return (max(vals) - min(vals)) / 2 if len(vals) > 1 else 0.0


def write_csvs(runs, dest):
    cols = ["run", "design", "platform", "variant", "kind", "seed", "threads", "dpo", "host", "cells", "nets"] + FINAL + ["options"]
    with open(os.path.join(dest, "runs.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols)
        w.writeheader()
        for r in sorted(runs, key=lambda r: (str(r["design"]), str(r["variant"]), r["seed"] or 0)):
            w.writerow(r)
    groups = {}
    for r in runs:
        groups.setdefault((r["design"], r["variant"]), []).append(r)
    hdr = ["design", "platform", "variant", "n", "cells"]
    for c in SUMMARY_COLS:
        hdr += [f"{c}_mean", f"{c}_halfrange"]
    with open(os.path.join(dest, "summary.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(hdr)
        for (design, variant), rs in sorted(groups.items(), key=lambda kv: (str(kv[0][0]), str(kv[0][1]))):
            row = [design, rs[0]["platform"], variant, len(rs), rs[0]["cells"]]
            for c in SUMMARY_COLS:
                vals = [r[c] for r in rs if isinstance(r[c], (int, float))]
                row += [f"{statistics.mean(vals):.6g}" if vals else "", f"{half_range(vals):.3g}" if vals else ""]
            w.writerow(row)
    return groups


def main(argv):
    out = os.path.join(ROOT, argv[1] if len(argv) > 1 else "paper_results")
    os.makedirs(out, exist_ok=True)
    index = []
    for exp, (group, purpose, extras) in EXPERIMENTS.items():
        src = os.path.join(ROOT, "results", exp)
        if not os.path.isdir(src):
            print(f"skip {exp}: no folder")
            continue
        runs = load_runs(src)
        if not runs:
            print(f"skip {exp}: no run records")
            continue
        dest = os.path.join(out, exp)
        os.makedirs(dest, exist_ok=True)
        groups = write_csvs(runs, dest)
        copied = ["runs.csv", "summary.csv"]
        for name in extras:
            p = os.path.join(src, name)
            if os.path.exists(p):
                # the generic summary.csv above must not be overwritten by a grid's own summary.csv
                dname = "summary_runs_legacy.csv" if name == "summary.csv" else name
                shutil.copy2(p, os.path.join(dest, dname))
                copied.append(dname)
            else:
                print(f"  {exp}: missing {name}")
        designs = sorted({str(r["design"]) for r in runs})
        variants = sorted({str(r["variant"]) for r in runs})
        index.append((exp, group, purpose, len(runs), designs, variants, copied))
        print(f"{exp:24s} {len(runs):5d} runs  {len(designs):3d} designs  {len(variants):3d} variants")

    for rel, (dfolder, dname) in EXTRA_FILES.items():
        p = os.path.join(ROOT, rel)
        if os.path.exists(p):
            dest = os.path.join(out, dfolder)
            os.makedirs(dest, exist_ok=True)
            shutil.copy2(p, os.path.join(dest, dname))
        else:
            print(f"missing extra {rel}")

    L = ["# Results\n",
         f"Generated by `python3 exp/paper_export.py` from `results/<exp>/*.json`. One sub-folder per experiment.\n",
         "**Start with [`tables/`](tables/README.md)**: `tables/0_paper_tables.md` reproduces every table and figure of the "
         "paper from the CSVs here, under the paper's rules; tables 1–10 are the extended views. The per-experiment "
         "folders below are the raw data behind them.\n",
         "Every folder has `runs.csv` (one row per run) and `summary.csv` (one row per design × variant, mean and "
         "half-range over seeds). Columns of `runs.csv`:\n",
         "| column | meaning |", "|---|---|",
         "| `hpwl_global` | HPWL (µm) as left by the placer, before legalization |",
         "| `hpwl_dpl` | HPWL after `detailed_placement`, before DPO — **the primary metric** in E12 onwards |",
         "| `hpwl` | final HPWL: after `improve_placement` (DPO) where `dpo` = 1, else = `hpwl_dpl` |",
         "| `grt_wl`, `overflow`, `max_overflow_h/v` | `global_route` wirelength (µm, GRT-0018) and the `Total` line of the final "
         "congestion report (GRT-0096); empty when not routed. GRT-0018 divides by the block's DEF units, so `grt_wl` is 10× too "
         "large for the ASAP7 checkpoints of aes, gcd, jpeg and swerv (DEF units 100, 1000 dbu/µm); `exp/grt_tables.py` corrects "
         "it, ratios on one floorplan are unaffected |",
         "| `wns`, `tns` | post-route STA (ns); empty when no liberty / not routed |",
         "| `t_place`, `t_dpl`, `t_dpo`, `t_grt`, `t_total` | seconds, 4 threads unless `threads` says otherwise |",
         "| `mem_peak_mb` | peak RSS of the OpenROAD process |",
         "| `violations` | legalizer check violations (0 = legal) |",
         "| `routing` | `ok`, `not run` / `skipped (MCP_NO_ROUTE)`, or `failed: <code>` |",
         "| `options` | the mcp option string of the variant (empty for gpl / capo) |",
         "| `kind` | `gpl`, `capo`, `mincut`, … — the placer family |",
         "\nOlder grids (E0–E11) ran without DPO: `hpwl` = `hpwl_dpl` there. Where a grid has its own wide table or "
         "ratio summary (E17, E25, X4, hybridization), it is copied alongside; `summary_runs_legacy.csv` is the "
         "grid's original `exp/summarize.py --csv` export where one existed.\n",
         "Placer codes used in the `variant` column: `gpl` = RePlAce (OpenROAD `global_placement`); `capo` = Capo 10.5; "
         "`tp` / `hm` / `mk` / `fp` = RB with TritonPart / hMETIS / Mt-KaHyPar / FastPart; `cutnet_tp` = RB (TP) "
         "(same configuration as `tp`, in the `x3_mega` / `x4_ar_sweep` grids); `kway_tp` = RB+k (TP). Other variant "
         "names are experiment-specific and explained in the folder's `STATUS.md` or in the table that uses them.\n",
         "## Folders\n"]
    for g, gname in GROUPS.items():
        rows = [r for r in index if r[1] == g]
        if not rows:
            continue
        L.append(f"### {gname}\n")
        L.append("| folder | what it is | runs | designs | variants | files |")
        L.append("|---|---|---|---|---|---|")
        for exp, _, purpose, n, designs, variants, copied in rows:
            dv = ", ".join(variants) if len(variants) <= 12 else f"{len(variants)} variants: " + ", ".join(variants[:8]) + ", …"
            dd = ", ".join(designs) if len(designs) <= 8 else f"{len(designs)} floorplans"
            L.append(f"| `{exp}/` | {purpose} | {n} | {dd} | {dv} | {', '.join(f'`{c}`' for c in copied)} |")
        if g == "D":
            L.append("| `hybridization_e18_e24/` | tables behind the hybridization studies (`e18`–`e24`) | | | | `summary_tables.md` |")
        L.append("")
    L.append("## Failure accounting files\n")
    L.append("* `x3_mega/replace_convergence.csv`, `e23_base/replace_convergence.csv`, `x4_ar_sweep/replace_convergence.csv`: per "
             "RePlAce run (floorplan, seed), what its log reported — final density overflow, iterations, whether it hit the "
             "5000-iteration cap, diverged or failed legalization — and the resulting `converged` flag (1 = stopped at or below "
             "the 0.2 overflow target and legalized). A RePlAce run with `converged` = 0 is a failure in the paper; "
             "`0_paper_tables.md`, tables 7–9 apply this rule (137 of the 180 few-row floorplans remain), tables 1–6 use the "
             "weaker condition that RePlAce produced a legal placement (149).")
    L.append("* `e17_krow/rb_engine_reports.csv`, `e23_base/rb_engine_reports.csv`: per RB run, how many bisections the "
             "partitioner failed to return (the placer's FM fallback stood in), whether the root bisection failed, the resulting "
             "`verdict` (`ok`, `kept` = sporadic fallback below 1 % of the bisections, `fail` = root or > 1 % failed — excluded "
             "in the paper), the cause, and the root / total cut counts behind the cut rows of Table II.")
    L.append("")
    L.append("## Notes\n")
    L.append("* `e23_base`: megaboom and mempool_group were placed but not routed (FastRoute ran for days). The `fp` "
             "(FastPart) arm on xiangshan_ar1_nangate45, megaboom (both PDKs) and mempool_group is not a FastPart result: "
             "the engine's worker processes died at start-up and every region fell back to FM (`verdict` = `fail` in "
             "`rb_engine_reports.csv`); it is excluded from every aggregate.")
    L.append("* `x4_ar_sweep`: `replace_baseline_floorplan_author.csv` is the floorplan author's own RePlAce (`-routability_driven`) summary "
             "for the floorplans he had finished (ar0.03_util60); not used in the paper.")
    L.append("* `x3_mega` is the grid of `exp/x3_mega.cfg`; one stray `gpl_dia` run is a probe.")
    L.append("* `gpl` is deterministic; its seeds 1–3 are identical runs where present.")
    with open(os.path.join(out, "README.md"), "w") as f:
        f.write("\n".join(L) + "\n")
    print(f"\n{len(index)} experiments -> {out}")


if __name__ == "__main__":
    main(sys.argv)
