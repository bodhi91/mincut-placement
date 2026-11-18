#!/usr/bin/env python3
"""The six result tables for the paper, from paper_results/<exp>/runs.csv.

    exp/paper_export.py            # first: builds paper_results/
    exp/paper_tables.py            # then: paper_results/tables/*.md

  1_few_rows.md          min-cut placers vs gpl on the 1-8-row floorplans (E17, X3 k-way)
  2_ar1_designs.md       min-cut placers vs gpl on the AR-1 floorplans (E23, X3 k-way, X4 AR 1)
  3_ar_util_sweep.md     the aspect-ratio x utilization DOE (X4)
  4_capo.md              Capo against gpl and against the min-cut placers (E14, E16, E17, E23, AM1)
  5_american_maps.md     the American-map study (AM1)
  6_anatomy_of_gap.md    where the from-scratch gap comes from (E8, E12, progression ladder,
                         hybridization spectrum, I1, legalization cost)

Metric, unless a table says otherwise: HPWL after detailed placement and
before DPO (`hpwl_dpl`; = `hpwl` for grids that ran without DPO), ratio to
gpl on the same floorplan, seed means per floorplan, geometric mean over
floorplans, "wins" = floorplans where the placer's HPWL is below gpl's.
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
OUT = os.path.join(PR, "tables")
NAMES = {"gpl": "gpl (RePlAce)", "capo": "Capo", "tp": "mcp-TritonPart", "hm": "mcp-hMETIS",
         "mk": "mcp-Mt-KaHyPar", "fp": "mcp-FastPart", "cutnet_tp": "mcp cutnet", "kway_tp": "mcp k-way"}
KROW = re.compile(r"^(?P<design>.+)_u(?P<util>\d+)_r(?P<rows>\d+)_(?P<platform>asap7|nangate45)$")
AR1 = re.compile(r"^(?P<design>.+)_ar1_(?P<platform>asap7|nangate45)$")
DESIGN_ORDER = ["gcd", "aes", "ibex", "jpeg", "swerv", "swerv_no_macro", "swerv_wrapper", "cva6", "cva6_no_macro",
                "mempool_tile", "xiangshan", "megaboom", "mempool_group", "tinyRocket"]
# FastPart runs whose engine never started (Mt-KaHyPar module gone from /tmp): FM fallback, not a FastPart result
FP_INVALID = {"xiangshan_ar1_nangate45", "megaboom_ar1_asap7", "megaboom_ar1_nangate45", "mempool_group_ar1_asap7",
              "mempool_group_ar1_nangate45"}


def f(x):
    return None if x in ("", None) else float(x)


def load(exp):
    p = os.path.join(PR, exp, "runs.csv")
    if not os.path.exists(p):
        return []
    rows = []
    for r in csv.DictReader(open(p)):
        for k in ("hpwl", "hpwl_global", "hpwl_dpl", "grt_wl", "t_place", "t_grt", "mem_peak_mb"):
            r[k] = f(r.get(k))
        r["seed"] = int(r["seed"]) if r["seed"] else 0
        r["m"] = r["hpwl_dpl"] if r["hpwl_dpl"] else r["hpwl"]   # primary metric
        rows.append(r)
    return rows


def dorder(d):
    for i, k in enumerate(DESIGN_ORDER):
        if d.startswith(k):
            return i, d
    return 99, d


def gm(xs):
    xs = [x for x in xs if x and x > 0]
    return math.exp(sum(math.log(x) for x in xs) / len(xs)) if xs else None


def s3(x):
    return "–" if x is None else f"{x:.3f}"


def per_fp(rows, metric="m"):
    """{design: {variant: mean over seeds}}"""
    acc = defaultdict(lambda: defaultdict(list))
    for r in rows:
        if r[metric]:
            acc[r["design"]][r["variant"]].append(r[metric])
    return {d: {v: statistics.mean(x) for v, x in vs.items()} for d, vs in acc.items()}


def ratios(fp, variants, ref="gpl", exclude=None):
    """{variant: [(design, ratio)]} vs ref, designs where both exist."""
    out = defaultdict(list)
    for d, vs in fp.items():
        if ref not in vs:
            continue
        for v in variants:
            if v in vs and not (exclude and (d, v) in exclude):
                out[v].append((d, vs[v] / vs[ref]))
    return out


def cell(pairs, sel=None):
    vals = [x for d, x in pairs if sel is None or sel(d)]
    if not vals:
        return "–"
    return f"{gm(vals):.3f} ({sum(v < 1 for v in vals)}/{len(vals)})"


def table(hdr, rows):
    out = ["| " + " | ".join(hdr) + " |", "|" + "---|" * len(hdr)]
    out += ["| " + " | ".join(str(c) for c in r) + " |" for r in rows]
    return "\n".join(out) + "\n"


def write(name, lines):
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, name), "w") as fh:
        fh.write("\n".join(lines) + "\n")
    print("wrote", os.path.join(OUT, name))


# ------------------------------------------------------------------ 1. few rows
def table_few_rows():
    L = ["# 1. Min-cut placers against gpl on few-row floorplans\n",
         "Source: E17 (`e17_krow`), 180 floorplans = {gcd, aes, ibex, jpeg, swerv_no_macro, cva6_no_macro} × "
         "{nangate45, asap7} × utilization {60, 70, 80 %} × {1, 2, 4, 6, 8} rows, one seed, no DPO. "
         "gpl = `global_placement -disable_pin_density_adjust -overflow 0.2` (the floorplan author's baseline; "
         "reproduces his HPWL within 0.5 %). mcp options `-k 2 -reserve minlocal -density <flow> -max_tolerance 1.0 "
         "-pad 0 -optimal_cells 12 -feedback_passes 1`, 4 threads. Metric: HPWL after detailed placement, "
         "ratio to gpl on the same floorplan, geomean over floorplans (wins/n). Capo is in table 4.\n"]
    wide = list(csv.DictReader(open(os.path.join(PR, "e17_krow", "e17_krow_results.csv"))))
    placers = ["tp", "hm", "mk", "fp"]
    ok = [r for r in wide if r["gpl_status"] == "ok" or f(r["gpl_hpwl_um"])]
    L.append(f"## 1.1 Coverage\n")
    fails = defaultdict(int)
    for r in wide:
        if not f(r["gpl_hpwl_um"]):
            fails[r["gpl_status"]] += 1
    L.append(f"gpl produced a legal placement on {len(ok)} / 180 floorplans; failures: " +
             ", ".join(f"{k} ×{v}" for k, v in sorted(fails.items())) +
             ". Every mcp arm placed all 180 (legal, 0 violations). Ratios below are over the "
             f"{len(ok)} floorplans with a gpl result.\n")

    def rat(r, p):
        a, g = f(r[f"{p}_hpwl_um"]), f(r["gpl_hpwl_um"])
        return a / g if a and g else None

    def block(sel, label):
        rs = [r for r in ok if sel(r)]
        cells = [label, len(rs)]
        for p in placers:
            v = [rat(r, p) for r in rs]
            v = [x for x in v if x]
            cells.append(f"{gm(v):.3f} ({sum(x < 1 for x in v)}/{len(v)})" if v else "–")
        return cells

    hdr = ["subset", "n"] + [NAMES[p] for p in placers]
    L.append("## 1.2 Overall and by number of rows\n")
    rows = [block(lambda r: True, "all")]
    for k in ["1", "2", "4", "6", "8"]:
        rows.append(block(lambda r, k=k: r["rows"] == k, f"{k} row{'s' if k != '1' else ''}"))
    L.append(table(hdr, rows))
    L.append("## 1.3 By design\n")
    rows = []
    for d in ["gcd", "aes", "ibex", "jpeg", "swerv_no_macro", "cva6_no_macro"]:
        cells_ = sorted({r["cells"] for r in wide if r["design"] == d and r["cells"]})
        rows.append(block(lambda r, d=d: r["design"] == d, f"{d} ({'/'.join(cells_)} cells)"))
    L.append(table(hdr, rows))
    L.append("## 1.4 By PDK and by utilization\n")
    rows = [block(lambda r, p=p: r["platform"] == p, p) for p in ["nangate45", "asap7"]]
    rows += [block(lambda r, u=u: r["util_pct"] == u, f"util {u} %") for u in ["60", "70", "80"]]
    L.append(table(hdr, rows))

    L.append("## 1.5 Design × rows (mcp-TritonPart / gpl)\n")
    hdr2 = ["design", "1 row", "2 rows", "4 rows", "6 rows", "8 rows"]
    rows = []
    for d in ["gcd", "aes", "ibex", "jpeg", "swerv_no_macro", "cva6_no_macro"]:
        c = [d]
        for k in ["1", "2", "4", "6", "8"]:
            v = [rat(r, "tp") for r in ok if r["design"] == d and r["rows"] == k]
            v = [x for x in v if x]
            c.append(f"{gm(v):.3f} ({len(v)})" if v else "– (gpl failed)")
        rows.append(c)
    L.append(table(hdr2, rows))
    L.append("(n in parentheses = floorplans with a gpl result, out of 6 per cell: 2 PDKs × 3 utilizations.)\n")

    L.append("## 1.6 Routed wirelength and runtime\n")
    rows = []
    for p in placers:
        wl = [f(r[f"{p}_grt_wl_um"]) / f(r["gpl_grt_wl_um"]) for r in ok
              if f(r[f"{p}_grt_wl_um"]) and f(r["gpl_grt_wl_um"])]
        t = [f(r[f"{p}_t_place_s"]) / f(r["gpl_t_place_s"]) for r in ok
             if f(r[f"{p}_t_place_s"]) and f(r["gpl_t_place_s"])]
        rows.append([NAMES[p], f"{gm(wl):.3f} ({sum(x < 1 for x in wl)}/{len(wl)})" if wl else "–",
                     f"{statistics.median(t):.1f}×" if t else "–"])
    L.append(table(["placer", "routed WL / gpl (wins/n, both routed)", "placer time / gpl (median)"], rows))

    # k-way on the k-row set (X3)
    x3 = [r for r in load("x3_mega") if KROW.match(r["design"]) and r["variant"] in ("gpl", "cutnet_tp", "kway_tp")]
    if x3:
        fp3 = per_fp(x3)
        rr = ratios(fp3, ["cutnet_tp", "kway_tp"])
        L.append("## 1.7 k-way refinement on the same floorplans (X3)\n")
        L.append("Source: `x3_mega` (the x3_full grid): gpl, mcp cutnet (= E17 `tp` options) and mcp k-way "
                 "(`-kway_refine_levels {2 3 4 5}`), seed 1, on the k-row floorplans that had a gpl result "
                 "(DPO on for the small designs; metric is pre-DPO HPWL).\n")
        hdr3 = ["subset", "n", "mcp cutnet / gpl", "mcp k-way / gpl", "k-way / cutnet"]
        kc = [(d, fp3[d]["kway_tp"] / fp3[d]["cutnet_tp"]) for d in fp3 if "kway_tp" in fp3[d] and "cutnet_tp" in fp3[d]]
        rows = [["all", len(rr["kway_tp"]), cell(rr["cutnet_tp"]), cell(rr["kway_tp"]), cell(kc)]]
        for k in ["1", "2", "4", "6", "8"]:
            sel = lambda d, k=k: KROW.match(d).group("rows") == k
            rows.append([f"{k} row{'s' if k != '1' else ''}", len([1 for d, _ in rr["kway_tp"] if sel(d)]),
                         cell(rr["cutnet_tp"], sel), cell(rr["kway_tp"], sel), cell(kc, sel)])
        for dn in ["gcd", "aes", "ibex", "jpeg", "swerv_no_macro", "cva6_no_macro"]:
            sel = lambda d, dn=dn: KROW.match(d).group("design") == dn
            n = len([1 for d, _ in rr["kway_tp"] if sel(d)])
            if n:
                rows.append([dn, n, cell(rr["cutnet_tp"], sel), cell(rr["kway_tp"], sel), cell(kc, sel)])
        L.append(table(hdr3, rows))
    write("1_few_rows.md", L)


# ------------------------------------------------------------------ 2. AR 1
def table_ar1():
    L = ["# 2. Min-cut placers against gpl on the aspect-ratio-1 floorplans\n",
         "Source: E23 (`e23_base`): the ORFS designs re-floorplanned square (AR 1) at their flow utilization, "
         "macros kept where the design has them (swerv_wrapper, mempool_tile, mempool_group), one seed, no DPO, "
         "4 threads; gpl in the same krow mode as E17. Metric: HPWL after detailed placement / gpl. "
         "Capo is in table 4.\n"]
    e23 = load("e23_base")
    placers = ["tp", "hm", "mk", "fp"]
    fp = per_fp([r for r in e23 if r["variant"] in placers + ["gpl"]])
    excl = {(d, "fp") for d in FP_INVALID}
    rr = ratios(fp, placers, exclude=excl)
    designs = sorted(fp, key=dorder)
    hdr = ["floorplan", "cells", "gpl HPWL (µm)"] + [NAMES[p] + " / gpl" for p in placers]
    rows = []
    cells_of = {r["design"]: r["cells"] for r in e23 if r["cells"]}
    for d in designs:
        if "gpl" not in fp[d]:
            continue
        c = [d, cells_of.get(d, ""), f"{fp[d]['gpl']:,.0f}"]
        for p in placers:
            if (d, p) in excl:
                c.append("invalid¹")
            elif p in fp[d]:
                c.append(f"{fp[d][p] / fp[d]['gpl']:.3f}")
            else:
                c.append("running" if d.startswith(("megaboom", "mempool_group")) else "–")
        rows.append(c)
    L.append("## 2.1 Per floorplan\n")
    L.append(table(hdr, rows))
    L.append("¹ FastPart's engine process could not start on these runs (its Mt-KaHyPar module had been removed "
             "from `/tmp`); every region fell back to FM. Not a FastPart result; excluded from the geomeans.\n")
    L.append("## 2.2 Geomeans\n")
    small = lambda d: int(cells_of.get(d, 0) or 0) < 200000
    rows = [["all", len(rr["tp"])] + [cell(rr[p]) for p in placers],
            ["< 200 k cells", len([1 for d, _ in rr["tp"] if small(d)])] + [cell(rr[p], small) for p in placers],
            ["≥ 200 k cells (xiangshan, megaboom, mempool_group)", len([1 for d, _ in rr["tp"] if not small(d)])]
            + [cell(rr[p], lambda d: not small(d)) for p in placers],
            ["nangate45", len([1 for d, _ in rr["tp"] if d.endswith("nangate45")])]
            + [cell(rr[p], lambda d: d.endswith("nangate45")) for p in placers],
            ["asap7", len([1 for d, _ in rr["tp"] if d.endswith("asap7")])]
            + [cell(rr[p], lambda d: d.endswith("asap7")) for p in placers]]
    L.append(table(["subset", "n"] + [NAMES[p] + " / gpl" for p in placers], rows))
    # runtime
    tfp = per_fp(e23, "t_place")
    rows = []
    for p in placers:
        t = [tfp[d][p] / tfp[d]["gpl"] for d in tfp if p in tfp[d] and "gpl" in tfp[d] and (d, p) not in excl]
        rows.append([NAMES[p], f"{statistics.median(t):.0f}×" if t else "–", len(t)])
    L.append("## 2.3 Placer time / gpl (median over floorplans)\n")
    L.append(table(["placer", "t_place / gpl", "n"], rows))

    x3 = [r for r in load("x3_mega") if AR1.match(r["design"]) and r["variant"] in ("gpl", "cutnet_tp", "kway_tp")]
    if x3:
        fp3 = per_fp(x3)
        L.append("## 2.4 k-way refinement on the AR-1 floorplans (X3)\n")
        L.append("Source: `x3_mega`, seed 1 (DPO on for the small designs; pre-DPO metric). Same gpl mode.\n")
        rows = []
        for d in sorted(fp3, key=dorder):
            v = fp3[d]
            if "gpl" in v:
                rows.append([d, s3(v.get("cutnet_tp") and v["cutnet_tp"] / v["gpl"]),
                             s3(v.get("kway_tp") and v["kway_tp"] / v["gpl"]),
                             s3(v.get("kway_tp") and v.get("cutnet_tp") and v["kway_tp"] / v["cutnet_tp"])])
        rr3 = ratios(fp3, ["cutnet_tp", "kway_tp"])
        kc = [(d, fp3[d]["kway_tp"] / fp3[d]["cutnet_tp"]) for d in fp3 if "kway_tp" in fp3[d] and "cutnet_tp" in fp3[d]]
        rows.append(["**geomean (wins/n)**", cell(rr3["cutnet_tp"]), cell(rr3["kway_tp"]), cell(kc)])
        L.append(table(["floorplan", "mcp cutnet / gpl", "mcp k-way / gpl", "k-way / cutnet"], rows))

    x4 = list(csv.DictReader(open(os.path.join(PR, "x4_ar_sweep", "x4_floorplans.csv"))))
    ar1 = [r for r in x4 if r["ar_tag"] == "1" and f(r["kway_tp_vs_gpl"])]
    if ar1:
        L.append("## 2.5 AR 1 in the floorplan author's DOE (X4): mcp k-way / gpl by design and utilization\n")
        L.append("Source: `x4_ar_sweep`, AR 1 configurations, macro-free (`REMOVE_MACROS=1`), 3 seeds, both PDKs. "
                 "Pre-DPO metric.\n")
        hdr = ["design", "util 60", "util 70", "util 80", "all (wins/n)"]
        rows = []
        for d in ["gcd", "aes", "ibex", "jpeg", "swerv", "cva6", "mempool_tile", "xiangshan"]:
            c = [d]
            for u in ["60", "70", "80"]:
                v = [f(r["kway_tp_vs_gpl"]) for r in ar1 if r["design"] == d and r["util"] == u]
                c.append(s3(gm(v)))
            v = [(r["floorplan"], f(r["kway_tp_vs_gpl"])) for r in ar1 if r["design"] == d]
            c.append(cell(v))
            rows.append(c)
        v = [(r["floorplan"], f(r["kway_tp_vs_gpl"])) for r in ar1]
        rows.append(["**all**", s3(gm([f(r["kway_tp_vs_gpl"]) for r in ar1 if r["util"] == "60"])),
                     s3(gm([f(r["kway_tp_vs_gpl"]) for r in ar1 if r["util"] == "70"])),
                     s3(gm([f(r["kway_tp_vs_gpl"]) for r in ar1 if r["util"] == "80"])), cell(v)])
        L.append(table(hdr, rows))
    write("2_ar1_designs.md", L)


# ------------------------------------------------------------------ 3. sweep
def table_sweep():
    x4 = list(csv.DictReader(open(os.path.join(PR, "x4_ar_sweep", "x4_floorplans.csv"))))
    x4 = [r for r in x4 if f(r["gpl_hpwl_um"])]
    SMALL, BIG = ["gcd", "aes", "ibex", "jpeg"], ["swerv", "cva6", "mempool_tile", "xiangshan"]
    ARS, UTILS = ["0p03", "0p06", "0p09", "1"], ["60", "70", "80"]
    arf = lambda a: a.replace("p", ".")
    L = ["# 3. Min-cut placers against gpl on the aspect-ratio × utilization sweep\n",
         "Source: X4 (`x4_ar_sweep`): the floorplan author's DOE, {gcd, aes, ibex, jpeg, swerv, cva6, mempool_tile, xiangshan} × "
         "aspect ratio {0.03, 0.06, 0.09, 1} × utilization {60, 70, 80 %} × {asap7, nangate45}, macro-free, "
         "182 floorplans (10 gcd configurations absent from his tree), 3 seeds. Arms: gpl (krow mode, density = "
         "util + 0.10), mcp cutnet, mcp k-way (`-kway_refine_levels {2 3 4 5}`). Metric: HPWL after detailed "
         "placement (DPO ran on the small set only; pre-DPO throughout), seed means, ratio to gpl, geomean (wins/n). "
         "Full tables: `../x4_ar_sweep/SUMMARY.md`.\n"]

    def pairs(key, sel):
        return [(r["floorplan"], f(r[key])) for r in x4 if f(r[key]) and sel(r)]

    L.append("## 3.1 Overall\n")
    rows = []
    for lab, ds in (("small (gcd aes ibex jpeg)", SMALL), ("big (swerv cva6 mempool_tile xiangshan)", BIG), ("all", SMALL + BIG)):
        sel = lambda r, ds=ds: r["design"] in ds
        rows.append([lab, len(pairs("kway_tp_vs_gpl", sel)), cell(pairs("cutnet_tp_vs_gpl", sel)),
                     cell(pairs("kway_tp_vs_gpl", sel)), cell(pairs("kway_vs_cutnet", sel))])
    outl = lambda r: f(r["kway_tp_vs_gpl"]) and f(r["kway_tp_vs_gpl"]) < 0.7
    sel = lambda r: r["design"] in BIG and not outl(r)
    rows.append(["big, excluding 6 gpl outliers (§3.5)", len(pairs("kway_tp_vs_gpl", sel)),
                 cell(pairs("cutnet_tp_vs_gpl", sel)), cell(pairs("kway_tp_vs_gpl", sel)), cell(pairs("kway_vs_cutnet", sel))])
    L.append(table(["set", "n", "mcp cutnet / gpl", "mcp k-way / gpl", "k-way / cutnet"], rows))

    L.append("## 3.2 mcp k-way / gpl by aspect ratio × utilization\n")
    for lab, ds, drop in (("Small set", SMALL, False), ("Big set", BIG, False), ("Big set, gpl outliers excluded", BIG, True)):
        L.append(f"### {lab}\n")
        rows = []
        for a in ARS:
            c = [arf(a)]
            for u in UTILS + [None]:
                sel = lambda r, a=a, u=u, ds=ds, drop=drop: (r["design"] in ds and r["ar_tag"] == a and (u is None or r["util"] == u)
                                                             and not (drop and outl(r)))
                c.append(cell(pairs("kway_tp_vs_gpl", sel)))
            rows.append(c)
        c = ["all"]
        for u in UTILS + [None]:
            sel = lambda r, u=u, ds=ds, drop=drop: r["design"] in ds and (u is None or r["util"] == u) and not (drop and outl(r))
            c.append(cell(pairs("kway_tp_vs_gpl", sel)))
        rows.append(c)
        L.append(table(["AR \\ util", "60 %", "70 %", "80 %", "all"], rows))

    L.append("## 3.3 mcp k-way / gpl by design × aspect ratio (both PDKs, 3 utilizations)\n")
    rows = []
    for d in SMALL + BIG:
        cells_ = "/".join(str(next((r["cells"] for r in x4 if r["design"] == d and r["pdk"] == p), "")) for p in ["asap7", "nangate45"])
        c = [f"{d} ({cells_})"]
        for a in ARS + [None]:
            c.append(cell(pairs("kway_tp_vs_gpl", lambda r, d=d, a=a: r["design"] == d and (a is None or r["ar_tag"] == a))))
        rows.append(c)
    L.append(table(["design (cells asap7/ng45)"] + [f"AR {arf(a)}" for a in ARS] + ["all"], rows))

    L.append("## 3.4 Rows: the same ratios against core rows\n")
    rows = []
    for lo, hi in [(1, 8), (9, 32), (33, 64), (65, 128), (129, 256), (257, 10**9)]:
        sel = lambda r, lo=lo, hi=hi: r["rows"] and lo <= int(r["rows"]) <= hi
        p = pairs("kway_tp_vs_gpl", sel)
        if p:
            ds = sorted({r["design"] for r in x4 if sel(r)}, key=dorder)
            rows.append([f"{lo}–{hi}" if hi < 10**9 else f"≥ {lo}", len(p), cell(pairs("cutnet_tp_vs_gpl", sel)), cell(p), ", ".join(ds)])
    L.append(table(["rows", "n", "mcp cutnet / gpl", "mcp k-way / gpl", "designs"], rows))

    L.append("## 3.5 gpl outliers, failures, seeds, runtime\n")
    rows = [[r["floorplan"], r["rows"], f"{f(r['gpl_hpwl_um']):,.0f}", f"{f(r['kway_tp_hpwl_um']):,.0f}", s3(f(r["kway_tp_vs_gpl"]))]
            for r in x4 if outl(r)]
    L.append("gpl outliers (k-way / gpl < 0.7): all xiangshan asap7 at AR ≤ 0.09, where RePlAce returns a legal but "
             "badly spread placement (the nangate45 twins are normal). Shown with and without above.\n")
    L.append(table(["floorplan", "rows", "gpl HPWL", "k-way HPWL", "k-way / gpl"], rows))
    L.append("Failures: 3 (gpl on `xiangshan_ar0.09_util80_asap7`, seeds 1–3, DPL-0033 after RePlAce). "
             "All 1 092 mcp runs legal.\n")
    rows = []
    for d in SMALL + BIG:
        sel = [r for r in x4 if r["design"] == d]
        tg = gm([f(r["gpl_t_place_s"]) for r in sel if f(r["gpl_t_place_s"])])
        tk = gm([f(r["kway_tp_t_place_s"]) for r in sel if f(r["kway_tp_t_place_s"])])
        sp = [f(r["kway_tp_spread"]) for r in sel if f(r["kway_tp_spread"]) is not None]
        rows.append([d, f"{tg:.1f}", f"{tk:.0f}", f"{tk / tg:.0f}×", f"{100 * statistics.median(sp):.1f} %" if sp else "–"])
    L.append(table(["design", "gpl t_place (s)", "k-way t_place (s)", "ratio", "k-way seed spread (max/min − 1, median)"], rows))
    L.append("gpl is deterministic (seed has no effect). Legalization costs gpl 6.7 % (big set) / 14.5 % (small set) "
             "of HPWL at the median, mcp 0.01 %; DPO (small set) helps gpl −7.7 % vs mcp −3.1 %, so on post-DPO HPWL "
             "the small-set ratio is 1.072 instead of 1.019.\n")
    write("3_ar_util_sweep.md", L)


# ------------------------------------------------------------------ 4. Capo
def table_capo():
    L = ["# 4. Capo against gpl and against the min-cut placers\n",
         "Capo 10.5 (UMpack `CapoTest1.exe`) through the Bookshelf bridge `bench/capo.tcl`, `-densityTarget` = the "
         "flow's density (percent), cells padded as gpl pads them. Same evaluation as every other arm: detailed "
         "placement, (DPO where the grid had it), HPWL after detailed placement / gpl. Here `mcp` = mcp-TritonPart "
         "(`tp`) unless stated.\n"]
    # E17
    wide = list(csv.DictReader(open(os.path.join(PR, "e17_krow", "e17_krow_results.csv"))))
    ok = [r for r in wide if f(r["gpl_hpwl_um"])]
    L.append("## 4.1 Few-row floorplans (E17, 149 floorplans with a gpl result)\n")

    def row(label, sel):
        rs = [r for r in ok if sel(r)]
        cg = [f(r["capo_hpwl_um"]) / f(r["gpl_hpwl_um"]) for r in rs if f(r["capo_hpwl_um"])]
        ct = [f(r["capo_hpwl_um"]) / f(r["tp_hpwl_um"]) for r in rs if f(r["capo_hpwl_um"]) and f(r["tp_hpwl_um"])]
        best = [f(r["capo_hpwl_um"]) / min(f(r[f"{p}_hpwl_um"]) for p in ["tp", "hm", "mk", "fp"] if f(r[f"{p}_hpwl_um"]))
                for r in rs if f(r["capo_hpwl_um"])]
        return [label, len(rs), cell([(0, x) for x in cg]), cell([(0, x) for x in ct]), cell([(0, x) for x in best])]

    hdr = ["subset", "n", "Capo / gpl", "Capo / mcp-TritonPart", "Capo / best mcp engine"]
    rows = [row("all", lambda r: True)]
    rows += [row(f"{k} row{'s' if k != '1' else ''}", lambda r, k=k: r["rows"] == k) for k in ["1", "2", "4", "6", "8"]]
    rows += [row(d, lambda r, d=d: r["design"] == d) for d in ["gcd", "aes", "ibex", "jpeg", "swerv_no_macro", "cva6_no_macro"]]
    rows += [row(p, lambda r, p=p: r["platform"] == p) for p in ["nangate45", "asap7"]]
    L.append(table(hdr, rows))
    allw = [r for r in wide if f(r["capo_hpwl_um"]) and f(r["tp_hpwl_um"])]
    ct = [f(r["capo_hpwl_um"]) / f(r["tp_hpwl_um"]) for r in allw]
    L.append(f"Over all 180 floorplans (gpl failures included, Capo and mcp both placed everything): "
             f"Capo / mcp-TritonPart = {gm(ct):.3f}, Capo better on {sum(x < 1 for x in ct)}/{len(ct)}. "
             "Capo placed all 180; gpl failed 31 (GPL-0305 ×5, DPL-0033 ×26).\n")
    tr = [f(r["capo_t_place_s"]) / f(r["tp_t_place_s"]) for r in allw if f(r["capo_t_place_s"]) and f(r["tp_t_place_s"])]
    L.append(f"Runtime: Capo / mcp-TritonPart placer time median {statistics.median(tr):.2f}×.\n")

    # E23
    e23 = load("e23_base")
    fp = per_fp([r for r in e23 if r["variant"] in ("gpl", "capo", "tp", "hm", "mk", "fp")])
    L.append("## 4.2 AR-1 floorplans (E23)\n")
    rows = []
    for d in sorted(fp, key=dorder):
        v = fp[d]
        if "capo" in v and "gpl" in v:
            best = min(v[p] for p in ["tp", "hm", "mk", "fp"] if p in v and (d, p) not in {(x, "fp") for x in FP_INVALID})
            rows.append([d, s3(v["capo"] / v["gpl"]), s3(v["tp"] / v["gpl"]) if "tp" in v else "–",
                         s3(v["capo"] / v["tp"]) if "tp" in v else "–", s3(v["capo"] / best)])
    cg = [(d, fp[d]["capo"] / fp[d]["gpl"]) for d in fp if "capo" in fp[d] and "gpl" in fp[d]]
    ct = [(d, fp[d]["capo"] / fp[d]["tp"]) for d in fp if "capo" in fp[d] and "tp" in fp[d]]
    tg = [(d, fp[d]["tp"] / fp[d]["gpl"]) for d in fp if "tp" in fp[d] and "gpl" in fp[d]]
    cb = []
    for d in fp:
        v = fp[d]
        if "capo" in v:
            cands = [v[p] for p in ["tp", "hm", "mk", "fp"] if p in v and not (p == "fp" and d in FP_INVALID)]
            if cands:
                cb.append((d, v["capo"] / min(cands)))
    rows.append(["**geomean (wins/n)**", cell(cg), cell(tg), cell(ct), cell(cb)])
    L.append(table(["floorplan", "Capo / gpl", "mcp-TritonPart / gpl", "Capo / mcp-TritonPart", "Capo / best mcp engine"], rows))

    # E14 + E16 (development checkpoints, ORFS floorplans)
    e14 = load("e14_capo")
    fp14 = per_fp(e14)
    L.append("## 4.3 Capo settings on the development checkpoints (E14; gcd / aes nangate45, ibex sky130hd; 3 seeds, DPO on; pre-DPO metric)\n")
    arms = [("capo", "Capo defaults"), ("capo_dens", "`-densityTarget` = flow density"), ("capo_hard", "`-tryHarder`"),
            ("capo_pad_dens", "padded cells + `-densityTarget` (the arm used from E16 on)"), ("fb1", "mcp-TritonPart (E12 defaults)")]
    rows = []
    for v, lab in arms:
        c = [lab]
        for d in sorted(fp14, key=dorder):
            c.append(s3(fp14[d][v] / fp14[d]["gpl"]) if v in fp14[d] and "gpl" in fp14[d] else "–")
        rows.append(c)
    L.append(table(["arm (/ gpl)"] + sorted(fp14, key=dorder), rows))
    e16 = load("e16_compare")
    fp16 = per_fp(e16)
    L.append("## 4.4 The six placers on the five development checkpoints (E16; 3 seeds, DPO on; pre-DPO metric, / gpl)\n")
    rows = []
    for p in ["capo", "tp", "hm", "mk", "fp"]:
        c = [NAMES[p]]
        vals = []
        for d in sorted(fp16, key=dorder):
            if p in fp16[d] and "gpl" in fp16[d]:
                vals.append(fp16[d][p] / fp16[d]["gpl"])
                c.append(s3(vals[-1]))
            else:
                c.append("–")
        c.append(s3(gm(vals)))
        rows.append(c)
    L.append(table(["placer"] + sorted(fp16, key=dorder) + ["geomean"], rows))

    # AM1 AMc
    am = load("am1")
    fpam = per_fp([r for r in am if r["variant"] in ("gpl", "X", "AM2_t30", "AMc", "AMg", "AMx")], "hpwl")
    if any("AMc" in v for v in fpam.values()):
        L.append("## 4.5 Capo as the seed of the American map (AM1 arm `AMc`; post-DPO HPWL / gpl, 3 seeds)\n")
        rows = []
        for d in sorted(fpam, key=dorder):
            v = fpam[d]
            if "gpl" in v:
                rows.append([d] + [s3(v[a] / v["gpl"]) if a in v else "–" for a in ["X", "AM2_t30", "AMc", "AMg", "AMx"]])
        L.append(table(["floorplan", "X (min-cut)", "AM2 (gpl τ0.3 seed)", "AMc (Capo seed)", "AMg (final gpl seed)", "AMx (X's own result as seed)"], rows))
    write("4_capo.md", L)


# ------------------------------------------------------------------ 5. American maps
def table_american_maps():
    am = load("am1")
    fp = per_fp(am, "hpwl")          # the study is defined on post-DPO HPWL
    fpd = per_fp(am, "m")
    cnt = defaultdict(lambda: defaultdict(int))
    for r in am:
        cnt[r["design"]][r["variant"]] += 1
    designs = ["aes_ar1_nangate45", "ibex_ar1_asap7", "jpeg_ar1_nangate45", "swerv_wrapper_ar1_nangate45",
               "ibex_u70_r8_asap7", "jpeg_u70_r4_nangate45"]
    designs = [d for d in designs if d in fp]
    L = ["# 5. American maps: connectivity versus geometry in one tree (AM1)\n",
         "Source: `am1` (`exp/am1.cfg`, `AMERICAN_MAPS_STUDY.md`). Six floorplans — three macro-free squares "
         "(aes/ng45 20 k, ibex/asap7 20 k, jpeg/ng45 65 k cells), one square with fixed macros (swerv_wrapper/ng45, "
         "100 k) and two k-row floorplans with the same netlists as the ibex / jpeg squares (ibex 8 rows, jpeg 4 rows) "
         "— 3 seeds, x3 conditions (DPO on, routed, 4 threads). **Metric in this table: post-DPO HPWL / gpl** "
         "(the study's definition); the pre-DPO ratio is given in parentheses in 5.1. gpl = krow mode.\n",
         "Arms: `X` min-cut from scratch (= mcp cutnet), `X_kway` (+ k-way refinement), `AM2_t30` the American map "
         "(a `gpl -overflow 0.3` seed assigned into the tree's regions by capacitated min-displacement transport at every "
         "level, min-cut nowhere), `MC_diag` min-cut decides at every level with the seed as terminals, `seed` the "
         "`gpl -overflow 0.3` placement legalized on its own (what the seed is worth without the tree), `AMx` / `AMg` / "
         "`AMc` the American map seeded by X's own result / the final gpl placement / Capo, `AM4_t30` quadrisection, "
         "`AMl_t30` net-aware transport (λ 0.5), `AM2_t50` / `AM2_t70` cruder seeds (`seed50` / `seed70` the matching controls).\n"]

    def r2(d, v, base="gpl"):
        if v not in fp[d] or base not in fp[d]:
            return "–"
        s = f"{fp[d][v] / fp[d][base]:.3f}"
        if v in fpd[d] and base in fpd[d]:
            s += f" ({fpd[d][v] / fpd[d][base]:.3f})"
        n = cnt[d][v]
        return s + (f" n={n}" if n != 3 else "")

    L.append("## 5.1 Core arms, per floorplan (post-DPO HPWL / gpl; pre-DPO in parentheses; n = seeds if ≠ 3)\n")
    core = ["seed", "X", "X_kway", "AM2_t30", "MC_diag"]
    rows = [[d] + [r2(d, v) for v in core] for d in designs]
    L.append(table(["floorplan"] + core, rows))
    L.append("`seed` failed legalization (DPL-0033, 3/3 seeds) on both k-row floorplans: the gpl τ 0.3 seed is not "
             "legalizable there on its own, yet the American map built from it is.\n")

    L.append("## 5.2 Geomeans over floorplan groups\n")
    groups = [("3 macro-free squares", designs[:3]), ("square with macros (swerv_wrapper)", [d for d in designs if d.startswith("swerv")]),
              ("2 k-row floorplans", [d for d in designs if KROW.match(d)]), ("all 6", designs)]
    rows = []
    for lab, ds in groups:
        c = [lab, len(ds)]
        for v in core:
            vals = [fp[d][v] / fp[d]["gpl"] for d in ds if v in fp[d] and "gpl" in fp[d]]
            c.append(f"{gm(vals):.3f}" + (f" ({len(vals)})" if len(vals) != len(ds) else "") if vals else "–")
        rows.append(c)
    L.append(table(["group", "n"] + core, rows))

    L.append("## 5.3 Where does the American map's result come from: seed quality and seed source\n")
    arms = ["seed", "AM2_t30", "seed50", "AM2_t50", "seed70", "AM2_t70", "AMg", "AMx", "AMc"]
    rows = [[d] + [r2(d, v) for v in arms] for d in designs]
    L.append(table(["floorplan"] + arms, rows))
    L.append("Read `AM2_tτ − seedτ` as the transport's own contribution at each seed quality; `AMg` is the map of the "
             "finished gpl placement (seed quality ceiling); `AMx` maps X's own placement (faithfulness control: an "
             "American map adds nothing to a min-cut result).\n")

    L.append("## 5.4 Variants of the map: quadrisection and net-aware transport\n")
    arms = ["AM2_t30", "AM4_t30", "AMl_t30"]
    rows = [[d] + [r2(d, v) for v in arms] for d in designs]
    L.append(table(["floorplan"] + arms, rows))

    L.append("## 5.5 One level decided by min-cut inside the American map (`AM2_notL` / `AM2_t30`)\n")
    L.append("Each column: the American map everywhere except level L, where min-cut decides; ratio to the pure "
             "American map (> 1: the min-cut decision at that level costs HPWL; < 1: it repairs the seed).\n")
    arms = [f"AM2_not{k}" for k in range(1, 7)]
    rows = [[d] + [r2(d, v, "AM2_t30") for v in arms] for d in designs]
    L.append(table(["floorplan"] + [f"L{k}" for k in range(1, 7)], rows))

    L.append("## 5.6 American map above level L, min-cut below (`AM2_LL` / gpl)\n")
    arms = ["AM2_t30", "AM2_L2", "AM2_L4", "AM2_L6", "X"]
    rows = [[d] + [r2(d, v) for v in arms] for d in designs]
    L.append(table(["floorplan", "AM at every level", "AM above L2", "AM above L4", "AM above L6", "min-cut everywhere (X)"], rows))

    # routed WL and runtime
    fpw = per_fp(am, "grt_wl")
    fpt = per_fp(am, "t_place")
    L.append("## 5.7 Routed wirelength and placer time (/ gpl)\n")
    arms = ["X", "X_kway", "AM2_t30", "MC_diag"]
    rows = []
    for d in designs:
        c = [d]
        for v in arms:
            w = fpw.get(d, {})
            t = fpt.get(d, {})
            c.append((s3(w[v] / w["gpl"]) if v in w and "gpl" in w else "–") + " / " +
                     (f"{t[v] / t['gpl']:.1f}×" if v in t and "gpl" in t else "–"))
        rows.append(c)
    L.append(table(["floorplan"] + [f"{v}: WL / time" for v in arms], rows))
    L.append("The full analysis with failure accounting, the per-level disagreement diagnostic and the transport's "
             "share of runtime: `python3 exp/american_maps.py results/am1` (`../am1/am1_analysis.md`).\n")
    write("5_american_maps.md", L)


# ------------------------------------------------------------------ 6. anatomy
def table_anatomy():
    L = ["# 6. Anatomy of the gap between min-cut and analytical placement\n",
         "Development checkpoints gcd (nangate45, 0.3 k cells), aes (nangate45, 20 k), ibex (sky130hd, 20 k), "
         "ORFS floorplans (AR ≈ 1), 3 seeds. Reference: `gpl_default` of E0 (plain `global_placement`, HPWL after "
         "detailed placement: gcd 4 071, aes 461 800, ibex 595 400 µm). Ratios are HPWL after detailed placement "
         "/ that reference unless stated.\n"]
    e0 = per_fp(load("e0_baselines"))
    ref = {d: e0[d]["gpl_default"] for d in e0 if "gpl_default" in e0[d]}
    D = ["gcd_nangate45", "aes_nangate45", "ibex_sky130hd"]

    def r(exp_fp, d, v):
        return s3(exp_fp[d][v] / ref[d]) if d in exp_fp and v in exp_fp[d] and d in ref else "–"

    # 6.1 ladder
    ladder = [("e0_baselines", "mcp_tp_k2", "M1 placer: cut-net, 1 wave, uniform whitespace (E0)"),
              ("e3_objectives", "tp_cutnet_k2", "+ waves (E3)"),
              ("e5_reserve", "ml_t10_pad", "+ `minlocal` whitespace, padding (E5 / Gate 3)"),
              ("e6_endgame", "all", "+ end game: exact ≤16, optimal ≤10, ironing (E6 `all`)"),
              ("e6_endgame", "opt12", "+ `-optimal_cells 12` (E6 `opt12`)"),
              ("e9_positions", "optimal", "+ optimal in-row positions (E9)"),
              ("e10_leaffb", "p2", "+ leaf feedback pass (E10 `p2`, current default)"),
              ("e11_feedback", "fb8", "+ 8 placement feedback passes (E11 `fb8`, 5–11× time)"),
              ("e9_positions", "gpl_optimal", "gpl seed → transport → end game (E9 `gpl_optimal`)"),
              ("e10_leaffb", "gpl_p3", "gpl seed → end game, 3 passes (E10 `gpl_p3`)"),
              ("e11_feedback", "gpl_tr_d1_st", "gpl seed → refinement with seed terminals → end game (E11)")]
    L.append("## 6.1 The from-scratch result over the project (HPWL after detailed placement / E0 gpl)\n")
    rows = []
    cache = {}
    for exp, v, lab in ladder:
        if exp not in cache:
            cache[exp] = per_fp(load(exp))
        rows.append([lab] + [r(cache[exp], d, v) for d in D])
    L.append(table(["configuration", "gcd", "aes", "ibex"], rows))
    L.append("The first eight rows are the from-scratch placer; the last three use gpl's placement as a seed and show "
             "what the tree's lower levels and end game are worth when the top-level decisions are analytical.\n")

    # 6.2 E8 per level
    e8 = per_fp(load("e8_seedlevels"))
    L.append("## 6.2 Per-level attribution (E8): gpl decides levels < L, min-cut decides from L down\n")
    L.append("`-seed_levels L` with the gpl seed assigned by transport above level L; L = 0 is pure min-cut, "
             "`Lall` = every level seeded (= the seed through the tree). HPWL / E0 gpl.\n")
    levels = [f"L{k:02d}" for k in range(14)] + ["Lall"]
    rows = []
    for lv in levels:
        if any(lv in e8.get(d, {}) for d in D):
            rows.append([f"L = {int(lv[1:])}" if lv != "Lall" else "all levels seeded"] + [r(e8, d, lv) for d in D])
    L.append(table(["seeded above", "gcd", "aes", "ibex"], rows))
    L.append("Reading: on aes and ibex the gap closes almost linearly as more levels are handed to gpl "
             "(aes 1.24 → 1.02, ibex 1.33 → 1.07 over 13 levels, ≈ 0.02 per level): the loss is distributed over "
             "the tree, no single level carries it. gcd stays at 1.20 whatever is seeded — its gap was in the "
             "leaves and end game, which E9/E10 later recovered (table 6.1).\n")

    # 6.3 E12 DPO
    e12 = load("e12_dpo")
    L.append("## 6.3 Legalization and DPO on both flows (E12; 3 seeds)\n")
    acc = defaultdict(lambda: defaultdict(list))
    for x in e12:
        acc[x["design"]][x["variant"]].append(x)
    rows = []
    for d in D:
        for v in ["gpl", "fb1", "fb4"]:
            rs = acc[d].get(v, [])
            if not rs:
                continue
            g = statistics.mean(x["hpwl_global"] for x in rs)
            dpl = statistics.mean(x["hpwl_dpl"] for x in rs)
            fin = statistics.mean(x["hpwl"] for x in rs)
            rows.append([d, {"gpl": "gpl", "fb1": "mcp fb1", "fb4": "mcp fb4"}[v], f"{g:,.0f}", f"{dpl:,.0f}",
                         f"{100 * (dpl / g - 1):+.1f} %", f"{fin:,.0f}", f"{100 * (fin / dpl - 1):+.1f} %",
                         s3(fin / statistics.mean(x["hpwl"] for x in acc[d]["gpl"]))])
    L.append(table(["design", "arm", "HPWL placer output", "after DPL", "legalization", "after DPO", "DPO gain", "post-DPO / gpl post-DPO"], rows))
    L.append("mcp's output is already row-aligned and overlap-free (legalization ≈ 0); gpl pays 5–15 % to legalize "
             "and recovers more of it in DPO than mcp does.\n")

    # 6.4 legalization cost on the large grids
    L.append("## 6.4 Legalization cost on the large grids (median of HPWL after DPL / placer output − 1)\n")
    rows = []
    cols = [("gpl", "gpl"), ("capo", "Capo"), ("tp", "mcp-TritonPart"), ("cutnet_tp", "mcp cutnet"), ("kway_tp", "mcp k-way"),
            ("hm", "mcp-hMETIS"), ("mk", "mcp-Mt-KaHyPar"), ("fp", "mcp-FastPart")]
    for exp, lab in [("e17_krow", "E17 few-row (180 floorplans)"), ("e23_base", "E23 AR 1 (20 floorplans)"),
                     ("x4_ar_sweep", "X4 AR × util sweep (182 floorplans, 3 seeds)")]:
        rs = load(exp)
        c = [lab]
        for a, _ in cols:
            v = [x["hpwl_dpl"] / x["hpwl_global"] - 1 for x in rs if x["variant"] == a and x["hpwl_dpl"] and x["hpwl_global"]
                 and not (a == "fp" and x["design"] in FP_INVALID)]
            c.append(f"{100 * statistics.median(v):.2f} %" if v else "–")
        rows.append(c)
    L.append(table(["grid"] + [lab for _, lab in cols], rows))
    L.append("gpl's legalization cost is small on the few-row floorplans (cells are already nearly in rows) and 7–10 % "
             "on the square and wide floorplans; every mcp arm (and Capo, which also places in rows) is below 1.5 %.\n")

    # 6.5 hybridization spectrum (copy)
    hyb = os.path.join(PR, "hybridization_e18_e24", "summary_tables.md")
    if os.path.exists(hyb):
        txt = open(hyb).read()
        m = re.search(r"## Hybridization spectrum.*?(?=\n## )", txt, re.S)
        if m:
            L.append("## 6.5 The hybridization spectrum (E18–E21e; post-DPO HPWL / gpl, 3 seeds)\n")
            L.append(m.group(0).split("\n", 1)[1].strip() + "\n")
            L.append("From the top: pure min-cut; min-cut informed by analytical positions (terminals, whitespace); "
                     "analytical decides, min-cut commits (seed → transport → end game). The order of the rows is the "
                     "order of how much of the decision is analytical. Figures: `results/viz/hybrid/`.\n")
        m = re.search(r"## k-row floorplans: post-DPO HPWL / gpl, geomean \(seed 1\).*?(?=\n## )", txt, re.S)
        if m:
            L.append("## 6.6 Does informing min-cut help where min-cut already wins? (E24 on the k-row floorplans)\n")
            L.append(m.group(0).split("\n", 1)[1].strip() + "\n")

    # 6.7 I1
    st = os.path.join(PR, "i1_info", "STATUS.md")
    if os.path.exists(st):
        txt = open(st).read()
        m = re.search(r"\| arm \| what \|.*?\n\n", txt, re.S)
        if m:
            L.append("## 6.7 Cut quality versus information (I1; six AR-1 floorplans, 2 seeds, post-DPO HPWL / gpl)\n")
            L.append(m.group(0).strip() + "\n")
            L.append("`deg*`: the bisections deliberately degraded to ×1.4–×3.3 the cut; `imp`: best of 4 runs with 10 FM "
                     "passes; `oT`: perfect gpl positions for every cell outside the region as terminals; `oA`: the "
                     "gpl-derived assignment decides inside the region. Elasticity of HPWL to cut ≈ 0.17; a partitioner "
                     "cutting 10 % fewer nets would be worth ~2 %; knowing where cells inside a region end up relative "
                     "to each other closes 57–89 % of the gap on the macro-free floorplans.\n")
    write("6_anatomy_of_gap.md", L)


def main():
    os.makedirs(OUT, exist_ok=True)
    table_few_rows()
    table_ar1()
    table_sweep()
    table_capo()
    table_american_maps()
    table_anatomy()
    with open(os.path.join(OUT, "README.md"), "w") as fh:
        fh.write(TABLES_README)


TABLES_README = """# Tables

**Start with [0_paper_tables.md](0_paper_tables.md)**: every table and figure of the paper, recomputed from the CSVs in
`../` under the paper's rules (RePlAce runs that diverge, fail legalization or stop above their overflow target are
failures; RB runs whose partitioner failed at the root or on > 1 % of the bisections are failures; every ratio is over
the floorplans common to the configurations compared). Its header says which section reproduces which paper item.

Tables 1–10 are the extended views: more subsets, per-floorplan listings, runtime, routing, and the studies the paper
only cites. Tables 1–6 use a weaker condition for RePlAce (any legal placement counts, 149 of the 180 few-row
floorplans instead of 137), so their aggregates differ slightly from the paper's; tables 0 and 7–10 use the paper's rule.

| table | contents | floorplans; placers | paper | data (`../<exp>/`) | script |
|---|---|---|---|---|---|
| [0_paper_tables.md](0_paper_tables.md) | the paper's Tables I–III and the numbers behind Figs. 1, few-row, sweep, square; held-out designs | all three sets; all placers | Tables I–III, Figs. 1, few-row, sweep, square, footnotes | `x3_mega`, `e17_krow`, `e23_base`, `x4_ar_sweep`, `e26b_norevisit`, `r5_ablation_square`, `r1_fewrow_oracle`, `x5_heldout` | `exp/paper_numbers.py` |
| [1_few_rows.md](1_few_rows.md) | few-row set by row count, design, PDK, utilization; routed wirelength; runtime; RB+k | 180 few-row; RB x {TP, HM, MK, FP}, RB+k (TP) | Table II (few-row), Fig. few-row | `e17_krow`, `x3_mega` | `exp/paper_tables.py` |
| [2_ar1_designs.md](2_ar1_designs.md) | square set per floorplan and geomeans; runtime; RB+k; AR-1 part of the sweep | 20 square; RB x 4 engines, RB+k (TP) | Table II (square), Fig. square | `e23_base`, `x3_mega`, `x4_ar_sweep` | `exp/paper_tables.py` |
| [3_ar_util_sweep.md](3_ar_util_sweep.md) | aspect ratio x utilization, by design, by row count; outliers; runtime | 182 sweep; RB (TP), RB+k (TP) | Fig. 1, Fig. sweep | `x4_ar_sweep` | `exp/paper_tables.py` |
| [4_capo.md](4_capo.md) | Capo 10.5 against RePlAce and against RB, both sets; Capo settings | 180 few-row + 20 square; Capo | Table II (Capo column), Sec. few-row and square | `e17_krow`, `e23_base`, `e14_capo`, `e16_compare`, `am1` | `exp/paper_tables.py` |
| [5_american_maps.md](5_american_maps.md) | RePlAce-derived assignments through the min-cut tree (American maps): seed quality, map above / below a level, min-cut at one level | 6 floorplans, 3 seeds | H3 (Sec. "Why the Square-Die Gap Persists", Fig. anatomy (b)) | `am1` | `exp/paper_tables.py` |
| [6_anatomy_of_gap.md](6_anatomy_of_gap.md) | development history of the gap: component ladder on the development checkpoints, per-level attribution, legalization and DPO, hybridization spectrum, H1 / H2 arms | development checkpoints; square set (H1 / H2) | H1, H2 (Fig. anatomy (a)); background | `e0`–`e12`, `e18`–`e24`, `i1_info` | `exp/paper_tables.py` |
| [7_global_routing.md](7_global_routing.md) | routed wirelength and routing overflow of every placer | few-row and square; all placers | Sec. few-row ("On 87 commonly global-routable floorplans ..."), Sec. square (10.8 %) | `e17_krow`, `x3_mega`, `e23_base` | `exp/grt_tables.py` |
| [8_capo_attribution.md](8_capo_attribution.md) | where RB's advantage over Capo comes from: component ladder vs Capo, leave-one-out (exact bisection, leaf search, ironing, multi-row assignment), partitioner at the base rung | 180 few-row; RB (TP) variants, Capo | Sec. few-row ("The advantage predates our implementation") | `e27_capo_attr`, `e26b_norevisit`, `e17_krow`, `x3_mega` | `exp/capo_attribution.py` |
| [9_seed_noise.md](9_seed_noise.md) | seed sensitivity: per-floorplan coefficient of variation and spread of RB / RB+k over three seeds, stability of the ratios to RePlAce | 182 sweep, 3 seeds | footnote in Sec. "Evaluation" (median CV 0.8 %) | `x4_ar_sweep` | `exp/seed_noise.py` |

Regenerate: `python3 exp/paper_export.py` (the CSVs), then `python3 exp/paper_numbers.py` (table 0),
`python3 exp/paper_tables.py` (tables 1–6), `python3 exp/grt_tables.py` (table 7),
`python3 exp/capo_attribution.py` (table 8), `python3 exp/seed_noise.py` (table 9).

Placer codes in the tables: `gpl` = RePlAce (OpenROAD `global_placement`); `capo` = Capo 10.5; `tp`, `hm`, `mk`, `fp`
= RB with TritonPart, hMETIS, Mt-KaHyPar, FastPart (`mcp-TritonPart` etc. in older headings); `cutnet_tp` / `mcp cutnet`
= RB (TP); `kway_tp` / `mcp k-way` = RB+k (TP). Metric, unless a table says otherwise: HPWL after detailed placement and
before DPO, ratio to RePlAce on the same floorplan, seed means per floorplan, geometric mean over floorplans; (wins/n) =
floorplans where the placer beats RePlAce / floorplans compared.
"""


if __name__ == "__main__":
    main()
