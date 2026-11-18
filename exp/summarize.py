#!/usr/bin/env python3
"""Tabulates results/<exp>/*.json (schema v1).

    exp/summarize.py results/e0_smoke [--csv out.csv]

Prints one row per (design, variant) with mean and spread over seeds for
HPWL, global-route wirelength, overflow, WNS, TNS, placement time.
"""

import csv
import glob
import json
import os
import statistics
import sys

COLS = ["hpwl", "hpwl_dpl", "grt_wl", "overflow", "wns", "tns", "t_place",
        "mem_peak_mb"]


def fmt(values):
    values = [v for v in values if v is not None]
    if not values:
        return "-"
    mean = statistics.fmean(values)
    if len(values) == 1:
        return "%.4g" % mean
    spread = max(values) - min(values)
    return "%.4g ±%.2g" % (mean, spread / 2)


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    folder = argv[1]
    csv_path = None
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

    header = ["design", "variant", "n"] + COLS
    rows = []
    for (design, variant), docs in sorted(groups.items()):
        row = [design, variant, str(len(docs))]
        for col in COLS:
            row.append(fmt([d["final"].get(col) for d in docs]))
        rows.append(row)

    widths = [max(len(r[i]) for r in [header] + rows) for i in range(len(header))]
    for r in [header] + rows:
        print("  ".join(c.ljust(w) for c, w in zip(r, widths)))

    if csv_path:
        with open(csv_path, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["design", "variant", "seed"] + COLS)
            for (design, variant), docs in sorted(groups.items()):
                for d in sorted(docs, key=lambda d: d["seed"]):
                    w.writerow([design, variant, d["seed"]]
                               + [d["final"].get(c) for c in COLS])
        print("wrote", csv_path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
