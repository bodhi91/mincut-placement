#!/usr/bin/env python3
"""Faithful Bookshelf -> LEF/DEF conversion for the ICCAD04 (IBM-MSwPins)
benchmarks, so that OpenROAD (gpl, mcp, dpl) can place them and report the
same HPWL that the Bookshelf evaluators (WLCalc) report.

  bench/bookshelf2lefdef.py <design>.aux <out_dir> [--macros <design>.macros.pl]
                            [--density D] [--name ibm01_iccad04]

Mapping (1 Bookshelf unit = 1 um, DEF/LEF units 1000 per um):
  .scl rows      -> SITE core (Sitewidth x row height), one DEF ROW per
                    (sub)row; DIEAREA = bounding box of the rows
  movable node   -> instance of a master shared by every node with the same
                    width, height and multiset of pin offsets (CLASS CORE
                    for row-height nodes, CLASS BLOCK for taller ones); pins
                    p0..p(n-1) are 0.1 x 0.1 squares at the Bookshelf offsets
                    from the node center, so pin-to-pin HPWL is preserved
  terminal 1x1   -> DEF PIN (BTerm) fixed at the node center (no site blocked)
  terminal larger-> fixed BLOCK instance at its .pl position
  --macros file  -> the multi-row nodes are FIXED at these lower-left
                    positions (bench/pb_macros.py: RePlAce's floorplan);
                    without it they stay unplaced (placers cannot move them)
  nets           -> one DEF NET per Bookshelf net (degree >= 2 after dropping
                    duplicates); .wts weights are all 1 in ICCAD04 and ignored

Writes <name>.lef (tech + masters), <name>.def, vars.tcl for bench/common.tcl
(platform iccad04, no liberty / routing), stats.json.  The density written to
vars.tcl is 0.90, or the standard-cell utilization of the free area (core
minus fixed macros) plus 0.10 if that is higher (capped at 1.0), unless
--density is given.  0.90 is where OpenROAD's gpl reproduces the published
RePlAce numbers (ibm02: 4.84M at 0.90 vs 4.76M published; 6.61M at 0.65,
5.07M at 0.80; at 1.00 dpl cannot legalize gpl's result).
"""
import argparse
import json
import math
import os
import sys
from collections import defaultdict

DBU = 1000


def parse_aux(aux):
    d = os.path.dirname(os.path.abspath(aux))
    files = {}
    for tok in open(aux).read().split(':', 1)[1].split():
        files[tok.rsplit('.', 1)[1]] = os.path.join(d, tok)
    return files


def parse_nodes(path):
    nodes = {}   # name -> (w, h, terminal)
    order = []
    with open(path) as f:
        for line in f:
            t = line.split()
            if not t or line.startswith('#') or t[0].startswith(('UCLA', 'Num')):
                continue
            nodes[t[0]] = (float(t[1]), float(t[2]), len(t) > 3 and t[3].startswith('terminal'))
            order.append(t[0])
    return nodes, order


def parse_nets(path):
    nets = []    # (name, [(node, ox, oy), ...])
    with open(path) as f:
        cur = None
        for line in f:
            t = line.split()
            if not t or line.startswith('#') or t[0].startswith(('UCLA', 'Num')):
                continue
            if t[0] == 'NetDegree':
                cur = (t[3] if len(t) > 3 else 'net%d' % len(nets), [])
                nets.append(cur)
                continue
            ox = oy = 0.0
            if len(t) >= 5 and t[2] == ':':
                ox, oy = float(t[3]), float(t[4])
            cur[1].append((t[0], ox, oy))
    return nets


def parse_pl(path):
    pl = {}
    with open(path) as f:
        for line in f:
            t = line.split()
            if not t or line.startswith('#') or t[0].startswith('UCLA'):
                continue
            pl[t[0]] = (float(t[1]), float(t[2]))
    return pl


def parse_scl(path):
    rows = []   # dicts: y, h, site_w, spacing, x, n
    with open(path) as f:
        cur = None
        for line in f:
            t = line.split()
            if not t:
                continue
            if t[0] == 'CoreRow':
                cur = {}
            elif t[0] == 'Coordinate':
                cur['y'] = float(t[2])
            elif t[0] == 'Height':
                cur['h'] = float(t[2])
            elif t[0] == 'Sitewidth':
                cur['site_w'] = float(t[2])
            elif t[0] == 'Sitespacing':
                cur['spacing'] = float(t[2])
            elif t[0] == 'SubrowOrigin':
                cur['x'] = float(t[2])
                cur['n'] = int(t[5])
                rows.append(dict(cur))
            elif t[0] == 'End':
                cur = None
    return rows


def parse_macros(path):
    pos = {}
    if not path:
        return pos
    with open(path) as f:
        for line in f:
            t = line.split()
            if not t or line.startswith('#'):
                continue
            pos[t[0]] = (int(t[1]), int(t[2]))
    return pos


def dbu(v):
    return int(round(v * DBU))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('aux')
    ap.add_argument('out_dir')
    ap.add_argument('--macros', help='name llx lly ... file of fixed macro positions')
    ap.add_argument('--density', type=float, help='global_place_density for vars.tcl')
    ap.add_argument('--name', help='design name (default: <aux basename>_iccad04)')
    args = ap.parse_args()

    files = parse_aux(args.aux)
    base = os.path.basename(args.aux).rsplit('.', 1)[0]
    name = args.name or base + '_iccad04'
    nodes, order = parse_nodes(files['nodes'])
    nets = parse_nets(files['nets'])
    pl = parse_pl(files['pl'])
    rows = parse_scl(files['scl'])
    macro_pos = parse_macros(args.macros)

    row_h = min(r['h'] for r in rows)
    site_w = rows[0]['site_w']
    assert all(r['site_w'] == site_w and r['spacing'] == site_w for r in rows), 'site width'
    die_x0 = min(r['x'] for r in rows)
    die_x1 = max(r['x'] + r['n'] * site_w for r in rows)
    die_y0 = min(r['y'] for r in rows)
    die_y1 = max(r['y'] + r['h'] for r in rows)
    core_area = sum(r['n'] * site_w * r['h'] for r in rows)

    # pins per node, in the order of appearance; sorted by offset to match the master
    node_pins = defaultdict(list)       # node -> [(ox, oy, net_idx)]
    for ni, (nname, pins) in enumerate(nets):
        for (node, ox, oy) in pins:
            if node not in nodes:
                sys.exit('net %s: unknown node %s' % (nname, node))
            node_pins[node].append((ox, oy, ni))

    # classify nodes
    kind = {}   # node -> 'core' | 'block' | 'bterm' | 'fixedblock'
    for n, (w, h, term) in nodes.items():
        if term:
            kind[n] = 'bterm' if (w <= 1 and h <= 1) else 'fixedblock'
        elif h > row_h + 1e-9:
            kind[n] = 'block'
        else:
            kind[n] = 'core'

    # masters by signature
    masters = {}    # signature -> (master_name, w, h, cls, offsets)
    inst_master = {}
    inst_pin_index = {}   # node -> {net_idx: pin index} (list for duplicates)
    for n in order:
        if kind[n] == 'bterm':
            continue
        w, h, _ = nodes[n]
        pins = sorted(node_pins.get(n, []))
        offsets = tuple((ox, oy) for (ox, oy, _) in pins)
        cls = 'CORE' if kind[n] == 'core' else 'BLOCK'
        sig = (w, h, cls, offsets)
        if sig not in masters:
            masters[sig] = ('%s%d' % ('c' if cls == 'CORE' else 'm', len(masters)), w, h, cls, offsets)
        inst_master[n] = masters[sig][0]
        inst_pin_index[n] = [(ni, i) for i, (ox, oy, ni) in enumerate(pins)]

    os.makedirs(args.out_dir, exist_ok=True)

    # ---- LEF
    with open(os.path.join(args.out_dir, name + '.lef'), 'w') as f:
        f.write('VERSION 5.8 ;\nBUSBITCHARS "[]" ;\nDIVIDERCHAR "/" ;\n'
                'UNITS\n  DATABASE MICRONS %d ;\nEND UNITS\n\n' % DBU)
        f.write('MANUFACTURINGGRID 0.001 ;\n\n')
        f.write('SITE core\n  CLASS CORE ;\n  SYMMETRY Y ;\n  SIZE %g BY %g ;\nEND core\n\n'
                % (site_w, row_h))
        # two routing layers so that pins and BTerms have a layer; unused otherwise
        for i, (lname, direction) in enumerate((('metal1', 'HORIZONTAL'), ('metal2', 'VERTICAL'))):
            f.write('LAYER %s\n  TYPE ROUTING ;\n  DIRECTION %s ;\n  PITCH 1.0 ;\n'
                    '  WIDTH 0.5 ;\n  SPACING 0.5 ;\nEND %s\n\n' % (lname, direction, lname))
            if i == 0:
                f.write('LAYER via1\n  TYPE CUT ;\n  SPACING 0.5 ;\nEND via1\n\n')
        f.write('VIA via1_default DEFAULT\n  LAYER metal1 ;\n    RECT -0.25 -0.25 0.25 0.25 ;\n'
                '  LAYER via1 ;\n    RECT -0.25 -0.25 0.25 0.25 ;\n'
                '  LAYER metal2 ;\n    RECT -0.25 -0.25 0.25 0.25 ;\nEND via1_default\n\n')
        # A one-site filler: without a one-site-wide CORE master, dpl forbids
        # one-site gaps between cells (a rule for technologies without a
        # 1-site fill cell) and check_placement fails on legal placements.
        f.write('MACRO FILL1\n  CLASS CORE SPACER ;\n  ORIGIN 0 0 ;\n  FOREIGN FILL1 0 0 ;\n'
                '  SIZE %g BY %g ;\n  SYMMETRY X Y ;\n  SITE core ;\nEND FILL1\n\n' % (site_w, row_h))
        for sig, (mname, w, h, cls, offsets) in masters.items():
            f.write('MACRO %s\n  CLASS %s ;\n  ORIGIN 0 0 ;\n  FOREIGN %s 0 0 ;\n  SIZE %g BY %g ;\n'
                    % (mname, cls, mname, w, h))
            if cls == 'CORE':
                f.write('  SYMMETRY X Y ;\n  SITE core ;\n')
            else:
                f.write('  SYMMETRY X Y ;\n')
            for i, (ox, oy) in enumerate(offsets):
                px = w / 2.0 + ox
                py = h / 2.0 + oy
                f.write('  PIN p%d\n    DIRECTION INOUT ;\n    USE SIGNAL ;\n    PORT\n'
                        '      LAYER metal1 ;\n        RECT %.4f %.4f %.4f %.4f ;\n'
                        '    END\n  END p%d\n' % (i, px - 0.05, py - 0.05, px + 0.05, py + 0.05, i))
            f.write('END %s\n\n' % mname)
        f.write('END LIBRARY\n')

    # ---- DEF
    n_fixed_macro = n_unplaced_macro = 0
    macro_area_fixed = 0.0
    std_area = 0.0
    bterms = []
    with open(os.path.join(args.out_dir, name + '.def'), 'w') as f:
        f.write('VERSION 5.8 ;\nDIVIDERCHAR "/" ;\nBUSBITCHARS "[]" ;\nDESIGN %s ;\n'
                'UNITS DISTANCE MICRONS %d ;\n\n' % (name, DBU))
        f.write('DIEAREA ( %d %d ) ( %d %d ) ;\n\n' % (dbu(die_x0), dbu(die_y0), dbu(die_x1), dbu(die_y1)))
        for i, r in enumerate(rows):
            f.write('ROW ROW_%d core %d %d N DO %d BY 1 STEP %d 0 ;\n'
                    % (i, dbu(r['x']), dbu(r['y']), r['n'], dbu(site_w)))
        f.write('\n')
        insts = [n for n in order if kind[n] != 'bterm']
        f.write('COMPONENTS %d ;\n' % len(insts))
        for n in insts:
            w, h, _ = nodes[n]
            k = kind[n]
            if k == 'core':
                std_area += w * h
                f.write('- %s %s ;\n' % (n, inst_master[n]))
            elif k == 'block':
                if n in macro_pos:
                    x, y = macro_pos[n]
                    n_fixed_macro += 1
                    macro_area_fixed += w * h
                    f.write('- %s %s + FIXED ( %d %d ) N ;\n' % (n, inst_master[n], dbu(x), dbu(y)))
                else:
                    n_unplaced_macro += 1
                    f.write('- %s %s ;\n' % (n, inst_master[n]))
            else:   # fixedblock terminal
                x, y = pl[n]
                f.write('- %s %s + FIXED ( %d %d ) N ;\n' % (n, inst_master[n], dbu(x), dbu(y)))
        f.write('END COMPONENTS\n\n')

        # BTerms: one per 1x1 terminal that belongs to a net
        for n in order:
            if kind[n] != 'bterm':
                continue
            pins = node_pins.get(n, [])
            if not pins:
                continue
            # a terminal on several nets gets one BTerm per net (name suffix)
            for j, (ox, oy, ni) in enumerate(pins):
                bname = n if len(pins) == 1 else '%s_%d' % (n, j)
                bterms.append((bname, ni, pl[n][0] + nodes[n][0] / 2.0, pl[n][1] + nodes[n][1] / 2.0))
        f.write('PINS %d ;\n' % len(bterms))
        bterm_of_net = defaultdict(list)
        for (bname, ni, cx, cy) in bterms:
            bterm_of_net[ni].append(bname)
            f.write('- %s + NET %s + DIRECTION INOUT + USE SIGNAL\n'
                    '  + LAYER metal1 ( -50 -50 ) ( 50 50 )\n  + FIXED ( %d %d ) N ;\n'
                    % (bname, nets[ni][0], dbu(cx), dbu(cy)))
        f.write('END PINS\n\n')

        f.write('NETS %d ;\n' % len(nets))
        for ni, (nname, pins) in enumerate(nets):
            f.write('- %s' % nname)
            for bname in bterm_of_net.get(ni, []):
                f.write(' ( PIN %s )' % bname)
            for (node, ox, oy) in pins:
                if kind[node] == 'bterm':
                    continue
                # pin index of this (node, net) incidence
                idx = next(i for (nn, i) in inst_pin_index[node] if nn == ni)
                inst_pin_index[node].remove((ni, idx))
                f.write(' ( %s p%d )' % (node, idx))
            f.write(' + USE SIGNAL ;\n')
        f.write('END NETS\n\nEND DESIGN\n')

    free_area = core_area - macro_area_fixed
    util_free = std_area / free_area if free_area > 0 else 1.0
    density = args.density if args.density else max(0.90, min(1.0, round(util_free + 0.10, 2)))

    with open(os.path.join(args.out_dir, 'vars.tcl'), 'w') as f:
        f.write('# generated by bench/bookshelf2lefdef.py from %s\n' % os.path.basename(args.aux))
        f.write('set platform iccad04\nset design %s\nset top_module %s\nset site core\n' % (name, name))
        f.write('set liberty_file {}\nset extra_liberty {}\nset dont_use {}\n')
        f.write('set global_place_density %.2f\nset global_place_pad 0\nset detail_place_pad 0\n' % density)
        f.write('set macro_place_halo {0 0}\n')
        f.write('# no technology: no liberty, SDC, wire RC or routing layers\n')

    stats = dict(design=name, nodes=len(nodes), movable_cells=sum(1 for k in kind.values() if k == 'core'),
                 macros_fixed=n_fixed_macro, macros_unplaced=n_unplaced_macro,
                 bterms=len(bterms), fixed_terminal_blocks=sum(1 for k in kind.values() if k == 'fixedblock'),
                 masters=len(masters), nets=len(nets), rows=len(rows), row_height=row_h, site_width=site_w,
                 die=[die_x0, die_y0, die_x1, die_y1], core_area=core_area, std_cell_area=std_area,
                 fixed_macro_area=macro_area_fixed, util_total=(std_area + macro_area_fixed) / core_area,
                 util_free=util_free, density=density)
    with open(os.path.join(args.out_dir, 'stats.json'), 'w') as f:
        json.dump(stats, f, indent=1)
    print('%s: %d cells, %d macros fixed (%d unplaced), %d pins, %d masters, %d nets, '
          'util %.3f total / %.3f of free area, density %.2f'
          % (name, stats['movable_cells'], n_fixed_macro, n_unplaced_macro, len(bterms),
             len(masters), len(nets), stats['util_total'], util_free, density))


if __name__ == '__main__':
    main()
