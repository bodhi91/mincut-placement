#!/usr/bin/env python3
"""Arranges the placement solutions written by `exp/run.sh --keep-def`
(results/<exp>/<floorplan>__<variant>__s<seed>.def.gz) into the solution
archive of the review repository:

    solutions/<set>/<floorplan>/<placer>_s<seed>.def.gz
    solutions/manifest.csv

<set> is few_row, sweep or square (by floorplan name); <placer> is the
paper's name of the arm.  manifest.csv lists every solution with the HPWL
after detailed placement recorded by the run (results JSON, `final.hpwl_dpl`,
micrometres) so that a reviewer can check `bench/verify_solution.tcl`
against it.

    exp/collect_solutions.py <solutions dir> [--exp x3_mega,x4_ar_sweep,...]
"""
import argparse
import csv
import json
import os
import re
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# experiment -> {variant: placer name in the paper}
SOURCES = {
    "solutions_x3": {"gpl": "replace", "cutnet_tp": "rb_tp", "kway_tp": "rbk_tp"},
    "solutions_x4": {"gpl": "replace", "cutnet_tp": "rb_tp", "kway_tp": "rbk_tp"},
    "x3_mega": {"gpl": "replace", "cutnet_tp": "rb_tp", "kway_tp": "rbk_tp"},
    "x4_ar_sweep": {"gpl": "replace", "cutnet_tp": "rb_tp", "kway_tp": "rbk_tp"},
    "e17_krow": {"hm": "rb_hm", "mk": "rb_mk", "fp": "rb_fp", "capo": "capo"},
    "e23_base": {"capo": "capo"},
    "e14_capo": {"capo": "capo"},
}


def floorplan_set(name):
    if name.startswith("sweep_"):
        return "sweep"
    if re.search(r"_u[678]0_r[12468]_(nangate45|asap7)$", name):
        return "few_row"
    if re.search(r"_ar1_(nangate45|asap7)$", name):
        return "square"
    return None


def platform(name):
    return "asap7" if name.endswith("asap7") else "nangate45"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--exp", default=",".join(SOURCES))
    args = ap.parse_args()
    rows = []
    for exp in args.exp.split(","):
        variants = SOURCES.get(exp)
        rdir = os.path.join(ROOT, "results", exp)
        if not variants or not os.path.isdir(rdir):
            continue
        for f in sorted(os.listdir(rdir)):
            m = re.match(r"^(.+?)__(.+?)__s(\d+)\.def\.gz$", f)
            if not m:
                continue
            fp, variant, seed = m.groups()
            if variant not in variants:
                continue
            fset = floorplan_set(fp)
            if fset is None:
                continue
            jf = os.path.join(rdir, f"{fp}__{variant}__s{seed}.json")
            hpwl = ""
            if os.path.exists(jf):
                with open(jf) as fh:
                    final = json.load(fh).get("final", {})
                hpwl = final.get("hpwl_dpl", final.get("hpwl", ""))
            placer = variants[variant]
            dest_dir = os.path.join(args.out, fset, fp)
            os.makedirs(dest_dir, exist_ok=True)
            dest = os.path.join(dest_dir, f"{placer}_s{seed}.def.gz")
            shutil.copy2(os.path.join(rdir, f), dest)
            rows.append((fset, fp, platform(fp), placer, seed, hpwl,
                         os.path.relpath(dest, args.out), exp))
    rows.sort()
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, "manifest.csv"), "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["set", "floorplan", "platform", "placer", "seed",
                    "hpwl_dpl_um", "file", "source_experiment"])
        w.writerows(rows)
    print(f"{len(rows)} solutions -> {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
