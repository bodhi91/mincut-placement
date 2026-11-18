#!/usr/bin/env python3
"""Table 9: seed sensitivity -- the three-seed runs of the aspect-ratio x
utilization sweep (X4), the one experiment of the paper run with more than
one seed per placer.

    python3 exp/seed_noise.py            # -> results/tables/9_seed_noise.md

Input: x4_ar_sweep/runs.csv (182 floorplans x {RePlAce, RB (TP), RB+k (TP)}
x seeds 1 2 3; HPWL after detailed placement, no routing).  The paper's
tables use seed 1 everywhere; this table says how much a different seed
would have moved them.

Noise per floorplan and placer: coefficient of variation (population std /
mean) and max-min spread relative to the mean of the three seeds.  Noise of
the aggregates: the geometric-mean ratio to RePlAce computed per seed, and
the floorplans whose win/loss against RePlAce depends on the seed.
"""
import csv
import math
import os
import re
import statistics as st
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# the exported CSVs: paper_results/ in the working copy, results/ in the release
PR = next(p for p in (os.path.join(ROOT, "paper_results"), os.path.join(ROOT, "results"))
          if os.path.isdir(os.path.join(p, "x3_mega")))
if not os.path.isdir(PR):
    PR = os.path.join(ROOT, "results")
OUT = os.path.join(PR, "tables", "9_seed_noise.md")
SRC = os.path.join(PR, "x4_ar_sweep", "runs.csv")
FP = re.compile(r"^sweep_(?P<design>.+?)_ar(?P<ar>[0-9p]+)_util(?P<util>\d+)_(?P<pdk>nangate45|asap7)$")
ARMS = [("RePlAce", "gpl"), ("RB (TP)", "cutnet_tp"), ("RB+k (TP)", "kway_tp")]
SEEDS = ["1", "2", "3"]


def gm(xs):
    xs = [x for x in xs if x and x > 0]
    return math.exp(sum(map(math.log, xs)) / len(xs)) if xs else float("nan")


def pct(x, d=1):
    return "–" if x is None or x != x else f"{100 * x:.{d}f} %"


def quant(xs, q):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(q * len(xs)))]


def table(hdr, rows):
    out = ["| " + " | ".join(hdr) + " |", "|" + "|".join("---" for _ in hdr) + "|"]
    out += ["| " + " | ".join(str(c) for c in r) + " |" for r in rows]
    return "\n".join(out) + "\n"


def main():
    H = defaultdict(dict)                   # (floorplan, variant) -> seed -> HPWL
    with open(SRC) as fh:
        for r in csv.DictReader(fh):
            try:
                H[(r["design"], r["variant"])][r["seed"]] = float(r["hpwl_dpl"])
            except ValueError:
                pass
    fps = sorted({d for d, _ in H})
    meta = {d: FP.match(d).groupdict() for d in fps}
    ar_label = lambda a: a.replace("p", ".")

    # ---- per-floorplan noise
    noise = {}                              # arm -> list of (fp, cv, range)
    for arm, v in ARMS:
        rows = []
        for d in fps:
            s = H.get((d, v), {})
            if len(s) < 3:
                continue
            xs = list(s.values())
            m = st.mean(xs)
            rows.append((d, st.pstdev(xs) / m, (max(xs) - min(xs)) / m))
        noise[arm] = rows

    L = ["# 9. Seed sensitivity (X4 sweep, 3 seeds)\n",
         "The paper reports seed 1 everywhere. This table measures, on the one experiment run with three seeds per "
         "placer, how much the seed moves the results: the 182 floorplans of the aspect-ratio x utilization sweep "
         "(§5; macro-free designs, AR 0.03 / 0.06 / 0.09 / 1 x utilization 60 / 70 / 80 % x NanGate45 / ASAP7), "
         "RePlAce, RB (TP) and RB+k (TP), HPWL after detailed placement (no routing), seeds 1, 2, 3. Source: "
         "`x4_ar_sweep/runs.csv`; regenerate with `python3 exp/seed_noise.py`.\n",
         "CV = population standard deviation / mean over the three seeds of one floorplan; spread = (max − min) / mean.\n"]

    # 9.1 distribution of noise per placer
    L.append("## 9.1 Per-floorplan seed noise by placer\n")
    rows = []
    for arm, _ in ARMS:
        n = noise[arm]
        cv = [c for _, c, _ in n]
        rg = [r for _, _, r in n]
        rows.append([arm, len(n), pct(st.median(cv), 2), pct(st.mean(cv), 2), pct(max(cv), 2),
                     pct(st.median(rg), 2), pct(quant(rg, 0.9), 2), pct(max(rg), 2),
                     sum(r > 0.05 for r in rg)])
    L.append(table(["placer", "floorplans (3 seeds)", "CV median", "CV mean", "CV max",
                    "spread median", "spread 90th pct", "spread max", "floorplans with spread > 5 %"], rows))
    L.append("RePlAce is deterministic in this flow: the three seeds give the same placement on every floorplan.\n")

    # 9.2 by design and by aspect ratio (RB+k)
    for i, (key, title) in enumerate([("design", "design"), ("ar", "aspect ratio"), ("util", "utilization"), ("pdk", "PDK")]):
        L.append(f"## 9.2.{i + 1} RB (TP) and RB+k (TP) seed spread by {title}\n")
        groups = defaultdict(lambda: defaultdict(list))
        for arm, _ in ARMS[1:]:
            for d, cv, rg in noise[arm]:
                groups[meta[d][key]][arm].append((cv, rg))
        rows = []
        for g in sorted(groups, key=lambda x: (float(x.replace("p", ".")) if key in ("ar", "util") else x)):
            r = [ar_label(g) if key == "ar" else g, len(groups[g][ARMS[1][0]])]
            for arm, _ in ARMS[1:]:
                xs = groups[g][arm]
                r += [pct(st.median([c for c, _ in xs]), 2), pct(st.median([s for _, s in xs]), 2), pct(max([s for _, s in xs]), 1)]
            rows.append(r)
        L.append(table([title, "n", "RB (TP) CV median", "RB (TP) spread median", "RB (TP) spread max",
                        "RB+k CV median", "RB+k spread median", "RB+k spread max"], rows))

    # 9.3 aggregate stability
    L.append("## 9.3 Stability of the comparison with RePlAce\n")
    rows = []
    for arm, v in ARMS[1:]:
        per_seed = []
        for s in SEEDS:
            rs = [H[(d, v)][s] / H[(d, "gpl")][s] for d in fps
                  if s in H.get((d, v), {}) and s in H.get((d, "gpl"), {})]
            per_seed.append((gm(rs), len(rs), sum(r < 1 for r in rs)))
        flips = tot = 0
        for d in fps:
            rs = [H[(d, v)][s] / H[(d, "gpl")][s] for s in SEEDS
                  if s in H.get((d, v), {}) and s in H.get((d, "gpl"), {})]
            if len(rs) == 3:
                tot += 1
                flips += min(rs) < 1 < max(rs)
        gms = [g for g, _, _ in per_seed]
        rows.append([arm] + [f"{g:.4f} ({w}/{n})" for g, n, w in per_seed]
                    + [pct((max(gms) - min(gms)) / min(gms), 2), f"{flips} / {tot}"])
    L.append(table(["placer", "geomean / RePlAce, seed 1 (wins/n)", "seed 2", "seed 3",
                    "geomean range", "floorplans whose win/loss vs RePlAce depends on the seed"], rows))
    L.append("The geometric-mean ratio moves by a few tenths of a percent between seeds; the win/loss sign changes "
             "only on floorplans whose ratio is close to 1.\n")

    # 9.4 outliers
    L.append("## 9.4 Floorplans with the largest RB+k seed spread\n")
    big = sorted(noise["RB+k (TP)"], key=lambda t: -t[2])[:12]
    rows = []
    for d, cv, rg in big:
        s = H[(d, "kway_tp")]
        g = H[(d, "gpl")]
        rows.append([d, meta[d]["design"], ar_label(meta[d]["ar"]), meta[d]["util"], meta[d]["pdk"], pct(cv, 2), pct(rg, 1),
                     " / ".join(f"{s[k] / g[k]:.3f}" for k in SEEDS if k in s and k in g)])
    L.append(table(["floorplan", "design", "AR", "util %", "PDK", "CV", "spread", "RB+k / RePlAce by seed"], rows))
    over = [(d, rg) for d, _, rg in noise["RB+k (TP)"] if rg > 0.05]
    n_sw = sum(meta[d]["design"] == "swerv" for d, _ in over)
    n_dense = sum(meta[d]["util"] in ("70", "80") for d, _ in over)
    n_narrow = sum(meta[d]["ar"] != "1" for d, _ in over)
    L.append(f"Of the {len(over)} floorplans with an RB+k spread above 5 %, {n_sw} are swerv, {n_dense} are at 70–80 % "
             f"utilization and {n_narrow} have AR ≤ 0.09. On these the three seeds also disagree on the comparison "
             "with RePlAce (last column), so single-seed results on narrow dense floorplans of swerv-size designs "
             "carry a few percent of seed noise; the aggregates of 9.3 do not.\n")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as fh:
        fh.write("\n".join(L))
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
