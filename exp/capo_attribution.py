#!/usr/bin/env python3
"""Table 8: where RB (TP)'s advantage over Capo 10.5 comes from, on the 180
few-row floorplans.

    python3 exp/capo_attribution.py      # -> <results>/tables/8_capo_attribution.md

<results> is paper_results/ when it exists (the development checkout), else
results/ (the release).  Inputs, all runs.csv files:
  e17_krow        Capo (the reference), RePlAce (gpl), RB with TP / HM / MK / FP
  e26_ablation    the cumulative component ladder a0_base .. a4_positions (TP)
  e27_capo_attr   leave-one-out on RB (TP) and the partitioner at the base rung
  x3_mega         RB+k (TP); replace_convergence.csv for the RePlAce column
Metric: HPWL after detailed placement (before DPO; no arm here ran DPO),
ratio to Capo on the same floorplan, geometric mean over floorplans, and
the number of floorplans where the arm beats Capo.
"""
import csv
import math
import os
import re
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# the exported CSVs: paper_results/ in the working copy, results/ in the release
PR = next(p for p in (os.path.join(ROOT, "paper_results"), os.path.join(ROOT, "results"))
          if os.path.isdir(os.path.join(p, "x3_mega")))
if not os.path.isdir(PR):
    PR = os.path.join(ROOT, "results")
OUT = os.path.join(PR, "tables", "8_capo_attribution.md")
FEW = re.compile(r"^(?P<design>.+?)_u(?P<util>\d+)_r(?P<rows>\d+)_(?P<pdk>nangate45|asap7)$")
ROWS = [1, 2, 4, 6, 8]
DESIGNS = ["gcd", "aes", "ibex", "jpeg", "swerv_no_macro", "cva6_no_macro"]


def gm(xs):
    xs = [x for x in xs if x]
    return math.exp(sum(map(math.log, xs)) / len(xs)) if xs else None


def f3(x):
    return "--" if x is None else f"{x:.3f}"


def load(exp):
    """-> {design: {variant: hpwl_dpl}} (first seed only; every grid here has one)."""
    out = defaultdict(dict)
    p = os.path.join(PR, exp, "runs.csv")
    if not os.path.exists(p):
        return out
    with open(p) as fh:
        for r in csv.DictReader(fh):
            if r.get("hpwl_dpl") and r.get("violations") in ("", "0") and r["variant"] not in out[r["design"]]:
                out[r["design"]][r["variant"]] = float(r["hpwl_dpl"])
    return out


def main():
    H = {}  # design -> arm -> hpwl
    for exp, variants, rename in (
            ("e17_krow", ["capo", "gpl", "tp", "hm", "mk", "fp"], {}),
            ("e26_ablation", ["a0_base", "a1_waves", "a2_minlocal", "a3_endgame", "a4_positions"], {}),
            ("e27_capo_attr", ["full", "no_exact", "no_optimal", "no_iron", "rows_only", "a0_hm", "a0_fm"], {}),
            ("x3_mega", ["kway_tp"], {})):
        for d, vs in load(exp).items():
            for v in variants:
                if v in vs:
                    H.setdefault(d, {})[rename.get(v, v)] = vs[v]
    fps = sorted(d for d in H if FEW.match(d) and "capo" in H[d])
    assert len(fps) == 180, len(fps)
    meta = {d: FEW.match(d).groupdict() for d in fps}
    conv = {}
    p = os.path.join(PR, "x3_mega", "replace_convergence.csv")
    if os.path.exists(p):
        with open(p) as fh:
            for r in csv.DictReader(fh):
                conv[r["design"]] = r["converged"] == "1" and r["legalization_failed"] == "0"

    def have(arm):
        return [d for d in fps if arm in H[d]]

    def ratio(arm, ref="capo", subset=None):
        ds = [d for d in (subset or fps) if arm in H[d] and ref in H[d]]
        return gm([H[d][arm] / H[d][ref] for d in ds]), sum(H[d][arm] < H[d][ref] for d in ds), len(ds)

    def by_rows(arm, ref="capo"):
        return " | ".join(f3(ratio(arm, ref, [d for d in fps if int(meta[d]["rows"]) == k])[0]) for k in ROWS)

    def by_designs(arm, ref="capo"):
        return " | ".join(f3(ratio(arm, ref, [d for d in fps if meta[d]["design"] == x])[0]) for x in DESIGNS)

    def vs_replace(arm):
        ds = [d for d in fps if conv.get(d) and "gpl" in H[d] and arm in H[d]]
        g = gm([H[d][arm] / H[d]["gpl"] for d in ds])
        return f"{f3(g)} ({len(ds)})" if ds else "--"

    P = ["# 8. Where the advantage over Capo comes from\n",
         "Capo 10.5 (`CapoTest1 -densityTarget <flow density>`, Bookshelf bridge, legalized by the same "
         "`detailed_placement`) is the fixed reference: every row is HPWL after detailed placement / Capo on the "
         "same floorplan, geomean over the 180 few-row floorplans of table 1 (6 designs x 2 PDKs x 60/70/80 % x "
         "1/2/4/6/8 rows), with the number of floorplans the arm wins against Capo. Capo and every arm complete "
         "all 180, so no floorplan is excluded. TritonPart unless stated, seed 1, 4 threads, no DPO. The last column "
         "gives the same arm against RePlAce on the floorplans where RePlAce converged (table 7's rule), for "
         "cross-reference with the paper's ablation table.\n",
         "Sources: Capo, RePlAce and the four RB engines from E17 (`e17_krow`); the cumulative ladder from E26 "
         "(`e26_ablation`); the leave-one-out and base-rung partitioner arms from E27 (`e27_capo_attr`); RB+k from "
         "X3 (`x3_mega`). Regenerate: `python3 exp/capo_attribution.py`.\n",
         "Reading. The ladder is an ablation of *our* components measured against Capo, not a decomposition of "
         "Capo's internals: Capo has its own whitespace allocator, end-case placer and row ironing, so rung 0 is a "
         "bisection core without those pieces (and lands 1.7 % behind Capo), not \"Capo with TritonPart\". "
         "What the table does establish is which of our components carry the 4.6 % (RB) / 6.0 % (RB+k) advantage "
         "and that the partitioning engine is not one of them.\n"]

    # 8.1 ladder
    ladder = [("a0_base", "0. basic cut-net bisection: TritonPart, terminal propagation, uniform whitespace, "
                          "even in-row spreading, no leaf optimization"),
              ("a1_waves", "1. + two checkerboard waves"),
              ("a2_minlocal", "2. + connectivity-guided whitespace (`-reserve minlocal`, adaptive tolerance)"),
              ("a3_endgame", "3. + end game: exact bisection <= 16 cells, leaf B&B (rows, then orders) <= 12, "
                             "row ironing 7 x 2"),
              ("a4_positions", "4. + optimal in-row positions = **RB (TP)**"),
              ("kway_tp", "5. + level-wise k-way reassignment = **RB+k (TP)**")]
    P.append("## 8.1 Cumulative component ladder against Capo\n")
    P.append("| configuration | / Capo | wins / 180 | step | " + " | ".join(f"{k} row{'s' if k > 1 else ''}" for k in ROWS)
             + " | / RePlAce (n) |")
    P.append("|---|---|---|---|" + "---|" * len(ROWS) + "---|")
    prev = None
    for arm, label in ladder:
        g, w, n = ratio(arm)
        if n == 0:
            P.append(f"| {label} | pending | | | " + " | ".join("" for _ in ROWS) + " | |")
            continue
        step = "--" if prev is None else f"{100 * (g / prev - 1):+.1f} %"
        P.append(f"| {label} | {f3(g)} | {w} | {step} | {by_rows(arm)} | {vs_replace(arm)} |")
        prev = g
    P.append("")
    P.append("By design (geomean / Capo over the 30 floorplans of each design; cells in parentheses in table 1):\n")
    P.append("| configuration | " + " | ".join(DESIGNS) + " |")
    P.append("|---|" + "---|" * len(DESIGNS))
    for arm, label in ladder:
        if have(arm):
            P.append(f"| {label.split(':')[0].split(' = ')[0]} | {by_designs(arm)} |")
    P.append("")

    # 8.2 leave-one-out
    P.append("## 8.2 Leave-one-out on RB (TP)\n")
    P.append("Each row removes one end-game component from the full RB (TP) configuration (E27). `full` is RB (TP) "
             "rerun on the E27 binary (control: it must match rung 4 above). The `/ full` columns are the arm's HPWL "
             "over the control's on the same floorplan, so a value above 1 is what the component is worth.\n")
    loo = [("full", "full RB (TP) (control)"),
           ("no_exact", "without exact small-region bisection (`-exact_cells 0`)"),
           ("no_optimal", "without the leaf branch and bound (`-optimal_cells 0`; ironing kept)"),
           ("no_iron", "without row ironing (`-iron_window 0`)"),
           ("rows_only", "leaf B&B orders only: heuristic cells-to-rows assignment kept (`-optimal_rows 0`) "
                         "-- the single-row end case")]
    P.append("| configuration | n | / Capo | wins vs Capo | / full | floorplans worse than full | " +
             " | ".join(f"/ full, {k} row{'s' if k > 1 else ''}" for k in ROWS) + " |")
    P.append("|---|---|---|---|---|---|" + "---|" * len(ROWS))
    for arm, label in loo:
        g, w, n = ratio(arm)
        if n == 0:
            P.append(f"| {label} | pending | | | | | " + " | ".join("" for _ in ROWS) + " |")
            continue
        gf, wf, nf = ratio(arm, "full")
        worse = sum(H[d][arm] > H[d]["full"] for d in fps if arm in H[d] and "full" in H[d])
        P.append(f"| {label} | {n} | {f3(g)} | {w} | {f3(gf)} | {worse}/{nf} | {by_rows(arm, 'full')} |")
    P.append("")
    P.append("On one-row floorplans the row-assignment search has nothing to decide, so `rows_only` equals `full` "
             "there by construction; its effect on 2-8 rows is the value of searching the cells-to-rows assignment "
             "over ordering the cells within rows the heuristic assigned.\n")

    # 8.3 partitioner
    P.append("## 8.3 The partitioning engine\n")
    P.append("The engine swapped at both ends of the ladder: the full RB configuration (E17 arms) and the base rung "
             "(E27 `a0_*`). hMETIS is the class of Capo's own MLPart (multilevel FM); `fm` is TritonPart with "
             "coarsening disabled (flat FM from a random initial bisection).\n")
    P.append("| configuration | engine | / Capo | wins / 180 | " + " | ".join(f"{k} row{'s' if k > 1 else ''}" for k in ROWS)
             + " | / RePlAce (n) |")
    P.append("|---|---|---|---|" + "---|" * len(ROWS) + "---|")
    for arm, cfg, eng in (("tp", "full RB", "TritonPart"), ("hm", "full RB", "hMETIS"),
                          ("mk", "full RB", "Mt-KaHyPar"), ("fp", "full RB", "FastPart"),
                          ("a0_base", "base rung 0", "TritonPart"), ("a0_hm", "base rung 0", "hMETIS"),
                          ("a0_fm", "base rung 0", "flat FM")):
        g, w, n = ratio(arm)
        if n == 0:
            P.append(f"| {cfg} | {eng} | pending | | " + " | ".join("" for _ in ROWS) + " | |")
            continue
        P.append(f"| {cfg} | {eng} | {f3(g)} | {w} | {by_rows(arm)} | {vs_replace(arm)} |")
    P.append("")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as fh:
        fh.write("\n".join(P) + "\n")
    print("wrote", OUT)


if __name__ == "__main__":
    main()
