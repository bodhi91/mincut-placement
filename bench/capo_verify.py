#!/usr/bin/env python3
"""Bookshelf bridge check, part 2: compare the Bookshelf model of a Capo
placement (design.nodes + design.nets pin offsets + Capo's .pl) with what
OpenDB holds after the same .pl was imported (bench/capo_verify.tcl).

    MCP_DESIGN=<floorplan> MCP_PL=<run>.capo.pl MCP_VDIR=<dir> \\
      openroad -exit bench/capo_verify.tcl > <dir>/verify.log
    python3 bench/capo_verify.py <dir> <run>.capo.pl <run>.capo.log

Prints, for every signal pin of every placed instance, the distance
between the Bookshelf pin (node lower-left + size/2 + offset) and the
OpenDB pin (center of the instance terminal's bounding box, with the
instance's orientation applied by OpenDB), and three HPWLs: the one Capo
printed for its own placement (`RBPl Est Pin-to-Pin HPWL`, in DBU), the
one of the Bookshelf model, and the one of the OpenDB pin geometry.
Where an instance has two pins on the same net, each Bookshelf pin is
matched to the nearest OpenDB pin of that instance and net.
"""
import collections
import re
import sys

vdir, pl_file, log_file = sys.argv[1], sys.argv[2], sys.argv[3]
nodes = {}
for line in open(f"{vdir}/design.nodes"):
    t = line.split()
    if len(t) >= 3 and t[0] not in ("UCLA", "NumNodes", "NumTerminals"):
        nodes[t[0]] = (float(t[1]), float(t[2]), len(t) > 3)
pl = {}
for line in open(pl_file):
    t = line.split()
    if len(t) >= 4 and t[0] not in ("UCLA",) and not t[0].startswith("#"):
        i = t.index(":")
        pl[t[0]] = (float(t[1]), float(t[2]), t[i + 1], "/FIXED" in t)
# Bookshelf pins: node center + offset
nets = collections.defaultdict(list)
cur = None
for line in open(f"{vdir}/design.nets"):
    t = line.split()
    if not t or t[0] == "UCLA" or t[0] in ("NumNets", "NumPins"):
        continue
    if t[0] == "NetDegree":
        cur = t[3]
        continue
    name, ox, oy = t[0], float(t[3]), float(t[4])
    w, h, term = nodes[name]
    x, y, orient, fixed = pl[name]
    nets[cur].append((name, x + w / 2 + ox, y + h / 2 + oy, orient))
bs_hpwl = sum((max(p[1] for p in ps) - min(p[1] for p in ps)) + (max(p[2] for p in ps) - min(p[2] for p in ps))
              for ps in nets.values())
# OpenDB pins
odb = collections.defaultdict(list)
odb_by = collections.defaultdict(list)
for line in open(f"{vdir}/odb_pins.txt"):
    inst, pin, net, x, y, fixed = line.split()
    odb[net].append((inst, int(x), int(y)))
    odb_by[(inst, net)].append((int(x), int(y)))
odb_hpwl = sum((max(p[1] for p in ps) - min(p[1] for p in ps)) + (max(p[2] for p in ps) - min(p[2] for p in ps))
               for ps in odb.values())
# pin by pin: Bookshelf pin of (node, net) against the nearest OpenDB pin of the same instance and net
diffs = []
worst = (0, None)
orients = collections.Counter()
for net, ps in nets.items():
    for name, bx, by, orient in ps:
        orients[orient] += 1
        if (name, net) not in odb_by:
            continue   # I/O terminal: no instance pin
        d = min(abs(bx - x) + abs(by - y) for x, y in odb_by[(name, net)])
        diffs.append(d)
        if d > worst[0]:
            worst = (d, (name, net, orient))
capo = None
for line in open(log_file):
    m = re.search(r"RBPl Est Pin-to-Pin HPWL:\s*([0-9.e+]+)", line)
    if m:
        capo = float(m.group(1))
n = len(diffs)
print(f"  pins compared {n}; |Bookshelf pin - OpenDB pin| mean {sum(diffs) / n:.2f} dbu, max {worst[0]:.1f} dbu {worst[1]}; "
      f"orientations in the .pl {dict(orients)}")
print(f"  HPWL dbu: Capo log {capo:.6g}, Bookshelf model {bs_hpwl:.6g} ({(bs_hpwl / capo - 1) * 100:+.3f} %), "
      f"OpenDB {odb_hpwl:.6g} ({(odb_hpwl / capo - 1) * 100:+.3f} %)")
