#!/usr/bin/env python3
"""Every table and figure of the paper, recomputed from the CSVs in
paper_results/ (results/ in the release) under the paper's rules:
paper_results/tables/0_paper_tables.md.

    python3 exp/paper_export.py      # builds paper_results/<exp>/runs.csv
    python3 exp/paper_numbers.py     # this file

Inputs (all under paper_results/):
  x3_mega/runs.csv + replace_convergence.csv      RePlAce, RB (TP), RB+k (TP); few-row and square
  e17_krow/runs.csv + rb_engine_reports.csv       Capo, RB x {TP, HM, MK, FP}; few-row
  e23_base/runs.csv + replace_convergence.csv + rb_engine_reports.csv   same on the square set
  x4_ar_sweep/runs.csv + replace_convergence.csv   aspect-ratio x utilization sweep, 3 seeds
  e26b_norevisit/runs.csv, r5_ablation_square/runs.csv   component ladder (few-row, square)
  r1_fewrow_oracle/runs.csv                       RePlAce-derived assignments on few-row floorplans
  x5_heldout/runs.csv                            held-out designs

Rules (Sec. "Evaluation" of the paper):
  * metric = HPWL after detailed placement, before DPO (`hpwl_dpl`), mean over seeds per floorplan,
    ratio to RePlAce on the same floorplan, geometric mean over floorplans; (wins/n) = floorplans
    where the placer beats RePlAce / floorplans compared;
  * a RePlAce run fails if it diverges, does not legalize, or stops above its 20 % overflow
    target (`converged` = 0 in replace_convergence.csv); an RB run fails if its partitioner
    returned no partition at the root or on more than 1 % of the bisections (`verdict` = fail in
    rb_engine_reports.csv) or the run produced no legal placement;
  * every ratio is over the floorplans *common* to the configurations compared (none fails).
"""
import csv
import math
import os
import re
import statistics
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# the exported CSVs: paper_results/ in the working copy, results/ in the release
PR = next(p for p in (os.path.join(ROOT, "paper_results"), os.path.join(ROOT, "results"))
          if os.path.isdir(os.path.join(p, "x3_mega")))
OUT = os.path.join(PR, "tables", "0_paper_tables.md")
FEW = re.compile(r"^(?P<design>.+?)_u(?P<util>\d+)_r(?P<rows>\d+)_(?P<pdk>nangate45|asap7)$")
SQ = re.compile(r"^(?P<design>.+?)_ar1_(?P<pdk>nangate45|asap7)$")
SWEEP = re.compile(r"^[a-z]+_(?P<design>.+?)_ar(?P<ar>[0-9p]+)_util(?P<util>\d+)_(?P<pdk>nangate45|asap7)$")
FEW_DESIGNS = ["gcd", "aes", "ibex", "jpeg", "swerv_no_macro", "cva6_no_macro"]
SQ_DESIGNS = ["gcd", "aes", "ibex", "jpeg", "swerv_wrapper", "cva6", "mempool_tile", "xiangshan", "megaboom", "mempool_group"]
SWEEP_DESIGNS = ["gcd", "aes", "ibex", "jpeg", "swerv", "cva6", "mempool_tile", "xiangshan"]
X5_DESIGNS = ["b19", "ethernet", "fft_256", "jpegencode", "tate_151", "tate_pairing", "vga_lcd"]
ENGINES = [("Capo", "e17", "capo"), ("RB (TP)", "e17", "tp"), ("RB (HM)", "e17", "hm"),
           ("RB (MK)", "e17", "mk"), ("RB (FP)", "e17", "fp"), ("RB+k (TP)", "x3", "kway_tp")]


# ------------------------------------------------------------------ helpers
def gm(xs):
    xs = [x for x in xs if x and x > 0]
    return math.exp(sum(map(math.log, xs)) / len(xs)) if xs else None


def fmt(x, n=3):
    return "–" if x is None else f"{x:.{n}f}"


def pct(x):
    """ratio -> '18.4 % lower' / '10.5 % higher'."""
    return "–" if x is None else (f"{100 * (1 - x):.1f} % lower" if x < 1 else f"{100 * (x - 1):.1f} % higher")


def cell(pairs):
    xs = [x for _, x in pairs if x]
    return "–" if not xs else f"{gm(xs):.3f} ({sum(x < 1 for x in xs)}/{len(xs)})"


def table(hdr, rows):
    out = ["| " + " | ".join(str(h) for h in hdr) + " |", "|" + "---|" * len(hdr)]
    out += ["| " + " | ".join(str(c) for c in r) + " |" for r in rows]
    return "\n".join(out) + "\n"


def load_runs(exp):
    """design -> variant -> [rows]"""
    out = defaultdict(lambda: defaultdict(list))
    p = os.path.join(PR, exp, "runs.csv")
    if not os.path.exists(p):
        return out
    for r in csv.DictReader(open(p)):
        out[r["design"]][r["variant"]].append(r)
    return out


def load_conv(exp):
    """(design, seed) -> converged?  and design -> all seeds converged?"""
    per, by = {}, defaultdict(list)
    p = os.path.join(PR, exp, "replace_convergence.csv")
    if os.path.exists(p):
        for r in csv.DictReader(open(p)):
            ok = r["converged"] == "1" and r["legalization_failed"] == "0"
            per[(r["design"], r.get("seed", "1") or "1")] = (ok, r)
            by[r["design"]].append(ok)
    return per, {d: all(v) for d, v in by.items()}


def load_engine(exp):
    """design -> variant -> row of rb_engine_reports.csv"""
    out = defaultdict(dict)
    p = os.path.join(PR, exp, "rb_engine_reports.csv")
    if os.path.exists(p):
        for r in csv.DictReader(open(p)):
            out[r["design"]][r["variant"]] = r
    return out


def legal_mean(rows, key="hpwl_dpl"):
    """mean metric over seeds if every seed is legal, else None."""
    if not rows:
        return None
    xs = []
    for r in rows:
        v = r.get(key) or r.get("hpwl")
        if not v or r.get("violations") not in ("", "0", None):
            return None
        xs.append(float(v))
    return statistics.mean(xs)


def cells_of(runs, d):
    for vs in runs[d].values():
        for r in vs:
            if r.get("cells"):
                return int(r["cells"])
    return None


# ------------------------------------------------------------------ few-row set
def few_row(P):
    x3, e17 = load_runs("x3_mega"), load_runs("e17_krow")
    conv, _ = load_conv("x3_mega")
    eng = load_engine("e17_krow")
    fps = sorted(d for d in x3 if FEW.match(d))
    assert len(fps) == 180, len(fps)

    S = {}
    for d in fps:
        m = FEW.match(d).groupdict()
        ok, cr = conv.get((d, "1"), (False, {}))
        gpl = legal_mean(x3[d].get("gpl")) if ok else None
        st = dict(name=d, rows=int(m["rows"]), design=m["design"], pdk=m["pdk"], util=m["util"],
                  group=(m["design"], m["pdk"], m["util"]), gpl=gpl, conv=cr)
        for label, src, var in ENGINES:
            rows = (e17 if src == "e17" else x3)[d].get(var, [])
            h = legal_mean(rows)
            if var in ("tp", "hm", "mk", "fp") and eng[d].get(var, {}).get("verdict") == "fail":
                h = None
            st[label] = h
            st[label + "/gpl"] = h / gpl if (h and gpl) else None
        S[d] = st

    # --- RePlAce failure breakdown
    n_conv = sum(1 for s in S.values() if s["gpl"])
    div = sum(1 for s in S.values() if s["conv"].get("diverged") == "1")
    dpl = sum(1 for s in S.values() if s["conv"].get("legalization_failed") == "1" and s["conv"].get("diverged") != "1")
    ovf = 180 - n_conv - div - dpl
    P.append("## 2. Few-row floorplans (Sec. \"Few-Row Floorplans -- Min-Cut Wins\", Table II, Fig. few-row)\n")
    P.append(f"RePlAce succeeds on **{n_conv} of 180** floorplans: {div} diverge, {dpl} fail legalization, "
             f"{ovf} legalize but stop above the 20 % overflow target (`x3_mega/replace_convergence.csv`). ")
    eng_fail = {lab: sorted(d for d in fps if eng[d].get(var, {}).get("verdict") == "fail")
                for lab, src, var in ENGINES if src == "e17" and var != "capo"}
    P.append("RB engine failures by the 1 %-of-bisections rule (`e17_krow/rb_engine_reports.csv`): " +
             "; ".join(f"{k} {len(v)}" + (f" ({', '.join(v)})" if v else "") for k, v in eng_fail.items()) + ".\n")

    ok = [s for s in S.values() if s["gpl"]]
    tp = [(s["name"], s["RB (TP)/gpl"]) for s in ok if s["RB (TP)/gpl"]]
    P.append(f"**RB (TP) against RePlAce on the {len(tp)} common floorplans: {fmt(gm([x for _, x in tp]))} "
             f"({pct(gm([x for _, x in tp]))}; wins {sum(x < 1 for _, x in tp)}/{len(tp)}).** ")
    poor = [(d, x) for d, x in tp if x < 0.5]
    rest = [x for d, x in tp if x >= 0.5]
    P.append(f"Excluding the {len(poor)} cases where RePlAce's HPWL is more than twice RB (TP)'s (ratio < 0.5: "
             + ", ".join(f"{d} {x:.2f}" for d, x in sorted(poor, key=lambda t: t[1])) +
             f"), the ratio is {fmt(gm(rest))} ({pct(gm(rest))}).\n")
    groups = defaultdict(list)
    for s in S.values():
        groups[s["group"]].append(s)
    complete = {g for g, ss in groups.items() if len(ss) == 5 and all(s["gpl"] for s in ss)}
    a = [x for d, x in tp if S[d]["group"] in complete]
    b = [x for d, x in tp if S[d]["group"] not in complete]
    P.append(f"Complete groups (design-PDK-utilization groups where RePlAce succeeds at all five row counts): "
             f"{len(complete)} of 36, {len(a)} floorplans, RB (TP) / RePlAce {fmt(gm(a))} ({pct(gm(a))}); "
             f"the other {len(b)} floorplans: {fmt(gm(b))} ({pct(gm(b))}).\n")
    P.append("Routed wirelength and routing overflow: [7_global_routing.md](7_global_routing.md) "
             "(RB (TP) against RePlAce on the floorplans both placers routed).\n")

    # --- Table II
    labels = [l for l, _, _ in ENGINES]
    common = [s for s in ok if all(s[l + "/gpl"] for l in labels)]
    P.append("### Table II, HPWL / RePlAce on the floorplans common to all configurations\n")

    def row(name, sel, denom):
        ss = [s for s in common if sel(s)]
        return [name, f"{len(ss)}/{denom}"] + [cell([(s["name"], s[l + "/gpl"]) for s in ss]) for l in labels]
    rows = [row(f"{r} row{'s' if r > 1 else ''}", (lambda r: lambda s: s["rows"] == r)(r), 36) for r in (1, 2, 4, 6, 8)]
    rows.append(row("All few-row", lambda s: True, 180))
    rows.append(row("Complete groups", lambda s: s["group"] in complete, 180))
    P.append(table(["Floorplans", "n", *labels], rows))
    cap = [s["Capo/gpl"] for s in common]
    P.append(f"Capo 10.5 on the {len(common)} common floorplans: {fmt(gm(cap))}; the four RB engines span "
             f"{fmt(min(gm([s[l + '/gpl'] for s in common]) for l in labels[1:5]))}–"
             f"{fmt(max(gm([s[l + '/gpl'] for s in common]) for l in labels[1:5]))}.\n")

    # --- cut rows
    eng_common = [d for d in fps if all(eng[d].get(v, {}).get("verdict") in ("ok", "kept") for v in ("tp", "hm", "mk", "fp"))]
    P.append("### Table II, cut / TP cut (floorplans on which all four engines complete; `rb_engine_reports.csv`)\n")
    rows = []
    for key, name in (("root_cut", "Few-row, root"), ("total_cut", "Few-row, total")):
        c = [name, f"{len(eng_common)}/180"]
        for v in ("tp", "hm", "mk", "fp"):
            c.append(fmt(gm([float(eng[d][v][key]) / float(eng[d]["tp"][key]) for d in eng_common]), 2))
        rows.append(c)
    P.append(table(["", "n", "TP", "HM", "MK", "FP"], rows))

    # --- oracle assignments (r1)
    r1 = load_runs("r1_fewrow_oracle")
    oa = []
    for d in fps:
        s = S[d]
        if s["util"] == "70" and s["design"] in ("jpeg", "swerv_no_macro", "cva6_no_macro") and s["gpl"] and r1[d].get("oA"):
            h = legal_mean(r1[d]["oA"])
            if h and s["RB (TP)/gpl"]:
                oa.append((h / s["gpl"], s["RB (TP)/gpl"]))
    if oa:
        P.append("### \"Does legalization explain the advantage?\": RePlAce-derived assignments (`r1_fewrow_oracle`, arm `oA`)\n")
        P.append(f"On the {len(oa)} converged few-row floorplans of jpeg, swerv and cva6 at 70 % utilization, RePlAce's "
                 f"cell-to-region assignments with our whitespace allocation and leaf placement give HPWL / RePlAce = "
                 f"{fmt(gm([a for a, _ in oa]))} ({pct(gm([a for a, _ in oa]))}), against {fmt(gm([b for _, b in oa]))} "
                 f"({pct(gm([b for _, b in oa]))}) for RB (TP) on the same floorplans.\n")
    dpl_cost = [float(r["hpwl_dpl"]) / float(r["hpwl_global"]) - 1 for d in fps for r in x3[d].get("gpl", [])
                if S[d]["gpl"] and r.get("hpwl_dpl") and r.get("hpwl_global")]
    if dpl_cost:
        q = statistics.quantiles(dpl_cost, n=10)
        P.append(f"Legalization (detailed placement) changes RePlAce's HPWL by a median {100 * statistics.median(dpl_cost):+.1f} % "
                 f"on the converged few-row floorplans (10th–90th percentile {100 * q[0]:+.1f} % to {100 * q[-1]:+.1f} %, "
                 f"extremes {100 * min(dpl_cost):+.1f} % and {100 * max(dpl_cost):+.1f} %).\n")

    # --- Fig. few-row
    P.append("### Fig. few-row: HPWL / RePlAce by design and PDK (RB (TP) and RB+k (TP), each on its common floorplans)\n")
    rows = []
    for dn in FEW_DESIGNS:
        for pdk in ("nangate45", "asap7"):
            ss = [s for s in ok if s["design"] == dn and s["pdk"] == pdk]
            c = [f"{dn} / {pdk}", cells_of(e17, next(s["name"] for s in S.values() if s["design"] == dn and s["pdk"] == pdk))]
            for l in ("RB (TP)", "RB+k (TP)", "Capo"):
                c.append(cell([(s["name"], s[l + "/gpl"]) for s in ss]))
            rows.append(c)
    P.append(table(["design / PDK", "cells", "RB (TP)", "RB+k (TP)", "Capo"], rows))
    P.append("### HPWL / RePlAce by design and row count, RB (TP)\n")
    rows = []
    for dn in FEW_DESIGNS:
        c = [dn]
        for r in (1, 2, 4, 6, 8):
            xs = [s["RB (TP)/gpl"] for s in ok if s["design"] == dn and s["rows"] == r and s["RB (TP)/gpl"]]
            c.append(f"{fmt(gm(xs))} ({len(xs)})" if xs else "– (0)")
        rows.append(c)
    P.append(table(["design", "1 row", "2 rows", "4 rows", "6 rows", "8 rows"], rows))
    P.append("(n) = converged RePlAce floorplans out of 6 (2 PDKs x 3 utilizations).\n")
    return S


# ------------------------------------------------------------------ sweep
def sweep(P):
    runs = load_runs("x4_ar_sweep")
    conv, conv_all = load_conv("x4_ar_sweep")
    fps = sorted(d for d in runs if SWEEP.match(d))
    assert len(fps) == 182, len(fps)
    S = {}
    for d in fps:
        m = SWEEP.match(d).groupdict()
        g = runs[d].get("gpl", [])
        gpl = legal_mean(g) if (len(g) == 3 and conv_all.get(d, False)) else None
        st = dict(name=d, ar=m["ar"].replace("p", "."), design=m["design"], util=m["util"], pdk=m["pdk"], gpl=gpl)
        for l, v in (("RB (TP)", "cutnet_tp"), ("RB+k (TP)", "kway_tp")):
            h = legal_mean(runs[d].get(v, []))
            st[l] = h / gpl if (h and gpl) else None
        S[d] = st
    ok = [s for s in S.values() if s["gpl"]]
    P.append("## 3. Aspect-ratio sweep (Sec. \"Aspect-Ratio Sweep -- The Crossover\", Fig. axis, Fig. sweep)\n")
    P.append(f"182 floorplans, 3 seeds per configuration; RePlAce converges and legalizes on all three seeds on "
             f"**{len(ok)}** of them (`x4_ar_sweep/replace_convergence.csv`). Ratios: seed means, RePlAce-converged "
             f"floorplans only.\n")
    ARS = ["0.03", "0.06", "0.09", "1"]
    P.append("### By aspect ratio (Fig. axis, sweep part)\n")
    rows = []
    for a in ARS + [None]:
        ss = [s for s in ok if a is None or s["ar"] == a]
        rows.append([a or "all", len(ss), cell([(s["name"], s["RB (TP)"]) for s in ss]), cell([(s["name"], s["RB+k (TP)"]) for s in ss]),
                     cell([(s["name"], s["RB+k (TP)"]) for s in ss if s["design"] != "xiangshan"])])
    P.append(table(["AR", "n", "RB (TP)", "RB+k (TP)", "RB+k (TP) excluding xiangshan"], rows))
    xs = [s for s in ok if s["design"] == "xiangshan" and s["pdk"] == "asap7" and s["ar"] != "1"]
    if xs:
        r = [1 / s["RB+k (TP)"] for s in xs if s["RB+k (TP)"]]
        P.append(f"XiangShan / ASAP7 at AR <= 0.09: RePlAce legalizes but its HPWL is {min(r):.1f}–{max(r):.1f}x that of RB+k (TP) "
                 f"({len(xs)} floorplans).\n")
    P.append("### Fig. sweep: RB+k (TP) / RePlAce by design and aspect ratio (both PDKs, three utilizations)\n")
    rows = []
    for dn in SWEEP_DESIGNS:
        c = [dn]
        for a in ARS:
            ss = [s for s in ok if s["design"] == dn and s["ar"] == a]
            c.append(cell([(s["name"], s["RB+k (TP)"]) for s in ss]))
        rows.append(c)
    P.append(table(["design", *[f"AR {a}" for a in ARS]], rows))
    P.append("### RB+k (TP) / RePlAce by utilization and aspect ratio\n")
    rows = []
    for u in ("60", "70", "80"):
        c = [f"{u} %"]
        for a in ARS + [None]:
            ss = [s for s in ok if s["util"] == u and (a is None or s["ar"] == a)]
            c.append(cell([(s["name"], s["RB+k (TP)"]) for s in ss]))
        rows.append(c)
    P.append(table(["utilization", *[f"AR {a}" for a in ARS], "all"], rows))
    P.append("Seed noise of these runs: [9_seed_noise.md](9_seed_noise.md).\n")


# ------------------------------------------------------------------ square
def square(P):
    x3, e23 = load_runs("x3_mega"), load_runs("e23_base")
    conv, _ = load_conv("e23_base")
    eng = load_engine("e23_base")
    fps = sorted(d for d in e23 if SQ.match(d))
    assert len(fps) == 20, len(fps)
    S = {}
    for d in fps:
        m = SQ.match(d).groupdict()
        ok, cr = conv.get((d, "1"), (False, {}))
        gpl = legal_mean(e23[d].get("gpl")) if ok else None
        st = dict(name=d, design=m["design"], pdk=m["pdk"], gpl=gpl, cells=cells_of(e23, d))
        for label, src, var in ENGINES:
            rows = (e23 if src == "e17" else x3)[d].get(var, [])
            h = legal_mean(rows)
            if var in ("tp", "hm", "mk", "fp") and eng[d].get(var, {}).get("verdict") == "fail":
                h = None
            st[label + "/gpl"] = h / gpl if (h and gpl) else None
        S[d] = st
    labels = [l for l, _, _ in ENGINES]
    ok = [s for s in S.values() if s["gpl"]]
    P.append("## 4. Square floorplans (Sec. \"Square Floorplans -- The Gap\", Table II, Fig. square)\n")
    P.append(f"RePlAce converges and legalizes on {len(ok)} of 20 (`e23_base/replace_convergence.csv`). ")
    fails = {l: sorted(s["name"] for s in ok if not s[l + "/gpl"]) for l in labels}
    P.append("Configurations without a valid result on a RePlAce-converged floorplan: " +
             "; ".join(f"{l}: {', '.join(v)}" for l, v in fails.items() if v) + ".\n")
    for l in ("RB (TP)", "RB+k (TP)", "Capo"):
        xs = [(s["name"], s[l + "/gpl"]) for s in ok if s[l + "/gpl"]]
        P.append(f"* {l} / RePlAce on its {len(xs)} common floorplans: {cell(xs)} ({pct(gm([x for _, x in xs]))}).")
    common = [s for s in ok if all(s[l + "/gpl"] for l in labels)]
    P.append(f"\n### Table II, square row: the {len(common)} floorplans common to all configurations\n")
    P.append(table(["Floorplans", "n", *labels],
                   [["Square", f"{len(common)}/20"] + [cell([(s["name"], s[l + "/gpl"]) for s in common]) for l in labels]]))
    eng_common = [d for d in fps if all(eng[d].get(v, {}).get("verdict") in ("ok", "kept") for v in ("tp", "hm", "mk", "fp"))]
    rows = []
    for key, name in (("root_cut", "Square, root"), ("total_cut", "Square, total")):
        c = [name, f"{len(eng_common)}/20"]
        for v in ("tp", "hm", "mk", "fp"):
            c.append(fmt(gm([float(eng[d][v][key]) / float(eng[d]["tp"][key]) for d in eng_common]), 2))
        rows.append(c)
    P.append(table(["", "n", "TP", "HM", "MK", "FP"], rows))
    P.append("### Fig. square: HPWL / RePlAce per floorplan\n")
    rows = []
    for dn in SQ_DESIGNS:
        for pdk in ("nangate45", "asap7"):
            s = S.get(f"{dn}_ar1_{pdk}")
            if not s:
                continue
            rows.append([s["name"], s["cells"] or "", "yes" if s["gpl"] else "no (RePlAce failed)"]
                        + [fmt(s[l + "/gpl"]) for l in ("Capo", "RB (TP)", "RB (HM)", "RB (MK)", "RB (FP)", "RB+k (TP)")])
    P.append(table(["floorplan", "cells", "RePlAce converged", "Capo", "RB (TP)", "RB (HM)", "RB (MK)", "RB (FP)", "RB+k (TP)"], rows))
    lg = [float(r["hpwl_dpl"]) / float(r["hpwl_global"]) - 1 for d in fps for r in e23[d].get("gpl", []) if S[d]["gpl"] and r.get("hpwl_dpl") and r.get("hpwl_global")]
    lt = [float(r["hpwl_dpl"]) / float(r["hpwl_global"]) - 1 for d in fps for r in e23[d].get("tp", []) if r.get("hpwl_dpl") and r.get("hpwl_global")]
    if lg and lt:
        P.append(f"Legalization (detailed placement) increases HPWL by a median {100 * statistics.median(lg):.1f} % for RePlAce "
                 f"and {100 * statistics.median(lt):.2f} % for RB (TP) on these floorplans.\n")
    P.append("Routed wirelength on the square set: [7_global_routing.md](7_global_routing.md).\n")
    return S


# ------------------------------------------------------------------ ablation
def ablation(P, few, sq):
    x3 = load_runs("x3_mega")
    e26 = load_runs("e26b_norevisit")
    r5 = load_runs("r5_ablation_square")
    rungs = [("a0_base", "Basic cut-net bisection"), ("a1_waves", "+ Two-wave processing"),
             ("a2_minlocal", "+ Connectivity-guided whitespace"), ("a3_endgame", "+ Small-region search, row ironing"),
             ("a4_positions", "+ Optimal in-row positions")]
    few_ok = [d for d, s in few.items() if s["gpl"]]
    sq_ok = sorted(d for d in r5 if sq.get(d, {}).get("gpl"))
    P.append("## 5. Component ablation (Table \"Cumulative component ablation\")\n")
    P.append(f"TritonPart throughout. Few-row column: `e26b_norevisit` rungs on the {len(few_ok)} RePlAce-converged few-row "
             f"floorplans, then `x3_mega` `cutnet_tp` (= RB) and `kway_tp` (= RB+k). Square column: `r5_ablation_square` on the "
             f"{len(sq_ok)} macro-free square floorplans ({', '.join(sq_ok)}), then `x3_mega` on the same floorplans. "
             "HPWL after detailed placement / RePlAce, geometric mean.\n")
    rows = []
    for var, label in rungs:
        a = [legal_mean(e26[d].get(var, [])) / few[d]["gpl"] for d in few_ok if legal_mean(e26[d].get(var, []))]
        b = [legal_mean(r5[d].get(var, [])) / sq[d]["gpl"] for d in sq_ok if legal_mean(r5[d].get(var, []))]
        rows.append([label, f"{fmt(gm(a))} ({len(a)})", f"{fmt(gm(b))} ({len(b)})"])
    for var, label in (("cutnet_tp", "+ Leaf revisiting (RB)"), ("kway_tp", "+ Level-wise reassignment (RB+k)")):
        a = [legal_mean(x3[d].get(var, [])) / few[d]["gpl"] for d in few_ok if legal_mean(x3[d].get(var, []))]
        b = [legal_mean(x3[d].get(var, [])) / sq[d]["gpl"] for d in sq_ok if legal_mean(x3[d].get(var, []))]
        rows.append([label, f"{fmt(gm(a))} ({len(a)})", f"{fmt(gm(b))} ({len(b)})"])
    P.append(table(["Components enabled", "Few-row (n)", "Square (n)"], rows))
    # runtime
    tr = []
    for d in few_ok:
        g, t, k = (legal_mean(x3[d].get(v, []), "t_place") for v in ("gpl", "cutnet_tp", "kway_tp"))
        if g and t and k:
            tr.append((t / g, k / g, k / t))
    if tr:
        P.append(f"Runtime on the converged few-row floorplans (placer wall-clock / RePlAce, median): RB (TP) "
                 f"{statistics.median(t for t, _, _ in tr):.1f}x, RB+k (TP) {statistics.median(k for _, k, _ in tr):.1f}x; "
                 f"level-wise reassignment adds a median {100 * (statistics.median(r for _, _, r in tr) - 1):.0f} % to RB's time. "
                 "Where the time goes (partitioning, leaf optimization, ironing) is in the per-run reports "
                 "(`decompose_seconds` and the level table of `*.mcp.json`), see table 8.\n")
    P.append("Attribution against Capo (leave-one-out, partitioner at the base rung): [8_capo_attribution.md](8_capo_attribution.md).\n")


# ------------------------------------------------------------------ held-out designs
def heldout(P):
    runs = load_runs("x5_heldout")
    if not runs:
        return
    conv, conv_all = load_conv("x5_heldout")
    P.append("## 6. Held-out designs (footnote of the benchmark section; `x5_heldout`)\n")
    P.append("Seven designs not used while developing the placer (b19, ethernet, fft_256, jpegencode, tate_151, tate_pairing, "
             "vga_lcd), both PDKs, few-row floorplans ({60, 70, 80} % x {1, 2, 4, 6, 8} rows) and a square floorplan at 60 %, "
             "seed 1, RePlAce against RB+k (TP); same failure rule for RePlAce as above (`x5_heldout/replace_convergence.csv`).\n")
    S = []
    nonconv = []
    for d in runs:
        m = FEW.match(d) or SQ.match(d)
        if not m:
            continue
        g, k = legal_mean(runs[d].get("gpl", [])), legal_mean(runs[d].get("kway_tp", []))
        if g and conv and not conv_all.get(d, False):
            nonconv.append(d)
            g = None
        S.append(dict(name=d, design=m.group("design"), pdk=m.group("pdk"), rows=int(m.groupdict().get("rows") or 0),
                      gpl=g, kway=k, ratio=k / g if (g and k) else None))
    few = [s for s in S if s["rows"]]
    sqs = [s for s in S if not s["rows"]]
    P.append(f"Few-row: {len(few)} floorplans; RePlAce succeeds on {sum(1 for s in few if s['gpl'])}, RB+k (TP) on "
             f"{sum(1 for s in few if s['kway'])}. Square: {len(sqs)} floorplans; RePlAce succeeds on {sum(1 for s in sqs if s['gpl'])}, "
             f"RB+k (TP) on {sum(1 for s in sqs if s['kway'])}." +
             (f" RePlAce legalized but stopped above the overflow target on {len(nonconv)} floorplans ({', '.join(sorted(nonconv))}); "
              "they are excluded." if nonconv else "") + "\n")
    rows = []
    for r in (1, 2, 4, 6, 8):
        ss = [s for s in few if s["rows"] == r]
        rows.append([f"{r} row{'s' if r > 1 else ''}", f"{sum(1 for s in ss if s['ratio'])}/{len(ss)}", cell([(s["name"], s["ratio"]) for s in ss])])
    rows.append(["All few-row", f"{sum(1 for s in few if s['ratio'])}/{len(few)}", cell([(s["name"], s["ratio"]) for s in few])])
    rows.append(["Square", f"{sum(1 for s in sqs if s['ratio'])}/{len(sqs)}", cell([(s["name"], s["ratio"]) for s in sqs])])
    P.append(table(["Floorplans", "n", "RB+k (TP) / RePlAce"], rows))
    rows = []
    for dn in X5_DESIGNS:
        c = [dn]
        for pdk in ("nangate45", "asap7"):
            ss = [s for s in few if s["design"] == dn and s["pdk"] == pdk]
            c.append(cells_of(runs, ss[0]["name"]) if ss else "")
            c.append(cell([(s["name"], s["ratio"]) for s in ss]))
            q = [s for s in sqs if s["design"] == dn and s["pdk"] == pdk]
            c.append(fmt(q[0]["ratio"]) if q else "–")
        rows.append(c)
    P.append(table(["design", "cells NG45", "few-row NG45", "square NG45", "cells ASAP7", "few-row ASAP7", "square ASAP7"], rows))


# ------------------------------------------------------------------ main
def main():
    P = ["# 0. The paper's tables and figures, recomputed from the CSVs in this folder\n",
         "Generated by `python3 exp/paper_numbers.py` from `runs.csv`, `replace_convergence.csv` and `rb_engine_reports.csv` "
         "of the experiment folders named in each section. Rules, as in the paper's \"Evaluation\" paragraph:\n",
         "* Metric: HPWL after detailed placement, before DPO (`hpwl_dpl`), mean over seeds per floorplan, ratio to RePlAce on "
         "the same floorplan, geometric mean over floorplans; (wins/n) = floorplans where the placer beats RePlAce / "
         "floorplans compared.",
         "* A RePlAce run fails if it diverges, does not legalize, or stops above its 20 % overflow target (`converged` = 0 in "
         "`replace_convergence.csv`). An RB run fails if its partitioner returned no partition at the root bisection or on more "
         "than 1 % of all bisections (`verdict` = `fail` in `rb_engine_reports.csv`), or if it produced no legal placement.",
         "* Every ratio is over the floorplans *common* to the configurations compared (none of them fails on it).\n",
         "| paper item | section here |", "|---|---|",
         "| Table I (benchmark) | 1 |",
         "| Table II (engines; HPWL and cut rows) | 2 (few-row rows), 4 (square row) |",
         "| Table III (component ablation) | 5 |",
         "| Fig. 1 (HPWL across geometries) | 2 (by row count), 3 (by aspect ratio), 4 (square) |",
         "| Fig. few-row (by design and PDK) | 2 |",
         "| Fig. sweep (by design and aspect ratio) | 3 |",
         "| Fig. square (per floorplan) | 4 |",
         "| Routed wirelength, routing overflow | [7_global_routing.md](7_global_routing.md) |",
         "| Capo comparison, attribution | 2, 4, [8_capo_attribution.md](8_capo_attribution.md) |",
         "| Seed noise (footnote) | [9_seed_noise.md](9_seed_noise.md) |",
         "| Held-out designs (footnote) | 6 |",
         "| Hypotheses H1–H3, Fig. anatomy | `../i1_info/STATUS.md`, `../am1/`, [6_anatomy_of_gap.md](6_anatomy_of_gap.md) "
         "(the per-level cut statistics need the per-run reports, which are not exported) |",
         "",
         "## 1. Benchmark (Table I)\n"]
    x3, e17, e23, x4 = load_runs("x3_mega"), load_runs("e17_krow"), load_runs("e23_base"), load_runs("x4_ar_sweep")
    rows = []
    for dn, few_name, sq_name, sw_name in (("gcd", "gcd", "gcd", "gcd"), ("aes", "aes", "aes", "aes"), ("ibex", "ibex", "ibex", "ibex"),
                                           ("jpeg", "jpeg", "jpeg", "jpeg"), ("swerv", "swerv_no_macro", "swerv_wrapper", "swerv"),
                                           ("cva6", "cva6_no_macro", "cva6", "cva6"), ("mempool_tile", None, "mempool_tile", "mempool_tile"),
                                           ("xiangshan", None, "xiangshan", "xiangshan"), ("megaboom", None, "megaboom", None),
                                           ("mempool_group", None, "mempool_group", None)):
        c = [dn]
        for pdk in ("nangate45", "asap7"):
            sets = []
            n = None
            if few_name:
                d = next((d for d in e17 if FEW.match(d) and FEW.match(d)["design"] == few_name and d.endswith(pdk)), None)
                if d:
                    sets.append("F")
                    n = n or cells_of(e17, d)
            if sw_name:
                d = next((d for d in x4 if SWEEP.match(d) and SWEEP.match(d)["design"] == sw_name and d.endswith(pdk)), None)
                if d:
                    sets.append("A")
                    n = n or cells_of(x4, d)
            if sq_name and f"{sq_name}_ar1_{pdk}" in e23:
                sets.append("S")
                n = n or cells_of(e23, f"{sq_name}_ar1_{pdk}")
            c += [n or "", "".join(sets)]
        rows.append(c)
    P.append(table(["design", "cells NG45", "sets", "cells ASAP7", "sets"], rows))
    P.append("F = few-row (180 floorplans: 6 designs x 2 PDKs x {60, 70, 80} % x {1, 2, 4, 6, 8} rows; swerv and cva6 without "
             "macros), A = aspect-ratio sweep (182 floorplans: 8 designs x 2 PDKs x AR {0.03, 0.06, 0.09, 1} x {60, 70, 80} %, "
             "macro-free), S = square (20 floorplans: 10 designs x 2 PDKs at the flow's utilization, macros kept where the design "
             "has them). Cell counts are movable cells of the few-row / sweep checkpoints where the design is in those sets, "
             "else of the square checkpoint.\n")
    few = few_row(P)
    sweep(P)
    sq = square(P)
    ablation(P, few, sq)
    heldout(P)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    open(OUT, "w").write("\n".join(P) + "\n")
    print("wrote", OUT)


if __name__ == "__main__":
    main()
