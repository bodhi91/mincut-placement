#!/usr/bin/env python3
"""Summarizes a FastPart server trace (MCP_FASTPART_TRACE=<file>).

    tools/fastpart/trace.py trace.jsonl [--mcp results/<exp>/<run>.json]

The trace has one JSON line per region the placer sent to FastPart (level,
size, budget, wall, engine runs, winning template, early stop).  Prints a
per-level table and the totals: how many engine runs the portfolio made,
which templates won, how often the budget was used up against stopped by
the stall rule, and (with --mcp, the run's metrics JSON) how many regions
per level went to the TritonPart delegate instead.  A trace file written by
several placements in a row is split at every level-0 request.
"""

import collections
import json
import statistics
import sys


def summarize(lines, levels):
    by = collections.defaultdict(list)
    for line in lines:
        by[line["level"]].append(line)
    print("lvl  regions  fastpart  delegated  budget  wall mean/max  runs  early%  "
          "movable min/med/max  winners")
    lane_time = 0.0
    for level in sorted(by):
        rs = by[level]
        regions = levels.get(level, {}).get("regions")
        wall = [r["wall_s"] or 0 for r in rs]
        lane_time += sum(wall)
        runs = [r["runs"] or 0 for r in rs]
        early = sum(1 for r in rs if r.get("stopped_early")) / len(rs)
        mv = [r["movable"] or 0 for r in rs]
        win = collections.Counter((r.get("template") or "").split(".")[0]
                                  for r in rs).most_common(3)
        print("%3d  %7s  %8d  %9s  %6.2f  %5.2f / %5.2f  %4.1f  %5.0f  %6d/%5d/%5d  %s"
              % (level, regions if regions is not None else "?", len(rs),
                 regions - len(rs) if regions is not None else "?",
                 rs[0]["budget_s"] or 0, statistics.fmean(wall), max(wall),
                 statistics.fmean(runs), 100 * early, min(mv),
                 statistics.median(mv), max(mv),
                 ", ".join("%s %d" % w for w in win)))
    tried = collections.Counter()
    valid = collections.Counter()
    for r in lines:
        for name, s in (r.get("templates") or {}).items():
            tried[name] += s["tried"]
            valid[name] += s["valid"]
    print("regions %d, engine runs %d (valid %d), lane time %.1f s, "
          "feasible %d, early stop %d, mean pool %.1f, lane waits > 0: %d"
          % (len(lines), sum(tried.values()), sum(valid.values()), lane_time,
             sum(1 for r in lines if r.get("feasible")),
             sum(1 for r in lines if r.get("stopped_early")),
             statistics.fmean(r.get("pool_size") or 0 for r in lines),
             sum(1 for r in lines if (r.get("wait_s") or 0) > 0.01)))
    fam = collections.Counter(r.get("family") for r in lines)
    print("winning family:", ", ".join("%s %d" % f for f in fam.most_common()))
    win = collections.Counter((r.get("template") or "").split(".")[0] for r in lines)
    print("winning template:", ", ".join("%s %d" % w for w in win.most_common()))
    print("runs per template:", ", ".join("%s %d" % t for t in tried.most_common()))


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    with open(argv[1]) as f:
        lines = [json.loads(l) for l in f if l.strip()]
    levels = {}
    if "--mcp" in argv:
        with open(argv[argv.index("--mcp") + 1]) as f:
            levels = {l["level"]: l for l in json.load(f).get("levels", [])}
    starts = [i for i, l in enumerate(lines) if l.get("level") == 0] or [0]
    starts.append(len(lines))
    for a, b in zip(starts, starts[1:]):
        if len(starts) > 2:
            print("== placement %d (requests %d-%d)" % (starts.index(a) + 1, a, b - 1))
        summarize(lines[a:b], levels)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
