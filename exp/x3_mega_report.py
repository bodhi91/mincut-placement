#!/usr/bin/env python3
"""Tables and figures for the X3 mega grid (results/x3_mega) and the AM-1
American-map study (results/am1), as consumed by RESULTS_SUMMARY.md.

    python3 exp/x3_mega_report.py [results/x3_mega] [results/am1] [plots/x3_mega]

Writes <plots>/tables.md (every table, GitHub markdown) and the figures

    x3_hpwl_vs_rows.png     geomean HPWL / gpl against rows, one panel per design,
                            cutnet_tp and kway_tp, platforms and utilizations pooled
    x3_box_rows_util.png    distribution of kway_tp / gpl by rows and by utilization
    x3_heatmap.png          every k-row floorplan: kway_tp / gpl; gpl failures hatched
    x3_kway_vs_cutnet.png   kway_tp / gpl against cutnet_tp / gpl, every floorplan
    x3_routed_vs_hpwl.png   routed WL / gpl against post-DPO HPWL / gpl
    x3_base_bars.png        the AR-1 base floorplans, HPWL and routed WL per design
    x3_kway_anatomy.png     the whole-level sweep per level: acceptance, moved cells,
                            W_grid gain, seconds
    x3_runtime.png          placer seconds against movable cells, all arms
    x3_am1_arms.png         AM-1: every arm against gpl on the six designs

A ratio below 1 is better than gpl. Geomeans are over floorplans that have a
gpl result; the x3 harness records a PART when the placement is legal but
global routing failed, so routed-WL n is smaller than HPWL n.
"""

import collections
import math
import os
import re
import statistics
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from american_maps import failures, gmean, load  # noqa: E402

ARMS = ["cutnet_tp", "kway_tp"]
COL = {"gpl": "#000000", "cutnet_tp": "#1f77b4", "kway_tp": "#d62728"}
LBL = {"gpl": "gpl (RePlAce) = 1", "cutnet_tp": "cutnet_tp (bisection only)",
       "kway_tp": "kway_tp (+ whole-level k-way sweeps)"}
KROW_DESIGNS = ["gcd", "aes", "ibex", "jpeg", "swerv_no_macro", "cva6_no_macro"]
BASE_DESIGNS = ["gcd", "aes", "ibex", "jpeg", "swerv_wrapper", "cva6", "mempool_tile",
                "xiangshan", "megaboom", "mempool_group"]
PLATFORMS = ["nangate45", "asap7"]
ROWS = [1, 2, 4, 6, 8]
UTILS = [60, 70, 80]
KROW_RE = re.compile(r"^(.+)_u(\d+)_r(\d+)_(nangate45|asap7)$")
BASE_RE = re.compile(r"^(.+)_ar1_(nangate45|asap7)$")


def parse(design):
    m = KROW_RE.match(design)
    if m:
        return dict(set="krow", design=m.group(1), util=int(m.group(2)), rows=int(m.group(3)),
                    platform=m.group(4))
    m = BASE_RE.match(design)
    if m:
        return dict(set="base", design=m.group(1), util=None, rows=None, platform=m.group(2))
    return None


def subset(fp):
    if fp["set"] == "krow":
        return "krow_big" if fp["design"] in ("swerv_no_macro", "cva6_no_macro") else "krow_small"
    return "base_big" if fp["design"] in ("megaboom", "mempool_group") else "base_small"


def final(data, design, arm):
    d = data.get(design, {}).get(arm, {}).get(1)
    return d["final"] if d else None


def movable(doc):
    n = (doc.get("netlist") or {}).get("movable")
    return n if n else (doc.get("mcp_result") or {}).get("cells")


def routed(f):
    return f.get("grt_wl") if f and f.get("routing") == "ok" and f.get("grt_wl") else None


def ratio_rows(data, pred):
    """[(design, fp, {arm: hpwl ratio}, {arm: routed ratio}, {arm: final})] over gpl-baselined floorplans."""
    out = []
    for design in sorted(data):
        fp = parse(design)
        if not fp or not pred(fp):
            continue
        g = final(data, design, "gpl")
        fins = {a: final(data, design, a) for a in ARMS}
        h = {a: fins[a]["hpwl"] / g["hpwl"] for a in ARMS if g and fins[a]}
        r = {a: routed(fins[a]) / routed(g) for a in ARMS if g and fins[a] and routed(fins[a]) and routed(g)}
        out.append((design, fp, h, r, fins, g))
    return out


def gm_table(rows, key):
    """key: 'h' or 'r' -> {arm: (geomean, n)}"""
    idx = 2 if key == "h" else 3
    out = {}
    for a in ARMS:
        vals = [row[idx][a] for row in rows if a in row[idx]]
        out[a] = (gmean(vals), len(vals))
    return out


def fmt(g, n):
    return "–" if n == 0 else f"{g:.3f} ({n})"


def paired(rows):
    """kway_tp / cutnet_tp on floorplans where both placed (no gpl needed)."""
    vals = []
    for _, _, _, _, fins, _ in rows:
        a, b = fins.get("cutnet_tp"), fins.get("kway_tp")
        if a and b:
            vals.append(b["hpwl"] / a["hpwl"])
    wins = sum(v < 1 for v in vals)
    return gmean(vals), wins, len(vals)


# --------------------------------------------------------------------------- tables
def table_accounting(data, fails, md):
    md.append("### Completion and failure accounting (x3_mega, seed 1)\n")
    md.append("| set | arm | ok (placed + routed, or routing skipped) | PART (placed, routing failed) | FAIL | FAIL classes | running |")
    md.append("|---|---|---:|---:|---:|---|---:|")
    all_designs = set(data) | set(fails)
    for sub in ("krow_small", "krow_big", "base_small", "base_big"):
        for arm in ("gpl", "cutnet_tp", "kway_tp", "gpl_dia"):
            ok = part = run = 0
            cls = collections.Counter()
            for design in all_designs:
                fp = parse(design)
                if not fp or subset(fp) != sub:
                    continue
                f = final(data, design, arm)
                if f:
                    if f.get("routing") == "ok" or str(f.get("routing", "")).startswith("skipped"):
                        ok += 1
                    else:
                        part += 1
                else:
                    c = fails.get(design, {}).get(arm, {}).get(1)
                    if c == "running":
                        run += 1
                    elif c:
                        cls[c] += 1
            if ok + part + run + sum(cls.values()) == 0:
                continue
            md.append(f"| {sub} | {arm} | {ok} | {part} | {sum(cls.values())} | "
                      f"{', '.join(f'{k} ×{v}' for k, v in cls.most_common())} | {run} |")
    md.append("\nkrow_big runs are not routed by design (`MCP_NO_ROUTE=1`, E17 found global route exhausting 100+ GB on them); they count as ok when placed.\n")


def table_krow(data, md):
    rows = ratio_rows(data, lambda fp: fp["set"] == "krow")
    md.append("### k-row set: geomean ratio to gpl (post-DPO HPWL; routed wirelength)\n")
    md.append("| slice | n floorplans (gpl ok / total) | cutnet_tp HPWL | kway_tp HPWL | cutnet_tp routed | kway_tp routed | kway_tp / cutnet_tp (wins) |")
    md.append("|---|---:|---:|---:|---:|---:|---:|")

    def line(name, sel):
        sub = [r for r in rows if sel(r[1])]
        if not sub:
            return
        h, r = gm_table(sub, "h"), gm_table(sub, "r")
        pg, w, n = paired(sub)
        md.append(f"| {name} | {h['kway_tp'][1]} / {len(sub)} | {fmt(*h['cutnet_tp'])} | {fmt(*h['kway_tp'])} | "
                  f"{fmt(*r['cutnet_tp'])} | {fmt(*r['kway_tp'])} | {pg:.3f} ({w}/{n}) |")

    line("**all k-row**", lambda fp: True)
    for p in PLATFORMS:
        line(p, lambda fp, p=p: fp["platform"] == p)
    for u in UTILS:
        line(f"utilization {u} %", lambda fp, u=u: fp["util"] == u)
    for r in ROWS:
        line(f"{r} row{'s' if r > 1 else ''}", lambda fp, r=r: fp["rows"] == r)
    for d in KROW_DESIGNS:
        line(d, lambda fp, d=d: fp["design"] == d)
    md.append("\n`kway_tp / cutnet_tp` is the paired geomean over floorplans where both min-cut arms placed (no gpl needed); wins = floorplans where the sweep lowered post-DPO HPWL.\n")
    return rows


def table_krow_rows_by_design(data, md):
    rows = ratio_rows(data, lambda fp: fp["set"] == "krow")
    md.append("### k-row set: kway_tp / gpl by design and rows (platforms and utilizations pooled, geomean, n)\n")
    md.append("| design | " + " | ".join(f"r{r}" for r in ROWS) + " |")
    md.append("|---|" + "---:|" * len(ROWS))
    for d in KROW_DESIGNS:
        cells = []
        for r in ROWS:
            vals = [row[2]["kway_tp"] for row in rows
                    if row[1]["design"] == d and row[1]["rows"] == r and "kway_tp" in row[2]]
            tot = sum(1 for row in rows if row[1]["design"] == d and row[1]["rows"] == r)
            cells.append(f"{gmean(vals):.3f} ({len(vals)}/{tot})" if vals else f"– (0/{tot})")
        md.append(f"| {d} | " + " | ".join(cells) + " |")
    md.append("\n(n = floorplans with a gpl result / floorplans in the cell; 6 per cell = 2 platforms × 3 utilizations.)\n")


def table_gpl_failures(data, fails, md):
    md.append("### k-row floorplans without a gpl baseline\n")
    md.append("| floorplan | gpl | gpl_dia (diamond legalizer) | cutnet_tp / kway_tp HPWL (µm) |")
    md.append("|---|---|---|---|")
    n = 0
    for design in sorted(set(data) | set(fails)):
        fp = parse(design)
        if not fp or fp["set"] != "krow" or final(data, design, "gpl"):
            continue
        n += 1
        g = fails.get(design, {}).get("gpl", {}).get(1, "not run")
        dia = final(data, design, "gpl_dia")
        dia_s = f"ok, HPWL {dia['hpwl']:.0f}" if dia else fails.get(design, {}).get("gpl_dia", {}).get(1, "not run")
        a, b = final(data, design, "cutnet_tp"), final(data, design, "kway_tp")
        md.append(f"| {design} | {g} | {dia_s} | {a['hpwl']:.0f} / {b['hpwl']:.0f} |" if a and b else
                  f"| {design} | {g} | {dia_s} | – |")
    md.append(f"\n{n} floorplans. DPL-0033: the default legalizer leaves overlaps; DPL-0036: the diamond legalizer gives up; GPL-0305: RePlAce diverged (1-row asap7).\n")


def table_base(data, md):
    rows = ratio_rows(data, lambda fp: fp["set"] == "base")
    md.append("### Base set (AR-1 ORFS floorplans): ratio to gpl per floorplan\n")
    md.append("| floorplan | cells | gpl HPWL (µm) | cutnet_tp HPWL | kway_tp HPWL | cutnet_tp routed | kway_tp routed | t_place gpl / cutnet / kway (s) |")
    md.append("|---|---:|---:|---:|---:|---:|---:|---|")
    for d in BASE_DESIGNS:
        for p in PLATFORMS:
            name = f"{d}_ar1_{p}"
            match = [r for r in rows if r[0] == name]
            doc = data.get(name, {})
            cells = next((movable(v[1]) for v in doc.values() if 1 in v and movable(v[1])), None)
            if not match or not match[0][5]:
                if doc:
                    arms = ", ".join(sorted(doc))
                    md.append(f"| {name} | {cells or '–'} | – | – | – | – | – | only {arms} finished |")
                continue
            _, fp, h, r, fins, g = match[0]
            t = " / ".join(f"{x['t_place']:.0f}" if x else "–" for x in (g, fins["cutnet_tp"], fins["kway_tp"]))
            v = lambda dct, a: f"{dct[a]:.3f}" if a in dct else "– (PART)"
            md.append(f"| {name} | {cells} | {g['hpwl']:.0f} | "
                      f"{v(h, 'cutnet_tp')} | {v(h, 'kway_tp')} | {v(r, 'cutnet_tp')} | {v(r, 'kway_tp')} | {t} |")
    h, r = gm_table(rows, "h"), gm_table(rows, "r")
    pg, w, n = paired(rows)
    md.append(f"| **geomean** | | | **{fmt(*h['cutnet_tp'])}** | **{fmt(*h['kway_tp'])}** | "
              f"**{fmt(*r['cutnet_tp'])}** | **{fmt(*r['kway_tp'])}** | kway/cutnet {pg:.3f} ({w}/{n} wins) |")
    md.append("")
    return rows


def kway_levels(data, pred):
    """per level: lists of (accepted, moved fraction, gain %, seconds, total place seconds)."""
    per = collections.defaultdict(list)
    for design, arms in data.items():
        fp = parse(design)
        if not fp or not pred(fp) or 1 not in arms.get("kway_tp", {}):
            continue
        doc = arms["kway_tp"][1]
        cells = movable(doc) or 1
        tp = doc["final"]["t_place"]
        for lv in doc["levels"]:
            if lv.get("kway_k", 0) == 0:
                continue
            before, after = lv["kway_hpwl_before_um"], lv["kway_hpwl_after_um"]
            gain = (before - after) / before * 100 if before else 0.0
            # the sweep over level l+1's k = 2^(l+1) regions is recorded on level l's entry
            per[int(round(math.log2(lv["kway_k"])))].append((lv["kway_accepted"], lv["kway_moved"] / cells, gain, lv["kway_seconds"], tp))
    return per


def table_kway_anatomy(data, md):
    md.append("### The whole-level k-way sweep, per level (kway_tp runs, `-kway_refine_levels {2 3 4 5}`)\n")
    md.append("| set | level (k) | sweeps | accepted | mean cells moved (accepted) | mean W_grid gain (accepted) | median seconds | share of t_place |")
    md.append("|---|---|---:|---:|---:|---:|---:|---:|")
    out = {}
    for name, pred in (("k-row", lambda fp: fp["set"] == "krow"), ("base", lambda fp: fp["set"] == "base")):
        per = kway_levels(data, pred)
        out[name] = per
        for level in sorted(per):
            v = per[level]
            acc = [x for x in v if x[0]]
            k = 2 ** level
            moved = statistics.mean(x[1] for x in acc) * 100 if acc else float("nan")
            gain = statistics.mean(x[2] for x in acc) if acc else float("nan")
            secs = statistics.median(x[3] for x in v)
            share = statistics.mean(x[3] / x[4] for x in v if x[4]) * 100
            md.append(f"| {name} | {level} ({k}) | {len(v)} | {len(acc)} ({len(acc) / len(v) * 100:.0f} %) | "
                      f"{moved:.1f} % | {gain:.2f} % | {secs:.1f} | {share:.1f} % |")
    md.append("\nW_grid gain is the placer's own estimate (HPWL with every cell at its region center) before and after the sweep; a sweep is accepted only if it falls. Share of t_place sums the level's sweep seconds over the whole placement time.\n")
    return out


def table_runtime(data, md):
    md.append("### Runtime (placer seconds, median over floorplans; gpl includes its own legalization)\n")
    md.append("| set | gpl | cutnet_tp | kway_tp | kway_tp / cutnet_tp |")
    md.append("|---|---:|---:|---:|---:|")
    for sub in ("krow_small", "krow_big", "base_small", "base_big"):
        t = {a: [] for a in ("gpl",) + tuple(ARMS)}
        rat = []
        for design, arms in data.items():
            fp = parse(design)
            if not fp or subset(fp) != sub:
                continue
            for a in t:
                f = final(data, design, a)
                if f:
                    t[a].append(f["t_place"])
            a, b = final(data, design, "cutnet_tp"), final(data, design, "kway_tp")
            if a and b:
                rat.append(b["t_place"] / a["t_place"])
        if not any(t.values()):
            continue
        md.append(f"| {sub} | " + " | ".join(f"{statistics.median(t[a]):.0f}" if t[a] else "–" for a in t)
                  + f" | {gmean(rat):.2f} |")
    md.append("")


AM_ARMS = [
    ("gpl", "RePlAce, the reference"),
    ("X", "min-cut placer, bisection only (= cutnet_tp)"),
    ("X_kway", "min-cut placer with k-way sweeps (= kway_tp)"),
    ("seed", "gpl seed (overflow 0.3) legalized alone"),
    ("seed50", "gpl seed (overflow 0.5) legalized alone"),
    ("seed70", "gpl seed (overflow 0.7) legalized alone"),
    ("seed_dia", "seed, diamond legalizer"),
    ("AM2_t30", "American map of the gpl seed (overflow 0.3), every level"),
    ("AM2_t50", "American map, seed overflow 0.5"),
    ("AM2_t70", "American map, seed overflow 0.7"),
    ("AMg", "American map of the converged gpl placement"),
    ("AMc", "American map of a Capo placement"),
    ("MC_diag", "min-cut decides every level, transport only diagnosed"),
    ("AM2_L2", "American map above level 2, min-cut below"),
    ("AM2_L4", "American map above level 4, min-cut below"),
    ("AM2_L6", "American map above level 6, min-cut below"),
    ("AM2_not1", "American map everywhere except level 1"),
    ("AM2_not2", "… except level 2"),
    ("AM2_not3", "… except level 3"),
    ("AM2_not4", "… except level 4"),
    ("AM2_not5", "… except level 5"),
    ("AM2_not6", "… except level 6"),
    ("AM4_t30", "American map with quadrisection (k = 4)"),
    ("AMl_t30", "net-aware transport (net weight 0.5)"),
    ("AMx", "feedback: pass 2 re-seeded by the placer's own pass-1 result"),
    ("X_sbi", "X with soft blockages ignored (swerv only)"),
    ("AM2_t30_sbi", "AM2_t30 with soft blockages ignored (swerv only)"),
]
AM_SHORT = {
    "aes_ar1_nangate45": "aes sq", "ibex_ar1_asap7": "ibex sq", "jpeg_ar1_nangate45": "jpeg sq",
    "swerv_wrapper_ar1_nangate45": "swerv macros", "ibex_u70_r8_asap7": "ibex 8-row",
    "jpeg_u70_r4_nangate45": "jpeg 4-row",
}


def am_ratio(am, design, arm, key="hpwl"):
    """mean over seeds of arm/gpl (same seed), or None; counts fails."""
    vals = []
    for seed, doc in am.get(design, {}).get(arm, {}).items():
        g = am.get(design, {}).get("gpl", {}).get(seed)
        v, gv = doc["final"].get(key), g["final"].get(key) if g else None
        if key == "grt_wl":
            v = routed(doc["final"])
            gv = routed(g["final"]) if g else None
        if v and gv:
            vals.append(v / gv)
    return vals


def table_am1(am, amfails, md):
    md.append("### AM-1: every arm, mean over 3 seeds of (arm / gpl, same seed), post-DPO HPWL\n")
    designs = list(AM_SHORT)
    md.append("| arm | what it is | " + " | ".join(AM_SHORT[d] for d in designs) + " |")
    md.append("|---|---|" + "---:|" * len(designs))
    for arm, what in AM_ARMS:
        cells = []
        any_ = False
        for d in designs:
            vals = am_ratio(am, d, arm)
            nf = sum(1 for s, c in amfails.get(d, {}).get(arm, {}).items() if c != "running")
            if vals:
                any_ = True
                s = f"{statistics.mean(vals):.3f}"
                if len(vals) > 1:
                    s += f" ±{statistics.stdev(vals):.3f}"
                if nf:
                    s += f" ({nf} FAIL)"
            elif nf:
                any_ = True
                s = f"FAIL ×{nf}"
            else:
                s = "–"
            cells.append(s)
        if any_:
            md.append(f"| {arm} | {what} | " + " | ".join(cells) + " |")
    md.append("\n± is the standard deviation over seeds. Squares: aes sq, ibex sq, jpeg sq; macros: swerv; k-row: ibex 8-row, jpeg 4-row.\n")

    md.append("### AM-1: routed wirelength, mean over seeds of (arm / gpl)\n")
    md.append("| arm | " + " | ".join(AM_SHORT[d] for d in designs) + " |")
    md.append("|---|" + "---:|" * len(designs))
    for arm, _ in AM_ARMS:
        cells, any_ = [], False
        for d in designs:
            vals = am_ratio(am, d, arm, "grt_wl")
            if vals:
                any_ = True
                cells.append(f"{statistics.mean(vals):.3f} ({len(vals)})")
            else:
                cells.append("–")
        if any_:
            md.append(f"| {arm} | " + " | ".join(cells) + " |")
    md.append("\n(n) = seeds where both the arm and gpl routed; the k-row designs route only partially.\n")

    md.append("### AM-1: placer seconds (mean over seeds)\n")
    md.append("| arm | " + " | ".join(AM_SHORT[d] for d in designs) + " |")
    md.append("|---|" + "---:|" * len(designs))
    for arm in ("gpl", "X", "X_kway", "AM2_t30", "AMg", "AMc", "MC_diag", "AMx"):
        cells = []
        for d in designs:
            ts = [doc["final"]["t_place"] for doc in am.get(d, {}).get(arm, {}).values()]
            cells.append(f"{statistics.mean(ts):.0f}" if ts else "–")
        md.append(f"| {arm} | " + " | ".join(cells) + " |")
    md.append("\nAM2_t30 and MC_diag run with `-am_diagnose`, which solves the min-cut split at every level as well as the transport, so they cost as much as X; AMg (no diagnostic) shows the American map's own cost, 2-5x below X. AMc includes the Capo seed run.\n")


# --------------------------------------------------------------------------- figures
def fig_hpwl_vs_rows(rows, out):
    fig, axes = plt.subplots(2, 3, figsize=(12, 6.5), sharey=True)
    for ax, d in zip(axes.flat, KROW_DESIGNS):
        for a in ARMS:
            ys, ns = [], []
            for r in ROWS:
                vals = [row[2][a] for row in rows if row[1]["design"] == d and row[1]["rows"] == r and a in row[2]]
                ys.append(gmean(vals) if vals else np.nan)
                ns.append(len(vals))
            ax.plot(ROWS, ys, "-o", color=COL[a], label=LBL[a], ms=5)
            if a == "kway_tp":
                for r, y, n in zip(ROWS, ys, ns):
                    if n:
                        ax.annotate(f"n={n}", (r, y), textcoords="offset points", xytext=(0, 6),
                                    ha="center", fontsize=7, color="gray")
        ax.axhline(1, color="k", ls="--", lw=1)
        ax.set_title(d)
        ax.set_xticks(ROWS)
        ax.set_xlabel("rows")
        ax.grid(alpha=0.3)
    axes[0, 0].set_ylabel("post-DPO HPWL / gpl (geomean)")
    axes[1, 0].set_ylabel("post-DPO HPWL / gpl (geomean)")
    h, l = axes[0, 0].get_legend_handles_labels()
    fig.legend(h, l, loc="lower center", ncol=2, fontsize=9, frameon=False)
    fig.suptitle("x3_mega k-row set: HPWL relative to gpl against row count (platforms and utilizations pooled)")
    fig.tight_layout(rect=(0, 0.05, 1, 1))
    fig.savefig(os.path.join(out, "x3_hpwl_vs_rows.png"), dpi=130)
    plt.close(fig)


def fig_box(rows, out):
    fig, axes = plt.subplots(1, 3, figsize=(13, 4.2))
    for ax, (key, cats, title) in zip(axes, (("rows", ROWS, "rows"), ("util", UTILS, "utilization %"),
                                             ("platform", PLATFORMS, "platform"))):
        pos = np.arange(len(cats))
        for i, a in enumerate(ARMS):
            vals = [[row[2][a] for row in rows if row[1][key] == c and a in row[2]] for c in cats]
            bp = ax.boxplot(vals, positions=pos + (i - 0.5) * 0.35, widths=0.3, patch_artist=True,
                            showfliers=True, flierprops=dict(ms=3, marker="."))
            for b in bp["boxes"]:
                b.set(facecolor=COL[a], alpha=0.45)
            for m in bp["medians"]:
                m.set(color="k")
        ax.axhline(1, color="k", ls="--", lw=1)
        ax.set_xticks(pos)
        ax.set_xticklabels([str(c) for c in cats])
        ax.set_xlabel(title)
        ax.grid(axis="y", alpha=0.3)
    axes[0].set_ylabel("post-DPO HPWL / gpl")
    from matplotlib.patches import Patch
    axes[0].legend(handles=[Patch(facecolor=COL[a], alpha=0.45, label=LBL[a]) for a in ARMS], fontsize=8)
    fig.suptitle("x3_mega k-row set: distribution of HPWL / gpl over floorplans with a gpl result")
    fig.tight_layout()
    fig.savefig(os.path.join(out, "x3_box_rows_util.png"), dpi=130)
    plt.close(fig)


def fig_heatmap(data, fails, out):
    cols = [(d, p, u) for d in KROW_DESIGNS for p in PLATFORMS for u in UTILS]
    mat = np.full((len(ROWS), len(cols)), np.nan)
    nogpl = np.zeros_like(mat, dtype=bool)
    for j, (d, p, u) in enumerate(cols):
        for i, r in enumerate(ROWS):
            name = f"{d}_u{u}_r{r}_{p}"
            g, k = final(data, name, "gpl"), final(data, name, "kway_tp")
            if g and k:
                mat[i, j] = k["hpwl"] / g["hpwl"]
            elif k and not g:
                nogpl[i, j] = True
    fig, ax = plt.subplots(figsize=(15, 4.2))
    im = ax.imshow(mat, aspect="auto", cmap="RdYlGn_r", vmin=0.3, vmax=1.3)
    for i in range(len(ROWS)):
        for j in range(len(cols)):
            if nogpl[i, j]:
                ax.add_patch(plt.Rectangle((j - 0.5, i - 0.5), 1, 1, fill=False, hatch="////", edgecolor="gray", lw=0))
            elif not np.isnan(mat[i, j]):
                ax.text(j, i, f"{mat[i, j]:.2f}", ha="center", va="center", fontsize=6)
    ax.set_yticks(range(len(ROWS)))
    ax.set_yticklabels([f"r{r}" for r in ROWS])
    ax.set_xticks(range(len(cols)))
    ax.set_xticklabels([f"{'n45' if p == 'nangate45' else 'a7'} u{u}" for _, p, u in cols], rotation=90, fontsize=7)
    for k, d in enumerate(KROW_DESIGNS):
        ax.text(k * 6 + 2.5, -0.75, d, ha="center", va="bottom", fontsize=9, fontweight="bold")
        if k:
            ax.axvline(k * 6 - 0.5, color="k", lw=1)
    fig.colorbar(im, ax=ax, label="kway_tp HPWL / gpl")
    ax.set_title("x3_mega k-row set, every floorplan (hatched: gpl produced no legal placement)", pad=30)
    fig.tight_layout()
    fig.savefig(os.path.join(out, "x3_heatmap.png"), dpi=130)
    plt.close(fig)


def fig_kway_vs_cutnet(krow, base, out):
    fig, ax = plt.subplots(figsize=(6.2, 6))
    for rows, mk, lab in ((krow, "o", "k-row floorplans"), (base, "s", "base (AR-1) floorplans")):
        xs = [r[2]["cutnet_tp"] for r in rows if "cutnet_tp" in r[2] and "kway_tp" in r[2]]
        ys = [r[2]["kway_tp"] for r in rows if "cutnet_tp" in r[2] and "kway_tp" in r[2]]
        ax.scatter(xs, ys, s=18, marker=mk, alpha=0.7, label=f"{lab} (n={len(xs)})")
    lim = [0.3, 1.4]
    ax.plot(lim, lim, "k--", lw=1, label="no change")
    ax.plot(lim, [l * 0.98 for l in lim], "k:", lw=1, label="−2 %")
    ax.set_xscale("log")
    ax.set_yscale("log")
    ticks = [0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0, 1.1, 1.2, 1.3]
    for setter, fmt_ in ((ax.set_xticks, ax.set_xticklabels), (ax.set_yticks, ax.set_yticklabels)):
        setter(ticks)
        fmt_([str(t) for t in ticks])
    ax.minorticks_off()
    ax.set_xlim(lim)
    ax.set_ylim(lim)
    ax.set_xlabel("cutnet_tp HPWL / gpl")
    ax.set_ylabel("kway_tp HPWL / gpl")
    ax.set_title("Effect of the whole-level k-way sweeps, floorplan by floorplan")
    ax.grid(alpha=0.3)
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(out, "x3_kway_vs_cutnet.png"), dpi=130)
    plt.close(fig)


def fig_routed_vs_hpwl(krow, base, out):
    fig, ax = plt.subplots(figsize=(6.5, 5.5))
    for rows, mk, lab in ((krow, "o", "k-row"), (base, "s", "base")):
        for a in ARMS:
            xs = [r[2][a] for r in rows if a in r[3]]
            ys = [r[3][a] for r in rows if a in r[3]]
            ax.scatter(xs, ys, s=16, marker=mk, color=COL[a], alpha=0.65, label=f"{a}, {lab} (n={len(xs)})")
    lim = [0.6, 1.5]
    ax.plot(lim, lim, "k--", lw=1)
    ax.set_xlim(lim)
    ax.set_ylim(lim)
    ax.set_xlabel("post-DPO HPWL / gpl")
    ax.set_ylabel("global-routed wirelength / gpl")
    ax.set_title("HPWL gains carry to routed wirelength, damped")
    ax.grid(alpha=0.3)
    ax.legend(fontsize=7)
    fig.tight_layout()
    fig.savefig(os.path.join(out, "x3_routed_vs_hpwl.png"), dpi=130)
    plt.close(fig)


def fig_base_bars(base, out):
    names = [f"{d}_ar1_{p}" for d in BASE_DESIGNS for p in PLATFORMS]
    names = [n for n in names if any(r[0] == n and r[2] for r in base)]
    fig, axes = plt.subplots(2, 1, figsize=(12, 7), sharex=True)
    x = np.arange(len(names))
    for ax, idx, ylab in ((axes[0], 2, "post-DPO HPWL / gpl"), (axes[1], 3, "routed WL / gpl")):
        for i, a in enumerate(ARMS):
            vals = []
            for n in names:
                row = next(r for r in base if r[0] == n)
                vals.append(row[idx].get(a, np.nan))
            ax.bar(x + (i - 0.5) * 0.38, vals, 0.36, color=COL[a], label=LBL[a])
            for xi, v in zip(x, vals):
                if not np.isnan(v):
                    ax.text(xi + (i - 0.5) * 0.38, v + 0.005, f"{v:.2f}", ha="center", fontsize=6, rotation=90)
        ax.axhline(1, color="k", ls="--", lw=1)
        ax.set_ylabel(ylab)
        ax.set_ylim(0.9, 1.55)
        ax.grid(axis="y", alpha=0.3)
    axes[0].legend(fontsize=8)
    axes[1].set_xticks(x)
    axes[1].set_xticklabels([n.replace("_ar1_", "\n") for n in names], fontsize=7)
    fig.suptitle("x3_mega base set (ORFS AR-1 floorplans, soft blockages blocked): ratio to gpl")
    fig.tight_layout()
    fig.savefig(os.path.join(out, "x3_base_bars.png"), dpi=130)
    plt.close(fig)


def fig_kway_anatomy(per, out):
    fig, axes = plt.subplots(1, 4, figsize=(14, 3.8))
    for name, color in (("k-row", "#1f77b4"), ("base", "#d62728")):
        lv = sorted(per[name])
        acc = [sum(x[0] for x in per[name][l]) / len(per[name][l]) * 100 for l in lv]
        moved = [statistics.mean([x[1] for x in per[name][l] if x[0]] or [0]) * 100 for l in lv]
        gain = [statistics.mean([x[2] for x in per[name][l] if x[0]] or [0]) for l in lv]
        secs = [statistics.median(x[3] for x in per[name][l]) for l in lv]
        for ax, ys, ylab in zip(axes, (acc, moved, gain, secs),
                                ("sweeps accepted (%)", "cells moved, accepted sweeps (%)",
                                 "W_grid gain, accepted sweeps (%)", "median seconds per sweep")):
            ax.plot(lv, ys, "-o", color=color, label=name)
            ax.set_ylabel(ylab)
            ax.set_xticks(lv)
            ax.set_xticklabels([f"L{l}\nk={2 ** l}" for l in lv])
            ax.grid(alpha=0.3)
    axes[3].set_yscale("log")
    axes[0].legend()
    fig.suptitle("The whole-level k-way sweep by level (kway_tp runs)")
    fig.tight_layout()
    fig.savefig(os.path.join(out, "x3_kway_anatomy.png"), dpi=130)
    plt.close(fig)


def fig_runtime(data, out):
    fig, ax = plt.subplots(figsize=(7, 5))
    for a, mk in (("gpl", "x"), ("cutnet_tp", "o"), ("kway_tp", "^")):
        xs, ys = [], []
        for design, arms in data.items():
            if parse(design) and 1 in arms.get(a, {}):
                doc = arms[a][1]
                n = movable(doc) or movable(next((arms[b][1] for b in ARMS if 1 in arms.get(b, {})), {}))
                if not n:
                    continue
                xs.append(n)
                ys.append(doc["final"]["t_place"])
        ax.scatter(xs, ys, s=14, marker=mk, color=COL[a], alpha=0.6, label=f"{a} (n={len(xs)})")
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("movable cells")
    ax.set_ylabel("placer seconds (4 threads)")
    ax.set_title("x3_mega: placement time against design size")
    ax.grid(alpha=0.3, which="both")
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(out, "x3_runtime.png"), dpi=130)
    plt.close(fig)


def fig_am1_arms(am, amfails, out):
    arms = ["X", "X_kway", "seed", "AM2_t30", "AM2_t50", "AM2_t70", "AMg", "AMc", "MC_diag",
            "AM2_L2", "AM2_L4", "AM2_L6", "AM4_t30", "AMl_t30", "AMx"]
    designs = list(AM_SHORT)
    fig, ax = plt.subplots(figsize=(14, 5))
    x = np.arange(len(arms))
    w = 0.13
    for i, d in enumerate(designs):
        means, errs, fails_x = [], [], []
        for j, a in enumerate(arms):
            vals = am_ratio(am, d, a)
            nf = sum(1 for c in amfails.get(d, {}).get(a, {}).values() if c != "running")
            means.append(statistics.mean(vals) if vals else np.nan)
            errs.append(statistics.stdev(vals) if len(vals) > 1 else 0)
            if nf and not vals:
                fails_x.append(j)
        ax.bar(x + (i - 2.5) * w, means, w, yerr=errs, capsize=1.5, label=AM_SHORT[d])
        for j in fails_x:
            ax.text(j + (i - 2.5) * w, 0.57, "F", ha="center", fontsize=7, color="C%d" % i, fontweight="bold")
    ax.axhline(1, color="k", ls="--", lw=1)
    ax.set_xticks(x)
    ax.set_xticklabels(arms, rotation=45, ha="right")
    ax.set_ylabel("post-DPO HPWL / gpl (mean ± sd over 3 seeds, log)")
    ax.set_yscale("log")
    ax.set_ylim(0.55, 3.5)
    yt = [0.6, 0.7, 0.8, 0.9, 1.0, 1.2, 1.5, 2.0, 2.5, 3.0]
    ax.set_yticks(yt)
    ax.set_yticklabels([str(t) for t in yt])
    ax.minorticks_off()
    ax.set_title("AM-1: every arm on the six designs (F = all seeds failed in legalization)")
    ax.grid(axis="y", alpha=0.3)
    ax.legend(fontsize=8, ncol=6, loc="upper center", bbox_to_anchor=(0.5, -0.22), frameon=False)
    fig.tight_layout()
    fig.savefig(os.path.join(out, "x3_am1_arms.png"), dpi=130)
    plt.close(fig)


def main():
    x3 = sys.argv[1] if len(sys.argv) > 1 else "results/x3_mega"
    am1 = sys.argv[2] if len(sys.argv) > 2 else "results/am1"
    out = sys.argv[3] if len(sys.argv) > 3 else "plots/x3_mega"
    os.makedirs(out, exist_ok=True)
    data = load(x3)
    fails = failures(x3, data)
    am = load(am1)
    amfails = failures(am1, am)
    md = []
    table_accounting(data, fails, md)
    krow = table_krow(data, md)
    table_krow_rows_by_design(data, md)
    table_gpl_failures(data, fails, md)
    base = table_base(data, md)
    per = table_kway_anatomy(data, md)
    table_runtime(data, md)
    table_am1(am, amfails, md)
    with open(os.path.join(out, "tables.md"), "w") as f:
        f.write("\n".join(md) + "\n")
    fig_hpwl_vs_rows(krow, out)
    fig_box(krow, out)
    fig_heatmap(data, fails, out)
    fig_kway_vs_cutnet(krow, base, out)
    fig_routed_vs_hpwl(krow, base, out)
    fig_base_bars(base, out)
    fig_kway_anatomy(per, out)
    fig_runtime(data, out)
    fig_am1_arms(am, amfails, out)
    print("\n".join(md))
    print(f"\nfigures and tables.md in {out}/")


if __name__ == "__main__":
    main()
