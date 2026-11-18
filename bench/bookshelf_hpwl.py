#!/usr/bin/env python3
"""Pin-to-pin HPWL of a Bookshelf placement (what WLCalc reports).

  bench/bookshelf_hpwl.py <design>.aux [<placement>.pl]

Pins of movable nodes are at node lower-left + size/2 + offset; terminals
(pads) at their center.  Nets with fewer than two pins are skipped.  Used to
cross-check the HPWL that OpenROAD reports on the converted designs
(bench/bookshelf2lefdef.py) against the Bookshelf evaluators, and to score
placements written back with bench/write_pl.tcl.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bookshelf2lefdef import parse_aux, parse_nets, parse_nodes, parse_pl  # noqa: E402


def hpwl(aux, pl_path=None):
    files = parse_aux(aux)
    nodes, _ = parse_nodes(files['nodes'])
    nets = parse_nets(files['nets'])
    pl = parse_pl(pl_path or files['pl'])
    total = 0.0
    skipped = 0
    for name, pins in nets:
        xs = []
        ys = []
        for (node, ox, oy) in pins:
            if node not in pl:
                continue
            w, h, _ = nodes[node]
            x, y = pl[node]
            xs.append(x + w / 2.0 + ox)
            ys.append(y + h / 2.0 + oy)
        if len(xs) < 2:
            skipped += 1
            continue
        total += (max(xs) - min(xs)) + (max(ys) - min(ys))
    return total, skipped


if __name__ == '__main__':
    total, skipped = hpwl(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
    print('HPWL %.1f (%d nets with < 2 placed pins skipped)' % (total, skipped))
