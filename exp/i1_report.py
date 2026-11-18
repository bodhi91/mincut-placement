#!/usr/bin/env python3
"""I1 (exp/i1_info.cfg): cut quality versus information.

    python3 exp/i1_report.py [results/i1_info] [plots/i1]

Writes <plots>/tables.md and the figures
    i1_cut_axis.png      HPWL/gpl against the achieved cut (deg*, ctl, imp)
    i1_info_axis.png     HPWL/gpl against the information rung (ctl, oT, oA)
    i1_objective.png     the 2x2 objective x terminals
    i1_amcut.png         cut of the gpl-derived assignment / min-cut cut, per level
    i1_levels.png        achieved degradation and cut per level
Every panel shows the per-floorplan trajectories around the geomean.
"""
import collections
import math
import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from american_maps import gmean, load  # noqa: E402

RES = sys.argv[1] if len(sys.argv) > 1 else "results/i1_info"
OUT = sys.argv[2] if len(sys.argv) > 2 else "plots/i1"
os.makedirs(OUT, exist_ok=True)

DESIGNS = [
    "aes_ar1_nangate45",
    "ibex_ar1_asap7",
    "jpeg_ar1_nangate45",
    "swerv_wrapper_ar1_nangate45",
    "cva6_ar1_asap7",
    "mempool_tile_ar1_nangate45",
]
SHORT = {d: d.replace("_ar1_nangate45", "/ng45").replace("_ar1_asap7", "/asap7").replace("swerv_wrapper", "swerv").replace("mempool_tile", "mempool") for d in DESIGNS}
CUT_ARMS = ["imp", "ctl", "deg10", "deg25", "deg50", "deg100"]
INFO_ARMS = ["ctl", "oT", "oA"]
OBJ_ARMS = [("cut-net", "ctl", "oT"), ("HPWL vectors", "hp", "hp_oT")]
ALL = ["gpl", "ctl", "imp", "deg10", "deg25", "deg50", "deg100", "oT", "oA", "hp", "hp_oT"]
DESC = {
    "gpl": "RePlAce (reference)",
    "ctl": "control: TritonPart, cut-net, standard terminals",
    "imp": "better cuts: best of 4 runs, 10 FM passes",
    "deg10": "cut degraded to >= 1.10x",
    "deg25": "cut degraded to >= 1.25x",
    "deg50": "cut degraded to >= 1.50x",
    "deg100": "cut degraded to >= 2.00x",
    "oT": "oracle terminals (gpl positions), TritonPart decides",
    "oA": "oracle assignment (gpl-derived transport decides)",
    "hp": "HPWL-vector objective, standard terminals",
    "hp_oT": "HPWL-vector objective, oracle terminals",
}

data = load(RES)


def final(design, arm, key="hpwl"):
    """Seed mean of final[key], or None."""
    docs = data.get(design, {}).get(arm, {})
    vals = [d["final"].get(key) for d in docs.values() if d["final"].get(key) is not None]
    return statistics.mean(vals) if vals else None


def ratio(design, arm, key="hpwl"):
    a, g = final(design, arm, key), final(design, "gpl", key)
    return a / g if a and g else None


def level_sum(design, arm, key, levels=None):
    """Sum of levels[].key over levels (all by default), seed mean."""
    docs = data.get(design, {}).get(arm, {})
    out = []
    for d in docs.values():
        s = 0.0
        for lv in d.get("levels", []):
            if levels is None or lv["level"] in levels:
                s += lv.get(key, 0) or 0
        out.append(s)
    return statistics.mean(out) if out else None


def total_cut(design, arm, lmax=None):
    """Nets newly cut by the bisections (cut_nets - inessential: the nets
    already spanning both children through external pins are not a
    decision), summed over the levels <= lmax, seed mean."""
    docs = data.get(design, {}).get(arm, {})
    out = []
    for d in docs.values():
        out.append(sum(lv["cut_nets"] - lv.get("inessential", 0) for lv in d.get("levels", [])
                       if lmax is None or lv["level"] <= lmax))
    return statistics.mean(out) if out else None


def fmt(x, nd=3):
    return "–" if x is None or (isinstance(x, float) and math.isnan(x)) else f"{x:.{nd}f}"


lines = []
P = lines.append

# ---------------------------------------------------------------- accounting
P("# I1: cut quality versus information\n")
P(f"Results: `{RES}`.  HPWL after detailed placement + DPO over gpl (krow mode) on the same floorplan, seed mean; geomean over floorplans.\n")
P("## Runs\n")
P("| arm | what | " + " | ".join(SHORT[d] for d in DESIGNS) + " |")
P("|---|---|" + "---:|" * len(DESIGNS))
for arm in ALL:
    P(f"| {arm} | {DESC[arm]} | " + " | ".join(str(len(data.get(d, {}).get(arm, {}))) for d in DESIGNS) + " |")
P("")

# ---------------------------------------------------------------- main table
P("## HPWL / gpl\n")
P("| arm | " + " | ".join(SHORT[d] for d in DESIGNS) + " | geomean | n |")
P("|---|" + "---:|" * (len(DESIGNS) + 2))
for arm in ALL[1:]:
    vals = [ratio(d, arm) for d in DESIGNS]
    ok = [v for v in vals if v]
    P(f"| {arm} | " + " | ".join(fmt(v) for v in vals) + f" | {fmt(gmean(ok))} | {len(ok)} |")
P("")
P("Paired against the control (arm / ctl on the same floorplan, geomean):\n")
P("| arm | " + " | ".join(SHORT[d] for d in DESIGNS) + " | geomean |")
P("|---|" + "---:|" * (len(DESIGNS) + 1))
for arm in ALL[2:]:
    vals = []
    for d in DESIGNS:
        a, c = final(d, arm), final(d, "ctl")
        vals.append(a / c if a and c else None)
    P(f"| {arm} | " + " | ".join(fmt(v) for v in vals) + f" | {fmt(gmean([v for v in vals if v]))} |")
P("")

# ---------------------------------------------------------------- routed
P("## Routed wirelength / gpl (runs whose routing finished)\n")
P("| arm | " + " | ".join(SHORT[d] for d in DESIGNS) + " | geomean |")
P("|---|" + "---:|" * (len(DESIGNS) + 1))
for arm in ALL[1:]:
    vals = [ratio(d, arm, "grt_wl") for d in DESIGNS]
    P(f"| {arm} | " + " | ".join(fmt(v) for v in vals) + f" | {fmt(gmean([v for v in vals if v]))} |")
P("")

# ---------------------------------------------------------------- cut axis
P("## A. Cut axis: achieved cut and HPWL\n")
P("Nets newly cut by the bisections (levels[].cut_nets - inessential), summed over all levels, relative to the control; and HPWL relative to the control.  The oracle assignment (oA) is on the same axis: its tree's cut against its HPWL.\n")
P("| arm | " + " | ".join(f"{SHORT[d]} cut / hpwl" for d in DESIGNS) + " | geomean cut | geomean hpwl |")
P("|---|" + "---:|" * (len(DESIGNS) + 2))
cut_axis = {}
for arm in CUT_ARMS + ["oA"]:
    cells = []
    cuts, hp = [], []
    for d in DESIGNS:
        c, c0 = total_cut(d, arm), total_cut(d, "ctl")
        h, h0 = final(d, arm), final(d, "ctl")
        rc = c / c0 if c and c0 else None
        rh = h / h0 if h and h0 else None
        cut_axis[(d, arm)] = (rc, rh)
        if rc:
            cuts.append(rc)
        if rh:
            hp.append(rh)
        cells.append(f"{fmt(rc)} / {fmt(rh)}")
    P(f"| {arm} | " + " | ".join(cells) + f" | {fmt(gmean(cuts))} | {fmt(gmean(hp))} |")
P("")
P("The same over the top six levels only (regions >= ~1/64 of the die; the transport assignment is feasible there without repairs):\n")
P("| arm | " + " | ".join(SHORT[d] for d in DESIGNS) + " | geomean |")
P("|---|" + "---:|" * (len(DESIGNS) + 1))
for arm in CUT_ARMS + ["oT", "oA"]:
    vals = []
    for d in DESIGNS:
        c, c0 = total_cut(d, arm, 5), total_cut(d, "ctl", 5)
        vals.append(c / c0 if c and c0 else None)
    P(f"| {arm} | " + " | ".join(fmt(v, 2) for v in vals) + f" | {fmt(gmean([v for v in vals if v]), 2)} |")
P("")
P("Requested versus achieved degradation of the engine's weighted cut (sum over bisections, all levels):\n")
P("| arm | requested | " + " | ".join(SHORT[d] for d in DESIGNS) + " |")
P("|---|---:|" + "---:|" * len(DESIGNS))
for arm, req in [("deg10", 1.10), ("deg25", 1.25), ("deg50", 1.50), ("deg100", 2.00)]:
    cells = []
    for d in DESIGNS:
        b, a = level_sum(d, arm, "degrade_cut_before"), level_sum(d, arm, "degrade_cut_after")
        cells.append(fmt(a / b) if a and b else "–")
    P(f"| {arm} | {req:.2f} | " + " | ".join(cells) + " |")
P("")

# ---------------------------------------------------------------- info axis
P("## B. Information axis\n")
P("| rung | arm | " + " | ".join(SHORT[d] for d in DESIGNS) + " | geomean |")
P("|---|---|" + "---:|" * (len(DESIGNS) + 1))
for rung, arm in [("1 standard terminals, TritonPart decides", "ctl"), ("2 oracle terminals, TritonPart decides", "oT"), ("3 oracle assignment decides", "oA")]:
    vals = [ratio(d, arm) for d in DESIGNS]
    P(f"| {rung} | {arm} | " + " | ".join(fmt(v) for v in vals) + f" | {fmt(gmean([v for v in vals if v]))} |")
P("")
P("Share of the control's gap to gpl closed by each step (geomean of the arms; (ctl - arm) / (ctl - 1)):\n")
for arm in ("oT", "oA"):
    vals = []
    for d in DESIGNS:
        c, a = ratio(d, "ctl"), ratio(d, arm)
        if c and a and c > 1.0:
            vals.append((c - a) / (c - 1.0))
    if vals:
        P(f"- {arm}: {statistics.mean(vals):+.0%} (mean over floorplans; per floorplan " + ", ".join(f"{v:+.0%}" for v in vals) + ")")
P("")

# ---------------------------------------------------------------- 2x2
P("## C. Objective x terminal information (HPWL / gpl)\n")
P("| objective | standard terminals | oracle terminals | oracle / standard |")
P("|---|---:|---:|---:|")
for name, std, orc in OBJ_ARMS:
    s = gmean([ratio(d, std) for d in DESIGNS if ratio(d, std)])
    o = gmean([ratio(d, orc) for d in DESIGNS if ratio(d, orc)])
    pr = gmean([final(d, orc) / final(d, std) for d in DESIGNS if final(d, orc) and final(d, std)])
    P(f"| {name} | {fmt(s)} | {fmt(o)} | {fmt(pr)} |")
P("")
P("Per floorplan (HPWL / gpl):\n")
P("| floorplan | cut-net std | cut-net oracle | HPWL std | HPWL oracle |")
P("|---|---:|---:|---:|---:|")
for d in DESIGNS:
    P(f"| {SHORT[d]} | {fmt(ratio(d, 'ctl'))} | {fmt(ratio(d, 'oT'))} | {fmt(ratio(d, 'hp'))} | {fmt(ratio(d, 'hp_oT'))} |")
P("")

# ---------------------------------------------------------------- am cut
P("## D. Cut of the gpl-derived assignment against the min-cut partition\n")
P("Same region, same hypergraph, same planned bounds (arm oT, which computes both at every bisection).  Weighted cut summed over the level's regions; ratio transport / min-cut.\n")
levels_present = sorted({lv["level"] for d in DESIGNS for doc in data.get(d, {}).get("oT", {}).values() for lv in doc.get("levels", []) if lv.get("am_regions")})
P("| floorplan | all levels | " + " | ".join(f"L{l}" for l in levels_present) + " |")
P("|---|---:|" + "---:|" * len(levels_present))
amcut = {}
for d in DESIGNS:
    docs = data.get(d, {}).get("oT", {})
    if not docs:
        continue
    per = collections.defaultdict(lambda: [0.0, 0.0])
    for doc in docs.values():
        for lv in doc.get("levels", []):
            if lv.get("am_regions"):
                per[lv["level"]][0] += lv.get("am_cut_transport", 0) or 0
                per[lv["level"]][1] += lv.get("am_cut_mincut", 0) or 0
    tot_t = sum(v[0] for v in per.values())
    tot_m = sum(v[1] for v in per.values())
    amcut[d] = {l: (per[l][0] / per[l][1] if per[l][1] else None) for l in per}
    cells = [fmt(amcut[d].get(l), 2) for l in levels_present]
    P(f"| {SHORT[d]} | {fmt(tot_t / tot_m if tot_m else None)} | " + " | ".join(cells) + " |")
P("")
P("Reading: > 1 means the assignment that produces the better placement (oA) cuts *more* nets than the partitioner's: cut and placement quality are misaligned.  < 1 would mean the partitioner is leaving cut on the table.\n")

# ---------------------------------------------------------------- runtime
P("## Runtime (t_place seconds, seed mean)\n")
P("| arm | " + " | ".join(SHORT[d] for d in DESIGNS) + " |")
P("|---|" + "---:|" * len(DESIGNS))
for arm in ALL:
    P(f"| {arm} | " + " | ".join(fmt(final(d, arm, 't_place'), 0) for d in DESIGNS) + " |")
P("")

with open(os.path.join(OUT, "tables.md"), "w") as f:
    f.write("\n".join(lines) + "\n")
print("\n".join(lines))

# ---------------------------------------------------------------- figures
try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError:
    print("matplotlib missing: tables only")
    sys.exit(0)

colors = plt.cm.tab10.colors

# cut axis
fig, ax = plt.subplots(figsize=(6.5, 4.5))
for i, d in enumerate(DESIGNS):
    xs, ys = [], []
    for arm in CUT_ARMS:
        rc, rh = cut_axis.get((d, arm), (None, None))
        h = ratio(d, arm)
        if rc and h:
            xs.append(rc)
            ys.append(h)
    if xs:
        order = sorted(range(len(xs)), key=lambda k: xs[k])
        ax.plot([xs[k] for k in order], [ys[k] for k in order], "o-", color=colors[i], alpha=0.6, label=SHORT[d])
gx, gy = [], []
for arm in CUT_ARMS:
    rc = gmean([cut_axis[(d, arm)][0] for d in DESIGNS if cut_axis.get((d, arm), (None,))[0]])
    rh = gmean([ratio(d, arm) for d in DESIGNS if ratio(d, arm)])
    if rc and rh and not math.isnan(rc) and not math.isnan(rh):
        gx.append(rc)
        gy.append(rh)
        ax.annotate(arm, (rc, rh), textcoords="offset points", xytext=(4, 6), fontsize=8)
if gx:
    order = sorted(range(len(gx)), key=lambda k: gx[k])
    ax.plot([gx[k] for k in order], [gy[k] for k in order], "s-", color="k", lw=2.2, label="geomean")
# The oracle assignment on the same axis: its partition's cut (oA commits
# the transport partition; levels[].cut_nets counts it) against its HPWL.
oa_x, oa_y = [], []
for i, d in enumerate(DESIGNS):
    c, c0, h = total_cut(d, "oA"), total_cut(d, "ctl"), ratio(d, "oA")
    if c and c0 and h:
        ax.plot(c / c0, h, "*", color=colors[i], ms=13, mec="k", mew=0.6)
        oa_x.append(c / c0)
        oa_y.append(h)
if oa_x:
    ax.plot([], [], "*", color="w", mec="k", ms=11, label="oracle assignment (oA)")
    ax.text(1.12, 1.045, "stars: the gpl-derived tree\n(%.2f-%.1fx the cut; swerv, mempool have macros)" % (min(oa_x), max(oa_x)),
            fontsize=7.5, va="center")
ax.axhline(1.0, color="gray", ls=":", lw=1)
ax.set_xscale("log")
from matplotlib.ticker import FixedLocator, FixedFormatter
ax.xaxis.set_major_locator(FixedLocator([0.9, 1, 1.25, 1.5, 2, 3, 4]))
ax.xaxis.set_major_formatter(FixedFormatter(["0.9", "1", "1.25", "1.5", "2", "3", "4"]))
ax.xaxis.set_minor_formatter(FixedFormatter([]))
ax.set_xlabel("nets cut by the bisections / control (log)")
ax.set_ylabel("HPWL / gpl")
ax.set_title("A. Cut axis: information fixed, cut quality varied")
ax.set_ylim(0.95, max(1.65, ax.get_ylim()[1]))
ax.legend(fontsize=7, ncol=2, loc="upper left")
fig.tight_layout()
fig.savefig(os.path.join(OUT, "i1_cut_axis.png"), dpi=150)
plt.close(fig)

# info axis
fig, ax = plt.subplots(figsize=(6.0, 4.5))
xs = range(len(INFO_ARMS))
for i, d in enumerate(DESIGNS):
    ys = [ratio(d, a) for a in INFO_ARMS]
    if all(ys):
        ax.plot(list(xs), ys, "o-", color=colors[i], alpha=0.6, label=SHORT[d])
g = [gmean([ratio(d, a) for d in DESIGNS if ratio(d, a)]) for a in INFO_ARMS]
ax.plot(list(xs), g, "s-", color="k", lw=2.2, label="geomean")
ax.axhline(1.0, color="gray", ls=":", lw=1)
ax.set_xticks(list(xs))
ax.set_xticklabels(["standard\nterminals", "oracle\nterminals", "oracle\nassignment"])
ax.set_ylabel("HPWL / gpl")
ax.set_title("B. Information axis: algorithm fixed, information varied")
ax.legend(fontsize=7, ncol=2)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "i1_info_axis.png"), dpi=150)
plt.close(fig)

# 2x2
fig, ax = plt.subplots(figsize=(6.0, 4.5))
width = 0.35
for j, (name, std, orc) in enumerate(OBJ_ARMS):
    s = gmean([ratio(d, std) for d in DESIGNS if ratio(d, std)])
    o = gmean([ratio(d, orc) for d in DESIGNS if ratio(d, orc)])
    ax.bar(j - width / 2, s, width, color="#999", label="standard terminals" if j == 0 else None)
    ax.bar(j + width / 2, o, width, color="#2b6ca3", label="oracle terminals" if j == 0 else None)
    for d, i in zip(DESIGNS, range(len(DESIGNS))):
        if ratio(d, std):
            ax.plot(j - width / 2, ratio(d, std), "o", color=colors[i], ms=4)
        if ratio(d, orc):
            ax.plot(j + width / 2, ratio(d, orc), "o", color=colors[i], ms=4)
ax.axhline(1.0, color="gray", ls=":", lw=1)
ax.set_xticks([0, 1])
ax.set_xticklabels([n for n, _, _ in OBJ_ARMS])
ax.set_ylabel("HPWL / gpl (geomean; dots = floorplans)")
ax.set_ylim(0.9, max(1.3, ax.get_ylim()[1]))
ax.set_title("C. Objective x terminal information")
ax.legend(fontsize=8)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "i1_objective.png"), dpi=150)
plt.close(fig)

# am cut per level
if amcut:
    fig, ax = plt.subplots(figsize=(6.5, 4.0))
    for i, d in enumerate(DESIGNS):
        if d in amcut:
            ls = sorted(l for l in amcut[d] if amcut[d][l])
            ax.plot(ls, [amcut[d][l] for l in ls], "o-", color=colors[i], alpha=0.7, label=SHORT[d])
    ax.axhline(1.0, color="gray", ls=":", lw=1)
    ax.set_xlabel("level")
    ax.set_ylabel("cut(gpl-derived assignment) / cut(min-cut)")
    ax.set_title("D. The better placement's partition cuts this much more")
    ax.legend(fontsize=7, ncol=2)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "i1_amcut.png"), dpi=150)
    plt.close(fig)

# achieved degradation per level
fig, ax = plt.subplots(figsize=(6.5, 4.0))
for j, arm in enumerate(["deg10", "deg25", "deg50", "deg100"]):
    per = collections.defaultdict(lambda: [0.0, 0.0])
    for d in DESIGNS:
        for doc in data.get(d, {}).get(arm, {}).values():
            for lv in doc.get("levels", []):
                if lv.get("degrade_regions"):
                    per[lv["level"]][0] += lv.get("degrade_cut_before", 0) or 0
                    per[lv["level"]][1] += lv.get("degrade_cut_after", 0) or 0
    ls = sorted(l for l in per if per[l][0])
    if ls:
        ax.plot(ls, [per[l][1] / per[l][0] for l in ls], "o-", label=arm)
ax.set_xlabel("level")
ax.set_ylabel("achieved cut / engine cut (all floorplans)")
ax.set_title("Achieved degradation per level")
ax.legend(fontsize=8)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "i1_levels.png"), dpi=150)
plt.close(fig)
print("figures in", OUT)
