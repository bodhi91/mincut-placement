#!/usr/bin/env python3
"""Placer comparison table from results/<exp>/*.json (schema v1).

    exp/compare.py results/e16_compare [--baseline gpl] [--order gpl,capo,...]
                   [--designs gcd_nangate45,...] [--md] [--csv out.csv]

One row per (design, variant) with the mean over seeds (± half the spread)
of the four reported metrics: HPWL after detailed placement (the placement
that is routed; with DPO=1 that includes improve_placement), placer
runtime, global-route wirelength and global-route runtime.  HPWL and routed
wirelength also as a ratio to the baseline variant on the same design.
"""

import csv
import glob
import json
import os
import statistics
import sys

COLS = [("hpwl", "HPWL um", True), ("t_place", "place s", False),
        ("grt_wl", "routed WL um", True), ("t_grt", "route s", False)]


def mean(values):
    values = [v for v in values if v is not None]
    return statistics.fmean(values) if values else None


def fmt(values, digits=4):
    values = [v for v in values if v is not None]
    if not values:
        return "-"
    m = statistics.fmean(values)
    text = "%.*g" % (digits, m) if m < 1e5 else "%d" % round(m, -2)
    if len(values) > 1 and max(values) != min(values):
        half = (max(values) - min(values)) / 2
        text += " ±%.2g" % half if half < 1e4 else " ±%d" % round(half, -2)
    return text


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    folder = argv[1]
    baseline = "gpl"
    order = None
    md = "--md" in argv
    csv_path = None
    if "--baseline" in argv:
        baseline = argv[argv.index("--baseline") + 1]
    if "--order" in argv:
        order = argv[argv.index("--order") + 1].split(",")
    if "--csv" in argv:
        csv_path = argv[argv.index("--csv") + 1]

    groups = {}
    for path in sorted(glob.glob(os.path.join(folder, "*.json"))):
        if path.endswith((".eval.json", ".metrics.json", ".mcp.json")):
            continue
        with open(path) as f:
            doc = json.load(f)
        if doc.get("schema") != 1:
            continue
        key = (doc["design"], doc["knobs"].get("variant", doc["variant"]))
        groups.setdefault(key, []).append(doc)

    designs = sorted({d for d, _ in groups})
    if "--designs" in argv:
        wanted = argv[argv.index("--designs") + 1].split(",")
        designs = [d for d in wanted if d in designs] + \
            [d for d in designs if d not in wanted]
    variants = sorted({v for _, v in groups})
    if order:
        variants = [v for v in order if v in variants] + \
            [v for v in variants if v not in order]

    header = ["design", "variant", "n"]
    for col, label, ratio in COLS:
        header.append(label)
        if ratio:
            header.append("x %s" % baseline)
    rows = []
    for design in designs:
        base = {}
        for col, _, ratio in COLS:
            if ratio and (design, baseline) in groups:
                base[col] = mean([d["final"].get(col)
                                  for d in groups[(design, baseline)]])
        for variant in variants:
            docs = groups.get((design, variant))
            if not docs:
                continue
            row = [design, variant, str(len(docs))]
            for col, _, ratio in COLS:
                values = [d["final"].get(col) for d in docs]
                row.append(fmt(values))
                if ratio:
                    m = mean(values)
                    b = base.get(col)
                    row.append("%.3f" % (m / b) if m and b else "-")
            rows.append(row)

    if md:
        print("| " + " | ".join(header) + " |")
        print("|" + "---|" * len(header))
        for r in rows:
            print("| " + " | ".join(r) + " |")
    else:
        widths = [max(len(r[i]) for r in [header] + rows)
                  for i in range(len(header))]
        for r in [header] + rows:
            print("  ".join(c.ljust(w) for c, w in zip(r, widths)))

    if csv_path:
        with open(csv_path, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["design", "variant", "seed"] + [c for c, _, _ in COLS])
            for (design, variant), docs in sorted(groups.items()):
                for d in sorted(docs, key=lambda d: d["seed"]):
                    w.writerow([design, variant, d["seed"]]
                               + [d["final"].get(c) for c, _, _ in COLS])
        print("wrote", csv_path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
