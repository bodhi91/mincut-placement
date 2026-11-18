#!/usr/bin/env python3
"""Merges the artifacts of one harness run into a metrics JSON (schema v1).

    collect.py <prefix> <out.json> [--knobs KEY=VALUE ...]

Inputs (all optional except <prefix>.eval.json):
    <prefix>.eval.json      bench/common.tcl mcp_eval
    <prefix>.metrics.json   openroad -metrics (global_route__wirelength, dpl)
    <prefix>.log            openroad -log (final congestion report)
    <prefix>.mcp.json       mincut_placement -report (levels)
    <prefix>.time           /usr/bin/time -f "%e %M" (wall s, peak RSS kB)

Output:
    {schema, design, platform, variant, seed, git_sha, knobs{},
     levels[], final{hpwl, hpwl_global, grt_wl, routing, overflow, max_overflow_h,
     max_overflow_v, wns, tns, t_place, t_dpl, t_grt, t_eval, t_total,
     mem_peak_mb, dpl_moves, dpl_displacement_mean}}
"""

import json
import os
import re
import sys


def load_json(path):
    if not os.path.exists(path):
        return None
    with open(path) as f:
        return json.load(f)


def parse_congestion(log_path):
    """Returns (max_h, max_v, total) of the last 'Final congestion report'."""
    if not os.path.exists(log_path):
        return None
    result = None
    pattern = re.compile(
        r"^Total\s+\d+\s+\d+\s+[\d.]+%\s+(\d+)\s*/\s*(\d+)\s*/\s*(\d+)\s*$")
    with open(log_path, errors="replace") as f:
        for line in f:
            m = pattern.match(line.strip())
            if m:
                result = tuple(int(g) for g in m.groups())
    return result


def parse_time(path):
    if not os.path.exists(path):
        return None
    with open(path) as f:
        fields = f.read().split()
    if len(fields) < 2:
        return None
    try:
        return float(fields[0]), int(fields[1])
    except ValueError:
        return None


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    prefix, out_path = argv[1], argv[2]
    knobs = {}
    if len(argv) > 3 and argv[3] == "--knobs":
        for item in argv[4:]:
            key, _, value = item.partition("=")
            knobs[key] = value

    ev = load_json(prefix + ".eval.json")
    if ev is None:
        sys.stderr.write("missing %s.eval.json\n" % prefix)
        return 1
    metrics = load_json(prefix + ".metrics.json") or {}
    report = load_json(prefix + ".mcp.json")
    cong = parse_congestion(prefix + ".log")
    tm = parse_time(prefix + ".time")

    design = ev.get("design", "")
    platform = design.rsplit("_", 1)[1] if "_" in design else ""

    final = {
        "hpwl": ev.get("hpwl_um"),
        "hpwl_global": ev.get("hpwl_global_um"),
        # without DPO (MCP_DPO=0) hpwl_um is already the post-dpl HPWL
        "hpwl_dpl": ev.get("hpwl_dpl_um", ev.get("hpwl_um") if "t_dpo_s" not in ev else None),
        "t_dpo": ev.get("t_dpo_s"),
        "grt_wl": metrics.get("global_route__wirelength"),
        "overflow": cong[2] if cong else None,
        "max_overflow_h": cong[0] if cong else None,
        "max_overflow_v": cong[1] if cong else None,
        "wns": ev.get("wns_ns"),
        "tns": ev.get("tns_ns"),
        "t_place": ev.get("t_place_s"),
        "t_dpl": ev.get("t_dpl_s"),
        "t_grt": ev.get("t_grt_s"),
        "t_eval": ev.get("t_eval_s"),
        "t_total": tm[0] if tm else None,
        "mem_peak_mb": round(tm[1] / 1024.0, 1) if tm else None,
        "dpl_moves": metrics.get("dpl__total__moves"),
        "dpl_displacement_mean": metrics.get(
            "dpl__instance__displacement__mean"),
        "violations": metrics.get("design__violations"),
        # "ok", "failed: <GRT error>" (caught by mcp_eval) or "not run"
        # (the process died in global_route; the partial eval survived).
        "routing": ev.get("routing", "ok"),
    }

    doc = {
        "schema": 1,
        "design": design,
        "platform": platform,
        "variant": ev.get("variant"),
        "seed": ev.get("seed"),
        "threads": ev.get("threads"),
        "git_sha": ev.get("git_sha"),
        "knobs": knobs,
        "final": final,
    }
    if report is not None:
        doc["mcp_options"] = report.get("options")
        doc["netlist"] = report.get("netlist")
        doc["levels"] = report.get("levels")
        doc["mcp_result"] = report.get("result")

    with open(out_path, "w") as f:
        json.dump(doc, f, indent=1)
        f.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
