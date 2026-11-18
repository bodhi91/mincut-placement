#!/usr/bin/env python3
"""Extract the macro placement from a Circuit Training protobuf netlist.

The TILOS MacroPlacement repository (Testcases/ICCAD04/ibmXX/ibmXX.pb.txt)
carries RePlAce's placement of every node of the modified ICCAD04 benchmarks,
in units of 1/100 Bookshelf unit (x, y = node center).  Only the macros are
needed: they become the fixed floorplan both placers work on.  Writes a
Bookshelf-style list "name llx lly width height" in Bookshelf units, lower-left
snapped to whole units, and reports overlaps.

  bench/pb_macros.py ibm01.pb.txt ibm01.nodes ibm01.macros.pl

A Bookshelf .pl (e.g. RePlAce-static's <design>.ibm.eplace.pl, lower-left
positions) is accepted in place of the protobuf: the repository's ibm17.pb.txt
is truncated (39 k of 185 k nodes), so ibm17's macros come from our own run of
the RePlAce binary in iccad04/.
"""
import re
import sys


def parse_pb_macros(path, scale=100.0):
    text = open(path).read()
    macros = {}
    for node in text.split('\nnode {')[1:]:
        m = re.search(r'key: "type"\s*value \{\s*placeholder: "([^"]+)"', node)
        if not m or m.group(1) != 'macro':
            continue
        name = re.search(r'name: "([^"]+)"', node).group(1)
        vals = {}
        for key in ('x', 'y', 'width', 'height'):
            mm = re.search(r'key: "%s"\s*value \{\s*f: ([-\d.e+]+)' % key, node)
            vals[key] = float(mm.group(1)) * scale
        macros[name] = vals
    return macros


def parse_nodes(path):
    sizes = {}
    row_h = None
    with open(path) as f:
        for line in f:
            t = line.split()
            if not t or line.startswith('#') or t[0].startswith(('UCLA', 'Num')):
                continue
            sizes[t[0]] = (float(t[1]), float(t[2]), len(t) > 3 and t[3].startswith('terminal'))
    return sizes


def parse_pl_macros(path, sizes, row_h):
    """Bookshelf .pl (lower-left) -> the pb-style dict (centers) of the multi-row nodes."""
    macros = {}
    with open(path) as f:
        for line in f:
            t = line.split()
            if not t or line.startswith('#') or t[0].startswith('UCLA'):
                continue
            if t[0] in sizes:
                w, h, term = sizes[t[0]]
                if not term and h > row_h:
                    macros[t[0]] = dict(x=float(t[1]) + w / 2.0, y=float(t[2]) + h / 2.0,
                                        width=w, height=h)
    return macros


def main():
    pb, nodes, out = sys.argv[1:4]
    sizes = parse_nodes(nodes)
    if pb.endswith('.pl'):
        row_h = min(h for (w, h, term) in sizes.values() if not term)
        macros = parse_pl_macros(pb, sizes, row_h)
    else:
        macros = parse_pb_macros(pb)
    heights = sorted({h for (w, h, term) in sizes.values() if not term})
    row_h = heights[0]
    tall = {n for n, (w, h, term) in sizes.items() if not term and h > row_h}
    missing = tall - set(macros)
    extra = set(macros) - tall
    if missing or extra:
        sys.exit('%s: %d multi-row nodes without pb position (%s), %d pb macros that are '
                 'single-row (%s)' % (pb, len(missing), sorted(missing)[:5], len(extra),
                                       sorted(extra)[:5]))
    rows = []
    for name in sorted(macros, key=lambda s: (len(s), s)):
        w, h, _ = sizes[name]
        v = macros[name]
        if abs(v['width'] - w) > 0.51 or abs(v['height'] - h) > 0.51:
            sys.exit('%s: size mismatch for %s: pb %gx%g, nodes %gx%g'
                     % (pb, name, v['width'], v['height'], w, h))
        llx = int(round(v['x'] - w / 2.0))
        lly = int(round(v['y'] - h / 2.0))
        rows.append((name, llx, lly, int(w), int(h)))
    # overlap check after snapping
    overlaps = 0
    for i in range(len(rows)):
        _, x1, y1, w1, h1 = rows[i]
        for j in range(i + 1, len(rows)):
            _, x2, y2, w2, h2 = rows[j]
            if x1 < x2 + w2 and x2 < x1 + w1 and y1 < y2 + h2 and y2 < y1 + h1:
                overlaps += 1
    with open(out, 'w') as f:
        f.write('# macro placement from %s (RePlAce, TILOS MacroPlacement ICCAD04)\n'
                '# name llx lly width height   (Bookshelf units, lower-left)\n' % pb.split('/')[-1])
        for r in rows:
            f.write('%s %d %d %d %d\n' % r)
    print('%s: %d macros, %d overlapping pairs after snapping -> %s'
          % (pb.split('/')[-1], len(rows), overlaps, out))


if __name__ == '__main__':
    main()
