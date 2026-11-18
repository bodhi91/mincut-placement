#!/usr/bin/env python3
"""Wide CSV of the k-row grid (E17): one row per floorplan, the baseline and
every min-cut placer side by side.

    exp/krow_export.py results/e17_krow out.csv [--order gpl,capo,tp,hm,mk,fp]

Columns: platform, design, util, rows, cells, then per placer
    <p>_hpwl_um        HPWL after detailed placement (before DPO), um
    <p>_vs_gpl         ratio to the gpl HPWL
    <p>_t_place_s      placer seconds
    <p>_grt_wl_um      routed wirelength (global_route), um; empty if not routed
    <p>_status         ok | routing crashed | failed: <reason> | not run
and finally best_placer (lowest HPWL among the min-cut placers) and
best_vs_gpl.  Cell counts come from the author's summary_stats.csv when
present next to the floorplans.
"""

import csv
import glob
import json
import os
import re
import sys

NAME = re.compile(r"^(?P<design>.+)_u(?P<util>\d+)_r(?P<rows>\d+)_(?P<platform>[^_]+)$")
DESIGN_ORDER = ["gcd", "aes", "ibex", "jpeg", "swerv_no_macro", "cva6_no_macro"]
PLACER_NAME = {"gpl": "gpl", "capo": "Capo", "tp": "mcp-TritonPart", "hm": "mcp-hMETIS",
               "mk": "mcp-Mt-KaHyPar", "fp": "mcp-FastPart"}


def failure_reason(prefix):
    """First ERROR / crash line of a failed run's stdout."""
    try:
        with open(prefix + ".stdout", errors="replace") as fh:
            text = fh.read()
    except OSError:
        return "not run"
    for pat in (r"\[ERROR ([A-Z]+-\d+)\]", r"(Segmentation fault)", r"(out of memory)",
                r"(Aborted)", r"(Killed)"):
        m = re.search(pat, text)
        if m:
            return "failed: " + m.group(1)
    # in flight: /usr/bin/time fills .time at exit
    t = prefix + ".time"
    if os.path.exists(t) and os.path.getsize(t) == 0:
        return "running"
    return "failed"


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    folder, out = argv[1], argv[2]
    order = (argv[argv.index("--order") + 1].split(",") if "--order" in argv
             else ["gpl", "capo", "tp", "hm", "mk", "fp"])

    runs = {}
    for path in glob.glob(os.path.join(folder, "*.json")):
        if path.endswith((".eval.json", ".metrics.json", ".mcp.json")):
            continue
        with open(path) as fh:
            doc = json.load(fh)
        if doc.get("schema") != 1:
            continue
        m = NAME.match(doc["design"])
        if not m:
            continue
        key = (m["platform"], m["design"], int(m["util"]), int(m["rows"]))
        runs.setdefault(key, {})[doc["knobs"].get("variant", doc["variant"])] = doc

    cells = {}
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    stats = os.path.join(root, "k-row-floorplans", "replace", "summary_stats.csv")
    if os.path.exists(stats):
        with open(stats, newline="") as fh:
            for r in csv.DictReader(fh):
                cells[r["design"]] = r["instance_count"]

    # every floorplan that has a checkpoint, so that unstarted ones appear too
    floorplans = set(runs)
    for d in glob.glob(os.path.join(root, "bench", "designs", "*_u*_r*_*")):
        m = NAME.match(os.path.basename(d))
        if m:
            floorplans.add((m["platform"], m["design"], int(m["util"]), int(m["rows"])))

    def sort_key(fp):
        return (fp[0], DESIGN_ORDER.index(fp[1]) if fp[1] in DESIGN_ORDER else 99, fp[2], fp[3])

    header = ["platform", "design", "util_pct", "rows", "cells"]
    for p in order:
        header += ["%s_hpwl_um" % p, "%s_vs_gpl" % p, "%s_t_place_s" % p,
                   "%s_grt_wl_um" % p, "%s_status" % p]
    header += ["best_placer", "best_vs_gpl"]

    rows_out = []
    for fp in sorted(floorplans, key=sort_key):
        per = runs.get(fp, {})
        row = [fp[0], fp[1], fp[2], fp[3], cells.get(fp[1], "")]
        base = per.get("gpl", {}).get("final", {}).get("hpwl_dpl")
        best = None
        for p in order:
            doc = per.get(p)
            prefix = os.path.join(folder, "%s_u%d_r%d_%s__%s__s1" % (fp[1], fp[2], fp[3], fp[0], p))
            if doc is None:
                row += ["", "", "", "", failure_reason(prefix)]
                continue
            fin = doc["final"]
            hp = fin.get("hpwl_dpl") or fin.get("hpwl")
            ratio = round(hp / base, 4) if (hp and base) else ""
            routing = fin.get("routing", "ok")
            status = "ok" if routing == "ok" else "routing crashed"
            row += [round(hp, 1) if hp else "", ratio, fin.get("t_place"),
                    fin.get("grt_wl") or "", status]
            if p != "gpl" and hp and (best is None or hp < best[1]):
                best = (p, hp)
        if best:
            row += [PLACER_NAME[best[0]], round(best[1] / base, 4) if base else ""]
        else:
            row += ["", ""]
        rows_out.append(row)

    with open(out, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(header)
        w.writerows(rows_out)
    done = sum(1 for r in rows_out if any(r[9 + 5 * i] == "ok" or r[9 + 5 * i] == "routing crashed"
                                        for i in range(len(order))))
    print("wrote %s: %d floorplans, %d with at least one result" % (out, len(rows_out), done))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
