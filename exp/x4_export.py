#!/usr/bin/env python3
"""X4 (the floorplan author's aspect-ratio x utilization DOE): CSVs and a markdown summary.

    exp/x4_export.py [results/x4_ar_sweep]

Writes, into the results folder:
    x4_runs.csv         one row per run (all seeds)
    x4_floorplans.csv   one row per floorplan: seed means per arm, ratios
    SUMMARY.md          ratio tables (AR x util, per design, rows axis,
                        runtime, the floorplan author's RePlAce cross-check)

HPWL used throughout: hpwl_dpl, after detailed placement and before the
optional DPO pass (DPO ran on the small set only; every arm goes through the
same legalizer).  final.hpwl (after DPO where it ran) is kept in the CSVs.
Ratios are per floorplan: mean over seeds of the arm / mean over seeds of
gpl; tables report the geometric mean over floorplans.
"""

import csv
import glob
import json
import math
import os
import re
import statistics
import sys
from collections import defaultdict

NAME = re.compile(r"^sweep_(?P<design>[a-z0-9_]+?)_ar(?P<ar>\d(?:p\d+)?)_util(?P<util>\d+)"
                  r"_(?P<pdk>asap7|nangate45)__(?P<variant>[a-z_]+)__s(?P<seed>\d)$")
CORE = re.compile(r"CoreBBox:\s*\(\s*([\d.]+)\s+([\d.]+)\s*\)\s*\(\s*([\d.]+)\s+([\d.]+)\s*\)")
SITE_H = {"asap7": 0.27, "nangate45": 1.4}
SMALL = ["gcd", "aes", "ibex", "jpeg"]
BIG = ["swerv", "cva6", "mempool_tile", "xiangshan"]
DESIGNS = SMALL + BIG
ARS = ["0p03", "0p06", "0p09", "1"]
UTILS = ["60", "70", "80"]
PDKS = ["asap7", "nangate45"]
ARMS = ["gpl", "cutnet_tp", "kway_tp"]
ARM_LABEL = {"gpl": "gpl (RePlAce)", "cutnet_tp": "mcp cutnet", "kway_tp": "mcp k-way"}
ROW_BINS = [(1, 4), (5, 8), (9, 16), (17, 32), (33, 64), (65, 128), (129, 256), (257, 10**9)]


def ar_f(ar):
    return float(ar.replace("p", "."))


def geomean(xs):
    xs = [x for x in xs if x and x > 0]
    return math.exp(sum(math.log(x) for x in xs) / len(xs)) if xs else float("nan")


def fmt(x, nd=3):
    return "" if x is None or (isinstance(x, float) and math.isnan(x)) else f"{x:.{nd}f}"


def rows_of(folder, fp, pdk):
    """Core rows from any gpl log of this floorplan (CoreBBox height / site)."""
    for log in sorted(glob.glob(os.path.join(folder, f"{fp}__gpl__s*.log"))):
        with open(log, errors="replace") as f:
            for line in f:
                m = CORE.search(line)
                if m:
                    h = float(m.group(4)) - float(m.group(2))
                    return int(round(h / SITE_H[pdk])), float(m.group(3)) - float(m.group(1)), h
    return None, None, None


def load(folder):
    runs = []
    for path in sorted(glob.glob(os.path.join(folder, "*__s[0-9].json"))):
        base = os.path.basename(path)[:-5]
        m = NAME.match(base)
        if not m:
            continue
        d = json.load(open(path))
        fin = d.get("final", {})
        r = dict(m.groupdict())
        r.update(
            floorplan=f"sweep_{r['design']}_ar{r['ar']}_util{r['util']}_{r['pdk']}",
            seed=int(r["seed"]),
            cells=d.get("netlist", {}).get("movable"),
            nets=d.get("netlist", {}).get("nets"),
            hpwl_global=fin.get("hpwl_global"),
            hpwl_dpl=fin.get("hpwl_dpl"),
            hpwl_final=fin.get("hpwl"),
            t_place_s=fin.get("t_place"),
            t_dpl_s=fin.get("t_dpl"),
            mem_peak_mb=fin.get("mem_peak_mb"),
            violations=fin.get("violations"),
            dpo=d.get("knobs", {}).get("dpo"),
            host=d.get("knobs", {}).get("host"),
        )
        runs.append(r)
    return runs


def failures(folder):
    out = []
    for log in (f"{folder}.log", f"{folder}_pdn.log"):
        if not os.path.exists(log):
            continue
        for line in open(log, errors="replace"):
            if line.startswith("FAIL"):
                name = line.split()[1]
                if name not in [o[0] for o in out]:
                    reason = ""
                    lp = os.path.join(folder, name + ".log")
                    if os.path.exists(lp):
                        for l in open(lp, errors="replace"):
                            if "[ERROR" in l:
                                reason = l.strip()[:90]
                                break
                    out.append((name, reason))
    return out


def main(argv):
    folder = argv[1] if len(argv) > 1 else "results/x4_ar_sweep"
    runs = load(folder)
    if not runs:
        sys.exit(f"no results in {folder}")

    # ---- long CSV -------------------------------------------------------
    cols = ["floorplan", "design", "pdk", "ar", "util", "variant", "seed", "cells", "nets",
            "hpwl_global", "hpwl_dpl", "hpwl_final", "t_place_s", "t_dpl_s", "mem_peak_mb",
            "violations", "dpo", "host"]
    with open(os.path.join(folder, "x4_runs.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        for r in sorted(runs, key=lambda r: (DESIGNS.index(r["design"]), ARS.index(r["ar"]),
                                            r["util"], r["pdk"], ARMS.index(r["variant"]), r["seed"])):
            rr = dict(r)
            rr["ar"] = ar_f(r["ar"])
            w.writerow(rr)

    # ---- per floorplan --------------------------------------------------
    by_fp = defaultdict(lambda: defaultdict(list))
    meta = {}
    for r in runs:
        by_fp[r["floorplan"]][r["variant"]].append(r)
        meta[r["floorplan"]] = r
    fps = []
    for fp, arms in by_fp.items():
        m = meta[fp]
        rows, core_w, core_h = rows_of(folder, fp, m["pdk"])
        rec = dict(floorplan=fp, design=m["design"], pdk=m["pdk"], ar=ar_f(m["ar"]), ar_tag=m["ar"],
                   util=int(m["util"]), rows=rows, core_w_um=core_w, core_h_um=core_h,
                   cells=m["cells"], nets=m["nets"],
                   set="small" if m["design"] in SMALL else "big")
        for a in ARMS:
            rs = arms.get(a, [])
            vals = [r["hpwl_dpl"] for r in rs if r["hpwl_dpl"]]
            rec[f"{a}_n"] = len(vals)
            rec[f"{a}_hpwl_um"] = statistics.mean(vals) if vals else None
            rec[f"{a}_hpwl_min"] = min(vals) if vals else None
            rec[f"{a}_hpwl_max"] = max(vals) if vals else None
            rec[f"{a}_spread"] = (max(vals) / min(vals) - 1) if len(vals) > 1 else None
            fin = [r["hpwl_final"] for r in rs if r["hpwl_final"]]
            rec[f"{a}_hpwl_final_um"] = statistics.mean(fin) if fin else None
            tp = [r["t_place_s"] for r in rs if r["t_place_s"] is not None]
            rec[f"{a}_t_place_s"] = statistics.mean(tp) if tp else None
        g = rec["gpl_hpwl_um"]
        for a in ("cutnet_tp", "kway_tp"):
            rec[f"{a}_vs_gpl"] = rec[f"{a}_hpwl_um"] / g if g and rec[f"{a}_hpwl_um"] else None
        rec["kway_vs_cutnet"] = (rec["kway_tp_hpwl_um"] / rec["cutnet_tp_hpwl_um"]
                                 if rec["cutnet_tp_hpwl_um"] and rec["kway_tp_hpwl_um"] else None)
        fps.append(rec)
    fps.sort(key=lambda r: (DESIGNS.index(r["design"]), ARS.index(r["ar_tag"]), r["util"], r["pdk"]))
    fcols = list(fps[0].keys())
    with open(os.path.join(folder, "x4_floorplans.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fcols)
        w.writeheader()
        for r in fps:
            w.writerow({k: (fmt(v, 4) if isinstance(v, float) else v) for k, v in r.items()})

    # ---- summary --------------------------------------------------------
    L = []
    P = L.append
    with_gpl = [r for r in fps if r["gpl_hpwl_um"]]
    P("# X4 — mcp on the floorplan author's aspect-ratio × utilization DOE\n")
    P(f"{len(runs)} runs, {len(fps)} floorplans ({len(with_gpl)} with a gpl baseline). "
      "HPWL = after detailed placement, before DPO. Ratio = seed mean of the arm / seed mean "
      "of gpl on the same floorplan; tables give the geometric mean over floorplans (n in "
      "parentheses). Wins = floorplans where the arm's HPWL is lower than gpl's.\n")

    def ratio_block(sel, key):
        vals = [r[key] for r in sel if r.get(key)]
        if not vals:
            return "–", 0, 0
        return f"{geomean(vals):.3f}", sum(v < 1 for v in vals), len(vals)

    P("## Overall\n")
    P("| set | designs | floorplans | k-way / gpl | wins | cutnet / gpl | wins | k-way / cutnet | wins |")
    P("|---|---|---|---|---|---|---|---|---|")
    for s, ds in (("small", SMALL), ("big", BIG), ("all", DESIGNS)):
        sel = [r for r in with_gpl if r["design"] in ds]
        k, kw, kn = ratio_block(sel, "kway_tp_vs_gpl")
        c, cw, cn = ratio_block(sel, "cutnet_tp_vs_gpl")
        x, xw, xn = ratio_block(sel, "kway_vs_cutnet")
        P(f"| {s} | {', '.join(ds)} | {kn} | **{k}** | {kw}/{kn} | {c} | {cw}/{cn} | {x} | {xw}/{xn} |")
    clean = [r for r in with_gpl if not (r["kway_tp_vs_gpl"] and r["kway_tp_vs_gpl"] < 0.7)]
    for s, ds in (("big, excl. gpl outliers", BIG), ("all, excl. gpl outliers", DESIGNS)):
        sel = [r for r in clean if r["design"] in ds]
        k, kw, kn = ratio_block(sel, "kway_tp_vs_gpl")
        c, cw, cn = ratio_block(sel, "cutnet_tp_vs_gpl")
        x, xw, xn = ratio_block(sel, "kway_vs_cutnet")
        P(f"| {s} | (k-way/gpl ≥ 0.7, see below) | {kn} | **{k}** | {kw}/{kn} | {c} | {cw}/{cn} | {x} | {xw}/{xn} |")
    P("")
    P("Big set by AR, excluding the gpl outliers: " + "; ".join(
        f"AR {ar_f(ar)}: {ratio_block([r for r in clean if r['design'] in BIG and r['ar_tag'] == ar], 'kway_tp_vs_gpl')[0]}"
        f" ({ratio_block([r for r in clean if r['design'] in BIG and r['ar_tag'] == ar], 'kway_tp_vs_gpl')[1]}/"
        f"{ratio_block([r for r in clean if r['design'] in BIG and r['ar_tag'] == ar], 'kway_tp_vs_gpl')[2]})"
        for ar in ARS) + ".\n")

    P("## k-way / gpl by aspect ratio × utilization\n")
    for s, ds in (("small set (gcd aes ibex jpeg)", SMALL), ("big set (swerv cva6 mempool_tile xiangshan)", BIG)):
        P(f"### {s}\n")
        P("| AR \\ util | " + " | ".join(UTILS) + " | all |")
        P("|---|" + "---|" * (len(UTILS) + 1))
        for ar in ARS:
            cells = []
            for u in UTILS + [None]:
                sel = [r for r in with_gpl if r["design"] in ds and r["ar_tag"] == ar
                       and (u is None or r["util"] == int(u))]
                v, w_, n = ratio_block(sel, "kway_tp_vs_gpl")
                cells.append(f"{v} ({w_}/{n})")
            P(f"| {ar_f(ar)} | " + " | ".join(cells) + " |")
        sel_all = [r for r in with_gpl if r["design"] in ds]
        cells = []
        for u in UTILS + [None]:
            sel = [r for r in sel_all if u is None or r["util"] == int(u)]
            v, w_, n = ratio_block(sel, "kway_tp_vs_gpl")
            cells.append(f"{v} ({w_}/{n})")
        P("| all | " + " | ".join(cells) + " |")
        P("")

    P("## k-way / gpl by design × aspect ratio (both PDKs, 3 utilizations)\n")
    P("| design | cells (asap7 / ng45) | " + " | ".join(f"AR {ar_f(a)}" for a in ARS) + " | all |")
    P("|---|---|" + "---|" * (len(ARS) + 1))
    for d in DESIGNS:
        cells_txt = " / ".join(str(next((r["cells"] for r in fps if r["design"] == d and r["pdk"] == p), "")) for p in PDKS)
        vals = []
        for ar in ARS + [None]:
            sel = [r for r in with_gpl if r["design"] == d and (ar is None or r["ar_tag"] == ar)]
            v, w_, n = ratio_block(sel, "kway_tp_vs_gpl")
            vals.append(f"{v} ({w_}/{n})")
        P(f"| {d} | {cells_txt} | " + " | ".join(vals) + " |")
    P("")

    P("## k-way / gpl by design × aspect ratio, per PDK\n")
    for p in PDKS:
        P(f"### {p}\n")
        P("| design | " + " | ".join(f"AR {ar_f(a)}" for a in ARS) + " | rows at AR 0.03 … 1 |")
        P("|---|" + "---|" * (len(ARS) + 1))
        for d in DESIGNS:
            vals = []
            for ar in ARS:
                sel = [r for r in with_gpl if r["design"] == d and r["pdk"] == p and r["ar_tag"] == ar]
                v, w_, n = ratio_block(sel, "kway_tp_vs_gpl")
                vals.append(f"{v} ({w_}/{n})")
            rws = []
            for ar in ARS:
                rr = sorted({r["rows"] for r in fps if r["design"] == d and r["pdk"] == p and r["ar_tag"] == ar and r["rows"]})
                rws.append(f"{rr[0]}–{rr[-1]}" if len(rr) > 1 else (str(rr[0]) if rr else "?"))
            P(f"| {d} | " + " | ".join(vals) + " | " + ", ".join(rws) + " |")
        P("")

    P("## Ratio versus number of rows\n")
    P("Rows = core height / site height. The k-row study (E17) covered 1–8 rows; this DOE starts "
      "at 3–4 rows (gcd) and reaches a few thousand (xiangshan at AR 1).\n")
    P("| rows | floorplans | k-way / gpl | wins | cutnet / gpl | designs |")
    P("|---|---|---|---|---|---|")
    for lo, hi in ROW_BINS:
        sel = [r for r in with_gpl if r["rows"] and lo <= r["rows"] <= hi]
        if not sel:
            continue
        k, kw, kn = ratio_block(sel, "kway_tp_vs_gpl")
        c, _, _ = ratio_block(sel, "cutnet_tp_vs_gpl")
        ds = sorted({r["design"] for r in sel}, key=DESIGNS.index)
        label = f"{lo}–{hi}" if hi < 10**9 else f"≥{lo}"
        P(f"| {label} | {kn} | {k} | {kw}/{kn} | {c} | {', '.join(ds)} |")
    P("")

    P("## Seed spread (max/min − 1 over 3 seeds, median over floorplans)\n")
    P("| set | gpl | cutnet | k-way |")
    P("|---|---|---|---|")
    for s, ds in (("small", SMALL), ("big", BIG)):
        sel = [r for r in fps if r["design"] in ds]
        vals = []
        for a in ARMS:
            sp = [r[f"{a}_spread"] for r in sel if r.get(f"{a}_spread") is not None]
            vals.append(f"{100 * statistics.median(sp):.2f} %" if sp else "–")
        P(f"| {s} | " + " | ".join(vals) + " |")
    P("")

    P("## Placer runtime (seconds, 4 threads, mean over floorplans and seeds)\n")
    P("| design | gpl | cutnet | k-way | k-way / gpl |")
    P("|---|---|---|---|---|")
    for d in DESIGNS:
        sel = [r for r in fps if r["design"] == d]
        t = {}
        for a in ARMS:
            v = [r[f"{a}_t_place_s"] for r in sel if r.get(f"{a}_t_place_s") is not None]
            t[a] = statistics.mean(v) if v else None
        rat = t["kway_tp"] / t["gpl"] if t["gpl"] and t["kway_tp"] else None
        P(f"| {d} | {fmt(t['gpl'], 1)} | {fmt(t['cutnet_tp'], 0)} | {fmt(t['kway_tp'], 0)} | {fmt(rat, 0)}× |")
    P("")

    # gpl outliers: floorplans where gpl is far worse than mcp (instability)
    P("## gpl outliers (k-way / gpl < 0.7)\n")
    out = [r for r in with_gpl if r["kway_tp_vs_gpl"] and r["kway_tp_vs_gpl"] < 0.7]
    if out:
        P("| floorplan | rows | gpl HPWL (min–max over seeds) | k-way HPWL | k-way / gpl |")
        P("|---|---|---|---|---|")
        for r in out:
            P(f"| {r['floorplan']} | {r['rows']} | {r['gpl_hpwl_um']:,.0f} ({r['gpl_hpwl_min']:,.0f}–{r['gpl_hpwl_max']:,.0f}) "
              f"| {r['kway_tp_hpwl_um']:,.0f} | {r['kway_tp_vs_gpl']:.3f} |")
    else:
        P("none")
    P("")

    P("## Failures\n")
    fl = failures(folder)
    if fl:
        for name, reason in fl:
            P(f"* `{name}` — {reason}")
    else:
        P("none")
    P("")

    # the floorplan author's RePlAce cross-check
    dav = "scratch/ar_sweep/meta/summary.csv"
    if os.path.exists(dav):
        P("## Cross-check against the floorplan author's RePlAce baseline (`summary.csv`, ar0.03_util60 only)\n")
        P("His flow: `global_placement -routability_driven` + detailed placement, one run. "
          "Ours (`gpl`): `-disable_pin_density_adjust -overflow 0.2`, 3 seeds.\n")
        P("| design | pdk | the floorplan author HPWL | our gpl HPWL (mean) | ours / the floorplan author | k-way / the floorplan author |")
        P("|---|---|---|---|---|---|")
        for row in csv.DictReader(open(dav)):
            if not row.get("hpwl_um"):
                continue
            cfg = row["config"].replace(".", "p").replace("ar", "ar", 1)
            fp = f"sweep_{row['design']}_{cfg}_{row['pdk']}"
            rec = next((r for r in fps if r["floorplan"] == fp), None)
            if not rec:
                continue
            dh = float(row["hpwl_um"])
            g = rec["gpl_hpwl_um"]
            k = rec["kway_tp_hpwl_um"]
            P(f"| {row['design']} | {row['pdk']} | {dh:,.0f} | {fmt(g, 0) and f'{g:,.0f}'} | "
              f"{fmt(g / dh if g else None)} | {fmt(k / dh if k else None)} |")
        P("")

    with open(os.path.join(folder, "SUMMARY.md"), "w") as f:
        f.write("\n".join(L) + "\n")
    print(f"{len(runs)} runs, {len(fps)} floorplans -> {folder}/x4_runs.csv, x4_floorplans.csv, SUMMARY.md")


if __name__ == "__main__":
    main(sys.argv)
