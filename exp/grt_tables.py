#!/usr/bin/env python3
"""Table 7: global routing on the few-row set -- routed wirelength and
congestion of every placer, from the harness runs.

    python3 exp/grt_tables.py            # -> results/tables/7_global_routing.md

Inputs (all in results/):
  e17_krow/runs.csv      RePlAce (gpl), Capo and RB with TP / HM / MK / FP
  x3_mega/runs.csv       RB+k (TP)
  x3_mega/replace_convergence.csv
                         RePlAce's final density overflow per floorplan, parsed
                         from its log; a run that stopped above the 0.2 target,
                         hit the iteration cap, diverged or did not legalize is
                         a RePlAce failure and is excluded from the ratios.

Routing in every run: global_route -allow_congestion -congestion_iterations 30
after detailed_placement and improve_placement (DPO).  grt_wl is the GRT-0018
wirelength; overflow / max_overflow_h / max_overflow_v are the Total line of
the final congestion report (GRT-0096).

Unit correction.  GRT-0018 divides the routed length in dbu by the block's
DEF units.  The ASAP7 checkpoints of aes, gcd, jpeg and swerv carry DEF units
100 with 1000 dbu/um, so their printed wirelength is 10x too large; the
factor below corrects it.  Ratios between placers on one floorplan and all
overflow figures are unaffected.
"""
import csv
import math
import os
import re
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# paper_results/ in the development checkout, results/ in the release.
# the exported CSVs: paper_results/ in the working copy, results/ in the release
PR = next(p for p in (os.path.join(ROOT, "paper_results"), os.path.join(ROOT, "results"))
          if os.path.isdir(os.path.join(p, "x3_mega")))
if not os.path.isdir(PR):
    PR = os.path.join(ROOT, "results")
OUT = os.path.join(PR, "tables", "7_global_routing.md")
FEW = re.compile(r"^(?P<design>.+?)_u(?P<util>\d+)_r(?P<rows>\d+)_(?P<pdk>nangate45|asap7)$")
ROWS = [1, 2, 4, 6, 8]
ARMS = ["RePlAce", "Capo", "RB (TP)", "RB (HM)", "RB (MK)", "RB (FP)", "RB+k (TP)"]
SRC = {"RePlAce": ("e17_krow", "gpl"), "Capo": ("e17_krow", "capo"), "RB (TP)": ("e17_krow", "tp"),
       "RB (HM)": ("e17_krow", "hm"), "RB (MK)": ("e17_krow", "mk"), "RB (FP)": ("e17_krow", "fp"),
       "RB+k (TP)": ("x3_mega", "kway_tp")}
DEF_UNIT_FIX = {("aes", "asap7"): 0.1, ("gcd", "asap7"): 0.1, ("jpeg", "asap7"): 0.1,
                ("swerv_no_macro", "asap7"): 0.1}


def gm(xs):
    xs = [x for x in xs if x and x > 0]
    return math.exp(sum(map(math.log, xs)) / len(xs)) if xs else float("nan")


def load_runs(exp):
    out = defaultdict(dict)
    with open(os.path.join(PR, exp, "runs.csv")) as fh:
        for r in csv.DictReader(fh):
            out[r["design"]][r["variant"]] = r
    return out


def num(r, k):
    v = r.get(k, "") if r else ""
    return float(v) if v not in ("", None) else None


def status(r):
    if r is None or not (r.get("hpwl_dpl") and r.get("violations") in ("", "0")):
        return "no legal placement"
    if r.get("grt_wl"):
        return "routed"
    s = (r.get("routing") or "").strip()
    if s.startswith("failed"):
        return s.replace("failed: ", "")
    return "skipped" if s.startswith("skipped") else "not run"


def main():
    runs = {"e17_krow": load_runs("e17_krow"), "x3_mega": load_runs("x3_mega")}
    conv = {}
    with open(os.path.join(PR, "x3_mega", "replace_convergence.csv")) as fh:
        for r in csv.DictReader(fh):
            conv[r["design"]] = r["converged"] == "1" and r["legalization_failed"] == "0"
    fps = sorted(n for n in conv if FEW.match(n))
    assert len(fps) == 180, len(fps)

    data = {}
    for n in fps:
        m = FEW.match(n).groupdict()
        scale = DEF_UNIT_FIX.get((m["design"], m["pdk"]), 1.0)
        d = {"rows": int(m["rows"]), "design": m["design"], "pdk": m["pdk"], "util": m["util"]}
        for a in ARMS:
            exp, var = SRC[a]
            r = runs[exp][n].get(var)
            if a == "RePlAce" and r is None:
                r = runs["x3_mega"][n].get("gpl")
            rwl = num(r, "grt_wl")
            d[a] = {"status": status(r), "rwl": rwl * scale if rwl else None, "hpwl": num(r, "hpwl_dpl"),
                    "ovf": num(r, "overflow"), "maxh": num(r, "max_overflow_h"), "maxv": num(r, "max_overflow_v")}
        gpl_legal = runs["e17_krow"][n].get("gpl") is not None or runs["x3_mega"][n].get("gpl") is not None
        d["conv"] = conv[n] and gpl_legal and d["RePlAce"]["status"] != "no legal placement"
        data[n] = d
    common = [n for n in fps if data[n]["conv"] and all(data[n][a]["rwl"] for a in ARMS)]
    placers = ARMS[1:]

    def ratio(n, a, key):
        x, y = data[n][a][key], data[n]["RePlAce"][key]
        return x / y if (x and y) else None

    def sel(k):
        return [n for n in common if k == "all" or data[n]["rows"] == k]

    P = ["# 7. Global routing on the few-row set\n",
         "Source: E17 (`e17_krow`, RePlAce / Capo / RB with TP, HM, MK, FP) and X3 (`x3_mega`, RB+k (TP)), "
         "the same runs as tables 1 and 4, one seed. After `detailed_placement` and DPO every run was "
         "global-routed with `global_route -allow_congestion -congestion_iterations 30` on the platform's "
         "signal layers (Nangate45 metal2--metal10 with a 0.5 layer adjustment, ASAP7 M2--M6 with 0.25). "
         "RWL = FastRoute's total wirelength (GRT-0018); overflow = the `Total` line of the final congestion "
         "report (GRT-0096): total overflow and the maximum per-edge overflow in the horizontal and vertical "
         "direction. HPWL = after detailed placement, before DPO (the metric of tables 1--6). "
         "Ratios are placer / RePlAce on the same floorplan, geomean over floorplans, (wins / n).\n",
         "RePlAce failure rule: a run that diverged, did not legalize or stopped above its 0.2 density-overflow "
         "target (`results/x3_mega/replace_convergence.csv`, parsed from the RePlAce logs) is excluded from "
         "every ratio; 137 of the 180 floorplans remain. The *common set* is the "
         f"{len(common)} floorplans on which, in addition, every placer's placement was routed.\n",
         "Unit note: the ASAP7 checkpoints of aes, gcd, jpeg and swerv carry DEF units 100 with 1000 dbu/um, "
         "and GRT-0018 divides by the DEF units, so their printed wirelength is 10x too large; the absolute "
         "values below multiply it by 0.1. Ratios between placers and all overflow figures are unaffected.\n",
         "Regenerate: `python3 exp/grt_tables.py`.\n"]

    # 7.1 coverage
    P.append("## 7.1 Which floorplans route\n")
    P.append("Routed = `global_route` finished and reported a wirelength. On the flattest floorplans FastRoute "
             "runs out of memory or crashes (`not run`); X3 skipped routing (`MCP_NO_ROUTE=1`) on the 60 "
             "floorplans where E17 had seen it fail, so RB+k's count is a lower bound.\n")
    P.append("| rows | floorplans | RePlAce converged | routed by RePlAce | routed by RB (TP) | routed by RB+k (TP) | "
             "routed by RePlAce and RB (TP) | routed by every placer | common set |")
    P.append("|---|---|---|---|---|---|---|---|---|")
    for k in ROWS + ["all"]:
        ns = [n for n in fps if k == "all" or data[n]["rows"] == k]
        P.append(f"| {k} | {len(ns)} | {sum(data[n]['conv'] for n in ns)} | "
                 f"{sum(1 for n in ns if data[n]['RePlAce']['rwl'])} | "
                 f"{sum(1 for n in ns if data[n]['RB (TP)']['rwl'])} | "
                 f"{sum(1 for n in ns if data[n]['RB+k (TP)']['rwl'])} | "
                 f"{sum(1 for n in ns if data[n]['RePlAce']['rwl'] and data[n]['RB (TP)']['rwl'])} | "
                 f"{sum(1 for n in ns if all(data[n][a]['rwl'] for a in ARMS))} | "
                 f"{sum(1 for n in ns if n in common)} |")
    P.append("")
    P.append("| placer | routed | GRT-0228 | GRT-0229 | not run | skipped | no legal placement |")
    P.append("|---|---|---|---|---|---|---|")
    for a in ARMS:
        c = defaultdict(int)
        for n in fps:
            c[data[n][a]["status"]] += 1
        P.append(f"| {a} | {c['routed']} | {c['GRT-0228']} | {c['GRT-0229']} | {c['not run']} | {c['skipped']} | "
                 f"{c['no legal placement']} |")
    P.append("")
    P.append("The common set is the part of the few-row set FastRoute can route for every placer: aes, gcd and "
             "ibex at every row count and jpeg/Nangate45 at 4--8 rows; no cva6, swerv or jpeg/ASAP7 floorplan. "
             "It is the part of the set where the HPWL advantage is smallest (table 1), so the routed figures "
             "below understate the set-wide effect; §7.3 pairs each placer with RePlAce on every floorplan both "
             "routed.\n")

    # 7.2 HPWL vs RWL by rows
    P.append("## 7.2 HPWL and routed wirelength by row count (common set)\n")
    P.append("| rows | n | " + " | ".join(f"{a} HPWL | {a} RWL" for a in placers) + " |")
    P.append("|---|---|" + "---|---|" * len(placers))
    for k in ROWS + ["all"]:
        ns = sel(k)
        cells = [f"{gm([ratio(n, a, 'hpwl') for n in ns]):.3f} | {gm([ratio(n, a, 'rwl') for n in ns]):.3f}"
                 for a in placers]
        row = f"| {k} | {len(ns)} | " + " | ".join(cells) + " |"
        P.append(row if k != "all" else row.replace("| all |", "| **all** |"))
    P.append("")
    P.append("RB (TP) and RB+k (TP) with wins:\n")
    P.append("| rows | n | RB (TP) HPWL | RB (TP) RWL | wins | RB+k (TP) HPWL | RB+k (TP) RWL | wins | Capo RWL |")
    P.append("|---|---|---|---|---|---|---|---|---|")
    for k in ROWS + ["all"]:
        ns = sel(k)
        tp = [ratio(n, "RB (TP)", "rwl") for n in ns]
        kk = [ratio(n, "RB+k (TP)", "rwl") for n in ns]
        P.append(f"| {k} | {len(ns)} | {gm([ratio(n, 'RB (TP)', 'hpwl') for n in ns]):.3f} | {gm(tp):.3f} | "
                 f"{sum(r < 1 for r in tp)}/{len(ns)} | {gm([ratio(n, 'RB+k (TP)', 'hpwl') for n in ns]):.3f} | "
                 f"{gm(kk):.3f} | {sum(r < 1 for r in kk)}/{len(ns)} | {gm([ratio(n, 'Capo', 'rwl') for n in ns]):.3f} |")
    P.append("")

    # 7.3 pairwise
    P.append("## 7.3 Pairwise against RePlAce (every floorplan both routed, RePlAce converged)\n")
    P.append("| placer | n | HPWL | RWL | RWL wins | designs beyond the common set |")
    P.append("|---|---|---|---|---|---|")
    for a in placers:
        ns = [n for n in fps if data[n]["conv"] and data[n][a]["rwl"] and data[n]["RePlAce"]["rwl"]]
        rs = [ratio(n, a, "rwl") for n in ns]
        extra = sorted({data[n]["design"] + "/" + data[n]["pdk"] for n in ns if n not in common})
        P.append(f"| {a} | {len(ns)} | {gm([ratio(n, a, 'hpwl') for n in ns]):.3f} | {gm(rs):.3f} | "
                 f"{sum(r < 1 for r in rs)}/{len(ns)} | {', '.join(extra) if extra else '--'} |")
    P.append("")

    # 7.4 overflow by rows
    P.append("## 7.4 Overflow by row count (common set)\n")
    P.append("For each row count and placer: floorplans with zero overflow / n; median and maximum total overflow "
             "over the floorplans with overflow; median max H and max V per-edge overflow over the same "
             "floorplans; and the number of floorplans where the placer's total overflow is lower / equal / "
             "higher than RePlAce's.\n")
    P.append("| rows | n | placer | zero overflow | median total (>0) | max total | median max H | median max V | "
             "lower / equal / higher than RePlAce |")
    P.append("|---|---|---|---|---|---|---|---|---|")

    def med(xs):
        xs = sorted(xs)
        return f"{int(xs[len(xs) // 2]):,}" if xs else "--"

    for k in ROWS + ["all"]:
        ns = sel(k)
        for a in ARMS:
            pos = [data[n][a] for n in ns if data[n][a]["ovf"] > 0]
            if a == "RePlAce":
                cmp_s = "--"
            else:
                lo = sum(1 for n in ns if data[n][a]["ovf"] < data[n]["RePlAce"]["ovf"])
                hi = sum(1 for n in ns if data[n][a]["ovf"] > data[n]["RePlAce"]["ovf"])
                cmp_s = f"{lo} / {len(ns) - lo - hi} / {hi}"
            P.append(f"| {k} | {len(ns)} | {a} | {len(ns) - len(pos)}/{len(ns)} | {med([p['ovf'] for p in pos])} | "
                     f"{int(max(p['ovf'] for p in pos)) if pos else '--':,} | {med([p['maxh'] for p in pos])} | "
                     f"{med([p['maxv'] for p in pos])} | {cmp_s} |")
        if k != "all":
            P.append("| | | | | | | | | |")
    P.append("")
    P.append("Congestion follows the floorplan, not the placer: the Nangate45 floorplans of aes, ibex and jpeg "
             "overflow for every placer, the ASAP7 floorplans and gcd for none (ibex/ASAP7 is the one mixed "
             "case). Max V overflow is 0--2 everywhere; the congestion is along the rows.\n")

    # 7.5 by design
    P.append("## 7.5 Overflow by design and PDK (common set)\n")
    P.append("Floorplans with overflow / n, then the geomean of (1 + total overflow) / (1 + RePlAce's) over the "
             "floorplans where either has overflow (below 1 = less congestion than RePlAce).\n")
    P.append("| design | PDK | n | RePlAce | " + " | ".join(placers) + " |")
    P.append("|---|---|---|---|" + "---|" * len(placers))
    for d, p in sorted({(data[n]["design"], data[n]["pdk"]) for n in common}):
        ns = [n for n in common if data[n]["design"] == d and data[n]["pdk"] == p]
        cells = []
        for a in placers:
            s = [n for n in ns if data[n][a]["ovf"] > 0 or data[n]["RePlAce"]["ovf"] > 0]
            g = gm([(1 + data[n][a]["ovf"]) / (1 + data[n]["RePlAce"]["ovf"]) for n in s])
            cells.append(f"{sum(1 for n in ns if data[n][a]['ovf'] > 0)}/{len(ns)}, {g:.2f}" if s
                         else f"0/{len(ns)}, --")
        P.append(f"| {d} | {p} | {len(ns)} | {sum(1 for n in ns if data[n]['RePlAce']['ovf'] > 0)}/{len(ns)} | "
                 + " | ".join(cells) + " |")
    P.append("")

    # 7.6 per floorplan
    P.append("## 7.6 Per floorplan (common set)\n")
    P.append("Routed wirelength in thousands of um; then total overflow (max H / max V), lowest total in bold.\n")
    P.append("| floorplan | rows | " + " | ".join(ARMS) + " |")
    P.append("|---|---|" + "---|" * len(ARMS))
    for n in common:
        P.append(f"| {n} | {data[n]['rows']} | " + " | ".join(f"{data[n][a]['rwl'] / 1000:,.0f}" for a in ARMS) + " |")
    P.append("")
    P.append("| floorplan | rows | " + " | ".join(ARMS) + " |")
    P.append("|---|---|" + "---|" * len(ARMS))
    for n in common:
        best = min(data[n][a]["ovf"] for a in ARMS)
        cells = []
        for a in ARMS:
            r = data[n][a]
            s = f"{int(r['ovf']):,} ({int(r['maxh'])}/{int(r['maxv'])})"
            cells.append(f"**{s}**" if r["ovf"] == best else s)
        P.append(f"| {n} | {data[n]['rows']} | " + " | ".join(cells) + " |")
    P.append("")

    with open(OUT, "w") as fh:
        fh.write("\n".join(P) + "\n")
    print(f"wrote {OUT} ({len(common)} common floorplans)")


if __name__ == "__main__":
    main()
