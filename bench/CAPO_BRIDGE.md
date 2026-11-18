# Running Capo on our LEF/DEF designs: the Bookshelf bridge

Capo 10.5 is the reference top-down min-cut placer, and we wanted it in the
comparison (E14, E16) on exactly the checkpoints the other placers see. The
problem: our designs live in OpenROAD's database (LEF/DEF, loaded from
`bench/designs/<design>/floorplan.db`), and the public Capo only reads the
UCLA Bookshelf format. This note explains how the two were connected. The
whole bridge is one Tcl script, `bench/capo.tcl` (≈ 270 lines), plus the
build script `tools/capo/build.sh`.

## 1. Why a bridge was needed at all

The original UMpack binaries had a `LEFDEF : file.lef file.def` input mode, but
the packages behind it (`ParserLEFDEF`, `DB`, `MetaPlacer`) are not in the
only public source today, the mirror
[i-markov/UMpack-32-June2026](https://github.com/i-markov/UMpack-32-June2026)
(the 2012 distribution modernized for C++20). What that mirror builds is
`CapoTest1.exe`, a driver that reads a Bookshelf `.aux` and writes a
Bookshelf `.pl`. OpenROAD, on the other side, has no Bookshelf writer or
reader. So the bridge has three parts, all executed inside one `openroad`
process by `bench/capo.tcl`:

1. **export** the loaded checkpoint as Bookshelf files,
2. **run** `CapoTest1.exe` on them,
3. **import** Capo's `.pl` back into the database,

after which the standard evaluation of the harness (`mcp_eval` in
`bench/common.tcl`: `detailed_placement`, `improve_placement`,
`global_route`, timing) runs unchanged on the result. Capo is therefore
measured through the same legalizer, router and metrics as `gpl` and `mcp`.

## 2. What Bookshelf needs and where it comes from

Bookshelf describes a row-based placement problem in five text files named by
an `.aux`:

```
RowBasedPlacement : design.nodes design.nets design.wts design.pl design.scl
```

| file | content | source in OpenDB |
|---|---|---|
| `.nodes` | every node with width and height; fixed ones flagged `terminal` | `dbInst` (master width/height), `dbBTerm` (I/O pins) |
| `.nets` | hyperedges: node, direction, pin offset from the node center | `dbNet` → `dbITerm` / `dbBTerm`, offsets from `dbMTerm` bounding boxes |
| `.pl` | a position and orientation for every node; `/FIXED` for terminals | `dbInst` bounding box and orientation, `dbBTerm` bounding box |
| `.scl` | the rows: y, height, site width and spacing, orientation, subrows | `dbRow` (origin, site, site count) |
| `.wts` | net weights | empty (all weights 1) |

The choices that made the files correct for Capo:

**Units.** Everything is written in database units (DBU: 1/2000 µm on
nangate45, 1/1000 µm on sky130hd), as integers. Capo stores coordinates as doubles, so
microns would also have worked, but integer DBU avoids accumulating rounding
error over hundreds of 0.19 µm sites and makes the import exact.

**Nodes.** Movable standard cells are written with their master's width and
height. Instances that `dbInst::isFixed()` reports (tap cells, end caps,
macros placed by the floorplan) are written as `terminal` nodes with their
real size; Capo's row reader (`RBPlaceIO`) carves terminals that overlap rows
out of the row sites, so a fixed macro or a tap cell becomes an obstacle, as
it is for `gpl`. I/O pins are written as 1 × 1 terminals at the center of
their geometry.

**Pin offsets.** Capo follows the ISPD-2005 convention: a pin's position is
the node's *center* plus the offset. OpenDB gives pin geometry relative to
the lower-left corner of the master in orientation `R0`, so the exporter
writes `(pin center − master width/2, pin center − master height/2)`. I/O
pins are their own node and get offset `0 0`.

**Nets.** Special and supply nets are skipped, and so are nets with fewer
than two pins, the same filter as the harness's HPWL. Direction letters are
`I`/`O`/`B` from the `dbMTerm` I/O type (for a block pin, inverted: a design
input drives the net). Capo does not use them for wirelength.

**Rows.** One `CoreRow Horizontal` per row with `Coordinate` (y), `Height`,
`Sitewidth`, `Sitespacing`, `Siteorient`, and `SubrowOrigin … Numsites`.
Around macros OpenROAD cuts a row into several `dbRow`s with the same y;
Capo aborts on two `CoreRow`s at one coordinate ("Please use subrows"), so
rows are grouped by y and the pieces written as subrows of one `CoreRow`,
sorted by x. (This is what the tinyRocket runs of E16 needed; the four other
designs have unbroken rows.)

**Initial positions.** Capo ignores the starting location of movable cells,
so the `.pl` lists them at `0 0 : N`; fixed nodes carry their real lower-left
corner and orientation with `/FIXED`.

A fragment of the gcd export (DBU):

```
# design.nodes                      # design.pl
UCLA nodes 1.0                      UCLA pl 1.0
NumNodes : 515                      _345_     0       0      : N
NumTerminals : 168                  PHY_EDGE_ROW_14_Right_14  180120  61600 : MY /FIXED
    _345_    2660   2800            clk       200190  52220  : N /FIXED
    PHY_EDGE_ROW_14_Right_14  380  2800  terminal
    clk      1      1      terminal

# design.nets                       # design.scl
NetDegree : 2    _000_              CoreRow Horizontal
    _672_   I : -1450.0 -170.0        Coordinate   : 22400
    _504_   O :     0.0  135.0        Height       : 2800
                                      Sitewidth    : 380
                                      Sitespacing  : 380
                                      Siteorient   : R0
                                      Sitesymmetry : Y
                                      SubrowOrigin : 20140   Numsites : 422
                                    End
```

## 3. Making the problem the same one the other placers solve

Two things in the flow are not part of the netlist and had to be translated
into Capo's terms.

**Cell padding.** `gpl` and `mcp` place cells inflated by
`-pad_left/-pad_right` sites (2 on nangate45, 4 on sky130hd) to leave routing
room. Capo has no such option, so the exporter widens every movable node by
`2 · pad · site width` and, on import, shifts the cell `pad · site width` to
the right of Capo's lower-left corner — the real cell sits centered in its
padded footprint, and because pin offsets are relative to the center they
stay exact. `MCP_CAPO_PAD` selects the padding (the grids pass
`$::global_place_pad`, the platform's value).

**Target density.** The flow places at a target density (0.3 on nangate45,
0.5 on sky130hd). Capo's equivalent is `-densityTarget <percent>`, its
ISPD-2006-style whitespace allocation. One subtlety that decided how E14 was
reported: Capo's density bounds the *unpadded* cell area per bin, whereas
`gpl`/`mcp` apply the density to the padded cells. Both readings were
measured in E14; the padded arm at the flow density (`capo_pad_dens`) is the
one comparable to `gpl` and `mcp` and is what E16 uses.

Capo's own defaults (no `-densityTarget`) pack cells to ~97 % local density;
on ibex that placement cannot be legalized with the detail padding and 2 of 3
seeds fail `check_placement`, which is why the density arm, not the default,
is the headline number.

## 4. Running Capo

`CapoTest1.exe -f design.aux -densityTarget 30 -savePl design.capo.pl`,
executed from inside the directory that holds the files (the `.aux` names them
relatively).

**Seeds.** Capo has no seed option; UMpack's random generator reads a
`seeds.in` in the working directory (line 1: `0`, line 2: the external seed)
and logs every draw to `seeds.out`. Each run therefore gets its own directory
with its own `seeds.in` carrying the harness seed, which makes the three seeds
of a grid distinct and the runs reproducible.

**Scratch disk.** Capo flushes `seeds.out` after every random object; on NFS
that turned a 0.3 s gcd run into 18 s and a 40 s aes run into 140 s. The
Capo directory goes to `MCP_SCRATCH` (local `/tmp`) when the harness sets it;
only `design.capo.log` and `design.capo.pl` are copied back next to the
results (`<prefix>.capo.log`, `<prefix>.capo.pl`).

**Libraries.** `CapoTest1.exe` is linked against the UMpack shared libraries
(`libCapo.so`, `libMLPart.so`, …); `LD_LIBRARY_PATH` must contain
`<UMpack>/lib` (the binary also has an rpath to its build location).
`tools/capo/build.sh` clones the mirror, answers its interactive `perl
config`, and builds with the conda gcc 13 of `openroad-env` (the system gcc 9
does not compile it).

**Timing.** The log prints `CAPO_SECONDS`, the wall time of the `exec`
(Capo's parsing, placement and writing). The placer time in the results
(`t_place`) is the harness's usual measure, the whole placement step, so for
Capo it includes the Tcl export and import: 0.55 s against 0.32 s of Capo on
gcd, 48 against 46 s on aes, 118 against 113 s on jpeg.

## 5. Reading the result back

Capo's `.pl` has one line per node, `name x y : orient`, with `/FIXED` on the
terminals. The importer skips the fixed lines and the terminals, looks up each
movable instance by name, adds the padding offset to x, maps the Bookshelf
(DEF) orientation names to OpenDB's (`N→R0`, `S→R180`, `FN→MY`, `FS→MX`, …),
and calls `setLocation` / `setOrient` / `setPlacementStatus PLACED`. Capo
places every cell in a row with the row's orientation, so the result is
already legal before `detailed_placement`; `dpl` then only inserts the detail
padding and `improve_placement` runs as for every other placer.

```
# design.capo.pl (Capo's output)                  -> in OpenDB
   _345_      128440       72800 :  N              _345_ at (128440 + 760, 72800), R0, PLACED
```

## 6. How to run it

```bash
tools/capo/build.sh                      # -> /tmp/$USER/UMpack/Capo/CapoTest1.exe
export LD_LIBRARY_PATH=/tmp/$USER/UMpack/lib:$LD_LIBRARY_PATH

# one design by hand
MCP_DESIGN=gcd_nangate45 MCP_SEED=1 MCP_DPO=1 \
MCP_CAPO=/tmp/$USER/UMpack/Capo/CapoTest1.exe \
MCP_CAPO_ARGS='-densityTarget [expr {int(100*$::global_place_density)}]' \
MCP_CAPO_PAD='$::global_place_pad' MCP_SCRATCH=/tmp/$USER/mcp \
MCP_OUT=runs/gcd_capo ./setup_openroad_env.sh run OpenROAD/build/bin/openroad \
  -exit -no_splash -log runs/gcd_capo.log bench/capo.tcl

# in a grid: variant "name|capo|<CapoTest1 args>|<pad sites>"
CAPO=/tmp/$USER/UMpack/Capo/CapoTest1.exe exp/run.sh exp/e16_compare.cfg -j 4
```

`MCP_CAPO_ARGS` and `MCP_CAPO_PAD` are `subst`ed after the checkpoint is
loaded, so they can refer to the platform variables of `vars.tcl`.

## 7. What is and is not carried across

Carried: the full netlist (signal nets ≥ 2 pins), exact cell sizes and pin
offsets, every fixed instance and macro as an obstacle, I/O pins at their
positions, the rows with their gaps, the flow's padding and target density,
the seed. Not carried: timing (Capo is wirelength-driven only; neither `gpl`
default nor `mcp` is timing-driven in these grids either), routing-layer
information, `dont_use`, net weights (all 1). Capo's legalizer and its
`-densityTarget` whitespace model replace `gpl`'s density penalty, which is
the intended difference.

The bridge was validated the simple way: `check_placement` passes after
import on every design; the HPWL Capo prints for its own placement
(`RBPl Est Pin-to-Pin HPWL`, in DBU) agrees with the harness's HPWL of the
imported placement before `detailed_placement` to 0.1 % (gcd 5 358 vs
5 363 µm, aes 668 110 vs 668 340, jpeg 1 830 650 vs 1 829 949; the residue is
the 1 × 1 I/O terminal against the real pin geometry); and the Bookshelf
counts in the log (`Bookshelf written: 347 movable, 168 terminals, 384 nets,
1153 pins, 57 rows` for gcd) equal the counts the min-cut placer reports for
the same checkpoint.

## 8. Verification against OpenDB (`bench/capo_verify.tcl`, `bench/capo_verify.py`)

Pin locations are the one place where a LEF/DEF-to-Bookshelf translation
can go wrong silently (cell origins, pin offsets relative to the center,
orientations). The bridge was therefore checked pin by pin on four kept
Capo placements of the few-row set (`results/e17_krow/*__capo__s1.capo.pl`):
the checkpoint is exported again, Capo's `.pl` is imported, and for every
signal pin of every instance the Bookshelf pin (node lower-left + size/2 +
offset from `design.nets`) is compared with the center of the OpenDB
terminal after import; the three HPWLs — the one Capo prints for its own
placement, the one recomputed from the Bookshelf files, and the one of the
OpenDB pin geometry with I/O pins — are compared as well.

```bash
D=aes_u60_r1_asap7; R=results/e17_krow/${D}__capo__s1; V=scratch/capo_verify/$D; mkdir -p $V
MCP_DESIGN=$D MCP_PL=$R.capo.pl MCP_VDIR=$V openroad -exit bench/capo_verify.tcl > $V/verify.log
python3 bench/capo_verify.py $V $R.capo.pl $R.capo.log
```

| floorplan | pins compared | Bookshelf pin − OpenDB pin | HPWL: Bookshelf / Capo log | HPWL: OpenDB / Capo log |
|---|---|---|---|---|
| gcd, 70 %, 4 rows, Nangate45 | 1 564 | 0 dbu on every pin | +0.000 % | +0.000 % |
| aes, 60 %, 1 row, ASAP7 | 38 533 | mean 0.21 dbu, max 1 dbu | +0.000 % | +0.000 % |
| ibex, 80 %, 8 rows, Nangate45 | 52 094 | 0 dbu on every pin | +0.000 % | +0.000 % |
| swerv, 60 %, 2 rows, ASAP7 | 339 659 | mean 0.23 dbu, max 1 dbu | +0.000 % | −0.000 % |

The ≤ 1 dbu differences on ASAP7 are the integer rounding of odd pin-box
centers (Bookshelf offsets are relative to the cell center, which falls on a
half-dbu for cells of odd width); Nangate45 has none. No floorplan has a
fixed instance with a non-R0 orientation and signal pins (where the R0 pin
offsets of the export would be wrong), and Capo returns every movable cell
in orientation N, so the bridge never has to apply an orientation; the
row orientation is restored by `detailed_placement` before the common
evaluation. The harness's HPWL metric (`mcp_hpwl`, the same for every
placer) is within 0.25 % of Capo's logged value on gcd and within 0.04 % on
the three larger floorplans; the residue is its treatment of I/O pins, not
the bridge.
