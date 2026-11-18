# Better Cuts Are Not Enough: Revisiting Min-Cut Placement with Modern Partitioners

**Artifact for double-blind review — DAC 2027.**
This repository accompanies the submission *Better Cuts Are Not Enough:
Revisiting Min-Cut Placement with Modern Partitioners*. It contains the complete source of the
placer, its integration with four hypergraph partitioners inside OpenROAD,
the benchmark harness, every experiment configuration behind the paper's
tables, the per-run results, and the scripts that regenerate and verify the
placement solutions, so that each number in the paper can be traced to a
file here and recomputed.
The repository is anonymized; please do not attempt to identify the authors.

---

## How to use this repository

* **To check a number in the paper:** open
  [`results/tables/0_paper_tables.md`](results/tables/0_paper_tables.md).
  It recomputes Tables I–III and the data behind every figure from the
  CSVs in `results/`, under the paper's rules, and its header maps each
  paper item to a section. [`results/tables/README.md`](results/tables/README.md)
  lists the extended tables (1–9) with, for each, what it contains, which
  paper table / figure / section it supports, which experiment folders it
  reads and which script writes it.
* **To see the raw data:** `results/<experiment>/runs.csv` (one row per
  run), described column by column in
  [`results/README.md`](results/README.md), which also groups the
  experiments into *main results*, *ablation and attribution*,
  *square-die hypotheses* and *development runs*.
* **To read the method:** `OpenROAD/src/mcp/` (§3 below maps the paper's
  method section to the source files), with the full command reference in
  [`OpenROAD/src/mcp/README.md`](OpenROAD/src/mcp/README.md).
* **To rerun:** build (§5), then `exp/run.sh exp/<config>.cfg` (§6); the
  configurations behind each paper section are listed there.

Names used throughout the tables and CSVs:

| in the paper | in the repository (`variant` column, table headings) |
|---|---|
| RePlAce | `gpl` (OpenROAD `global_placement -disable_pin_density_adjust -overflow 0.2`) |
| Capo 10.5 | `capo` |
| RB (TP), RB (HM), RB (MK), RB (FP) — recursive bisection with TritonPart, hMETIS, Mt-KaHyPar, FastPart | `tp`, `hm`, `mk`, `fp` in `e17_krow` / `e23_base` (older headings: `mcp-TritonPart` …); `cutnet_tp` / `mcp cutnet` = RB (TP) in the `x3_mega` and sweep grids |
| RB+*k* (TP) — RB with level-wise *k*-way reassignment | `kway_tp`, `mcp k-way` |
| few-row set / aspect-ratio sweep / square set | floorplans `<design>_u<util>_r<rows>_<pdk>` / `sweep_<design>_ar<ar>_util<util>_<pdk>` / `<design>_ar1_<pdk>` |

---

## 1. Summary

Top-down min-cut placement was displaced by analytical placement two
decades ago. We built a min-cut global placer as an OpenROAD module and
drove it with modern hypergraph partitioners (TritonPart, hMETIS,
Mt-KaHyPar and FastPart) to ask where, and why, the approach still
competes. Every ratio below is HPWL after detailed placement relative to
RePlAce on the same floorplan, geometric mean over the floorplans on which
no compared configuration fails (the paper's rule; see
`results/tables/0_paper_tables.md` for the counts).

| Finding | Evidence |
|---|---|
| On **few-row floorplans** (1–8 standard-cell rows) RePlAce succeeds on 137 of 180 floorplans; on those, RB (TP) has **18 % lower HPWL** (32 % lower at eight rows), and 11 % lower once the 11 floorplans where RePlAce's HPWL is more than twice ours are set aside. Capo 10.5 is also better than RePlAce there (0.856), so the advantage is the formulation's, not the partitioner's: the four engines span 0.804–0.862. | `results/tables/0_paper_tables.md` §2, `1_few_rows.md`, `4_capo.md`, `8_capo_attribution.md` |
| Across a **sweep of aspect ratio and utilization** (182 floorplans, 3 seeds) RB+*k* (TP) is 10.7 %, 1.7 % and 3.5 % better than RePlAce at aspect ratios 0.03, 0.06 and 0.09, and 10.5 % worse on square floorplans; the crossover is design-dependent. | `0_paper_tables.md` §3, `3_ar_util_sweep.md`, `9_seed_noise.md` |
| On the **square set** (20 floorplans, up to 2.95 M cells) RB (TP) trails RePlAce by 13.1 % and RB+*k* (TP) by 11.2 %; the four engines produce total cuts within 3 % of each other yet HPWL ratios from 1.143 to 1.221. | `0_paper_tables.md` §4, `2_ar1_designs.md` |
| The square-die gap is **not** a cut-quality problem: stronger partitioning gains 1.3 %, exact external-terminal positions 0.3 %, while cell-to-region assignments derived from RePlAce turn the 1.132 ratio into 0.983 although they cut 1.7–2.2× more nets. | `results/i1_info/STATUS.md`, `results/am1/`, `results/tables/5_american_maps.md`, `6_anatomy_of_gap.md` |
| The HPWL gain **survives global routing**: on the 87 few-row floorplans FastRoute routes for RePlAce and RB, RB (TP) is 4.6 % shorter in routed wirelength and has no greater total overflow than RePlAce on 77 of them. | `results/tables/7_global_routing.md` |
| The components that matter: on the few-row set the plain bisection core is already at 0.880, connectivity-guided whitespace, the leaf search and optimal in-row positions take it to 0.818, level-wise reassignment to 0.806; on the square set the same ladder goes 1.449 → 1.091. | `0_paper_tables.md` §5, `8_capo_attribution.md` |

The placer combines modern hypergraph partitioning, capacity-aware
whitespace allocation, level-wise *k*-way reassignment across earlier cut
lines, and bounded branch-and-bound optimization of the leaves. It is
roughly 12 K lines of C++ and runs as the command `mincut_placement` in
OpenROAD.

---

## 2. Repository layout

```
.
├── README.md                     this file
├── LICENSE                       BSD-3-Clause
├── OpenROAD/
│   ├── BASE_COMMIT               upstream OpenROAD commit the work is based on
│   ├── integration.patch         changes to upstream files (build, TritonPart, hooks)
│   ├── apply.sh                  clone upstream, apply the patch, add the module, build
│   └── src/mcp/                  the placer: `mincut_placement` (C++, Tcl, tests)
│       └── README.md             full command reference, every option explained
├── tools/                        bridges to the external partitioners and to Capo
│   ├── hmetis/                   driver around libhmetis (hMETIS 1.5)
│   ├── mtkahypar/                pinned Mt-KaHyPar build with process mapping (+ patches/)
│   ├── fastpart/                 FastPart server driver
│   └── capo/                     Capo 10.5 (UMpack) build notes
├── bench/                        benchmark harness (OpenROAD Tcl + Python)
│   ├── designs/<floorplan>/      vars.tcl + design.sdc of every floorplan used
│   ├── checkpoint_from_def.tcl   rebuild a floorplan checkpoint from a solution DEF
│   ├── verify_solution.tcl       recompute HPWL and legality of a solution DEF
│   ├── capo.tcl, capo_bridge.tcl OpenDB <-> Bookshelf bridge for Capo (CAPO_BRIDGE.md)
│   └── capo_verify.{tcl,py}      pin-by-pin check of the bridge against OpenDB
├── exp/                          experiment runner and the paper's configurations
│   ├── run.sh                    grid runner: designs × placers × seeds → JSON
│   ├── *.cfg                     one configuration per experiment (see §6)
│   └── paper_*.py, *_tables.py   the scripts that write results/tables
├── results/                      per-run results (CSV) and the tables
│   ├── README.md                 column reference; experiments by group
│   ├── <experiment>/runs.csv     one row per run (+ failure-accounting CSVs)
│   └── tables/                   0_paper_tables … 9_seed_noise (Markdown)
└── solutions/                    layout and tooling for the placement DEFs (see §8)
```

---

## 3. The placer

`OpenROAD/src/mcp` implements the method of the paper (Sec. "Methodology").
The core area is decomposed recursively; at every level the cells of a
region are split into two children by balanced min-cut hypergraph
partitioning, the whitespace is allocated and the cut line is shifted,
selected levels are revisited by a *k*-way reassignment, and the leaves are
placed by exact search. The main stages and the files that implement them:

| Stage (paper subsection) | What happens | Files |
|---|---|---|
| **Region tree** | A region is a rectangle of usable row area holding a set of movable cells. Until a cell has final coordinates, its location is the center of its region. | `Region.h`, `RegionTree.*`, `PlacementArea.*` |
| **Subproblem construction** (*Bisection*) | One area-weighted vertex per cell; connections leaving the region become *external terminals* (pins and fixed cells at their coordinates, other movable cells at their region centers), projected onto the nearer child. Nets that already span both children are dropped as constant. | `SubproblemBuilder.*`, `NetlistModel.*` |
| **Cell assignment** (*Bisection*) | Weighted cut-net minimization under explicit per-child area bounds, by any of the four engines. Regions of at most 16 cells are solved exactly by branch and bound. | `Engine.*`, `ExactPartition.*` |
| **Area bounds and whitespace** (*Area Bounds and Whitespace*) | The balance tolerance grows with the whitespace above the target density; after partitioning, free area is allocated by the *minlocal* policy (whitespace moves away from the external connections) and the cut line is moved to the resulting target, snapped to sites or rows, around fixed obstacles. | `Capacity.*`, `RegionTree.*` |
| **Waves** (*Bisection*) | Each level is cut in two spatially alternating waves so that the second wave sees the updated child centers of the first. | `Decomposer.*` |
| **Level-wise *k*-way reassignment** (*Level-Wise Reassignment*) | After levels 2–5 all current regions become the blocks of one mapping problem: every net pays the Steiner-tree length over the regions it occupies on a graph of neighboring region centers. Mt-KaHyPar refines the recursive assignment under fixed region boundaries and 2 % slack; the result is accepted only if the HPWL computed with every movable cell at its region center (fixed cells and I/O pins at their pin locations) improves. | `KwayRefine.*` |
| **Leaves** (*Leaf Placement*) | Regions of ≤ 12 cells: heuristic layout, then branch and bound over rows and in-row order (budget 2 × 10⁵ nodes), then *clumping* for optimal horizontal coordinates under non-overlap. Leaves are revisited once their neighbors are placed; *row ironing* re-optimizes sliding windows of seven cells across leaf boundaries. | `EndCase.*`, `Optimal.*`, `Ironing.*` |
| **Seeding and oracles** (*Why the Square-Die Gap Persists*) | The controlled experiments of the hypotheses H1–H3: a reference placement decides assignments by transport (*oracle assignments*, American maps), exact terminal positions (*oracle terminals*), placement feedback. Off by default. | `Assignment.*`, `Oracle.*`, `Feedback.*` |
| **Driver, command, GUI** | Option parsing, per-level report (JSON), GUI overlay of the decomposition. | `MinCutPlacer.*`, `MakeMinCutPlacer.*`, `mcp.tcl`, `mcp.i`, `graphics.*` |

Every option is documented in
[`OpenROAD/src/mcp/README.md`](OpenROAD/src/mcp/README.md). The
configurations used in the paper are:

```tcl
# RB (TP): recursive bisection, cut-net objective, two waves, minlocal
# whitespace, exact end game.  <density> is the floorplan's target density.
mincut_placement -k 2 -engine tritonpart -reserve minlocal -density <density> \
                 -max_tolerance 1.0 -optimal_cells 12 -feedback_passes 1

# RB+k (TP): the same plus level-wise reassignment after levels 2..5
mincut_placement ... -kway_refine_levels {2 3 4 5}

# other engines
mincut_placement ... -engine hmetis    -hmetis_path    tools/hmetis/hmetis_driver
mincut_placement ... -engine mtkahypar -mtkahypar_path tools/mtkahypar/bin/MtKaHyPar
mincut_placement ... -engine fastpart  -fastpart_path  tools/fastpart/fastpart_driver.py

detailed_placement   ;# legalize, as in the evaluation
```

---

## 4. Partitioner integration

All four engines run inside the same placement flow and see the same
subproblem: a hypergraph in local vertex ids (the region's cells, then
one fixed vertex per child for the terminals), per-vertex areas, net
weights, and lower/upper area bounds per child.

| Engine | How it is attached | Where |
|---|---|---|
| **TritonPart** (default) | In-process. We extended OpenROAD's `par` module with an in-memory k-way request (`KWayRequest`: fixed vertices, explicit per-part lower and upper bounds, optional initial solution, flat mode without coarsening) and with *net-vector objectives* (`NetVectors`, `NetVectorRefine`), so that its k-way FM, pair-wise FM and greedy refiners can optimize a geometric cost per net-occupancy mask instead of the cut. The cut-overlay clustering is disabled when bounds must be honored. These changes are the `src/par` part of `OpenROAD/integration.patch`. | `src/mcp/src/Engine.cpp` (`TritonPartEngine`), patch |
| **hMETIS** 1.5 | External process through hMETIS-format files. `hmetis_driver` wraps `libhmetis.a` (32-bit), passes the pre-assignment flag so fixed vertices are honored, and works around a heap corruption of hMETIS when a block has no fixed vertex. The library is not redistributable and must be obtained separately. | `tools/hmetis/`, `Engine.cpp` (`ExternalEngine`) |
| **Mt-KaHyPar** | Two roles. As a *bisection engine*: the `MtKaHyPar` binary through files (upper bounds honored). As the *k-way reassignment engine* of RB+*k*: linked as a library (`libmtkahypar.so`) with the Steiner-tree (process-mapping) objective and V-cycles from an initial solution; `tools/mtkahypar/build.sh` pins the upstream commit, enables the mapping features and applies `tools/mtkahypar/patches/` (one fix: the greedy one-to-one mapping overflows the stack when, from the second V-cycle on, a block outweighs every free processor). The CMake option `MCP_MTKAHYPAR_DIR` points at that build. | `tools/mtkahypar/`, `KwayRefine.cpp`, `Engine.cpp` |
| **FastPart** | A long-lived server (`fastpart_driver.py --serve`) with persistent single-threaded Mt-KaHyPar workers; every region is one connection on a Unix socket carrying a JSON request. This amortizes the engine start-up over the thousands of small regions of one placement. FastPart itself is referenced in the paper. | `tools/fastpart/`, `Engine.cpp` (`FastPartEngine`) |
| *Capo 10.5* (comparison only) | Bookshelf bridge written for this work (no third-party converter): the checkpoint is written as `nodes/nets/pl/scl` in integer DBU with pin offsets relative to the cell center, fixed instances and I/O pins as terminals, rows as `CoreRow`s with their gaps, and the flow's padding and target density; Capo runs; its `.pl` is read back and legalized by the same `detailed_placement` as every other placer. [`bench/CAPO_BRIDGE.md`](bench/CAPO_BRIDGE.md) documents the data-model mapping and its verification: on four kept Capo placements (gcd, aes, ibex, swerv; 431 850 pins) every Bookshelf pin coincides with the OpenDB pin after import to within 1 DBU, and Capo's reported HPWL equals the HPWL of the imported placement to all printed digits (`bench/capo_verify.tcl`, `bench/capo_verify.py`). | `bench/capo.tcl`, `bench/capo_bridge.tcl`, `tools/capo/build.sh` |

When an external engine fails on a region, the placer falls back to the
flat TritonPart engine for that region and records the failure in its
report. A run where this happened at the root bisection or on more than
1 % of the bisections counts as a failure of that engine
(`results/<exp>/rb_engine_reports.csv`).

---

## 5. Building

The placer is a module of OpenROAD. Reproduce the build in three steps
(or run `OpenROAD/apply.sh`, which performs them):

```bash
# 1. upstream OpenROAD at the exact base commit
git clone https://github.com/The-OpenROAD-Project/OpenROAD.git openroad
cd openroad && git checkout "$(cat ../OpenROAD/BASE_COMMIT)" && git submodule update --init --recursive

# 2. our changes: integration patch + the module directory
git apply ../OpenROAD/integration.patch
cp -r ../OpenROAD/src/mcp src/mcp

# 3. configure and build as usual (GCC 13, -O3 Release); optional Mt-KaHyPar
#    library for -kway_refine_levels
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DMCP_MTKAHYPAR_DIR=$PWD/../tools/mtkahypar     # omit to build without RB+k
cmake --build build -j
```

OpenROAD's own dependency instructions apply (its `etc/DependencyInstaller.sh`).
The patch also contains two small build conveniences: unit-test
subdirectories are guarded by `ENABLE_TESTS`, and the Rocky 8 static
`libstdc++` workaround can be switched off (`ROCKY8_STATIC_LIBSTDCXX=OFF`)
when a self-contained toolchain is used. `tools/*/build.sh` build the
external engines; each script states the pinned upstream commit.

The module ships with regression tests (`OpenROAD/src/mcp/test`) and a
C++ unit test of the transport assignment.

---

## 6. Benchmarks and experiments

Three benchmark sets (Table I of the paper), all from Yosys-mapped
netlists on Nangate45 and ASAP7 (the public PDK files under OpenROAD's
`test/` directory), plus one held-out set used only after the method was
fixed:

| Set | Floorplans | Description |
|---|---|---|
| **few-row** (F) | 180 | six designs (gcd, aes, ibex, jpeg, swerv, cva6; macro-free) × 2 PDKs × utilization {60, 70, 80} % × {1, 2, 4, 6, 8} rows |
| **aspect-ratio sweep** (A) | 182 | eight designs (+ mempool_tile, xiangshan; macro-free) × 2 PDKs × aspect ratio {0.03, 0.06, 0.09, 1} × utilization {60, 70, 80} %, 3 seeds |
| **square** (S) | 20 | ten designs (+ megaboom, mempool_group) × 2 PDKs at AR 1 and 70 % utilization, up to 2.95 M cells; macros fixed where present |
| held-out | 224 | seven further designs (b19, ethernet, fft_256, jpegencode, tate_151, tate_pairing, vga_lcd) × 2 PDKs in the few-row shapes (210) and square at 60 % (14); RePlAce vs RB+*k* only (footnote of the paper; `0_paper_tables.md` §6) |

Every placer starts from the same checkpoint: floorplan, rows, fixed
instances, I/O pins; standard cells unplaced. The binary checkpoints are
too large for this repository. They are produced from the OpenROAD flow
designs by `bench/make_krow_checkpoints.tcl` (few-row set),
`bench/make_base_checkpoints.tcl` (square set) and the sweep's floorplan
databases; `bench/designs/<floorplan>/vars.tcl` and `design.sdc` are
included for every floorplan. A checkpoint can also be rebuilt from
any placement DEF of the floorplan, since a DEF carries the complete
floorplan (`bench/checkpoint_from_def.tcl` unplaces the movable cells
again):

```bash
MCP_DESIGN=aes_u70_r2_nangate45 MCP_DEF=<any placement of that floorplan>.def.gz \
  openroad -exit bench/checkpoint_from_def.tcl        # -> bench/designs/aes_u70_r2_nangate45/floorplan.db
```

The runner then executes a configuration over designs × placers × seeds,
one JSON per run, resumable and parallel:

```bash
OPENROAD=/path/to/openroad exp/run.sh exp/x3_mega.cfg -j 8 --keep-def
python3 exp/summarize.py results/x3_mega
python3 exp/paper_export.py && python3 exp/paper_numbers.py && python3 exp/paper_tables.py   # -> results/tables
```

Configurations behind the paper (each file documents its arms and settings):

| Configuration | Paper | Content |
|---|---|---|
| `x3_mega.cfg` | Tables II, III; Figs. 1, few-row, square | RePlAce, RB (TP), RB+*k* (TP) on the few-row and square sets (the RePlAce reference of every few-row and square ratio) |
| `e17_krow.cfg`, `e17_krow_big.cfg` | Table II (few-row), Fig. few-row, routing | Capo, RB with TP / HM / MK / FP on the 180 few-row floorplans; RePlAce with routing |
| `e23_base.cfg` | Table II (square), Fig. square | Capo, RB with the four engines on the 20 square floorplans |
| `x4_ar_sweep.cfg` | Figs. 1, sweep; seed-noise footnote | RePlAce, RB (TP), RB+*k* (TP) on the 182 sweep floorplans, 3 seeds |
| `x5_heldout.cfg` | benchmark footnote | RePlAce, RB+*k* (TP) on the held-out set |
| `e26b_norevisit.cfg`, `e26_ablation.cfg`, `r5_ablation_square.cfg` | Table III | cumulative ladder of the placer's components on the few-row and square sets |
| `e27_capo_attr.cfg`, `e14_capo.cfg` | Sec. few-row ("the advantage predates our implementation") | leave-one-out on RB (TP) and the partitioner at the base rung, against Capo (table 8); Capo settings |
| `r1_fewrow_oracle.cfg` | Sec. few-row ("Does legalization explain the advantage?") | RePlAce-derived assignments with our whitespace allocation and leaf placement |
| `i1_info.cfg`, `am1.cfg` | Sec. "Why the Square-Die Gap Persists" (H1–H3, Fig. anatomy) | stronger / degraded partitioning, oracle terminals, HPWL-estimating objective, oracle assignments (American maps) at all or some levels |
| `e8_seedlevels.cfg`, `e18`–`e21*.cfg`, `e24*.cfg`, `e12`–`e16*.cfg`, `e25_objectives.cfg` | background (development) | seeded levels, hybrid flows, DPO, engine and objective studies on the development checkpoints |
| `e0_smoke.cfg` | — | smoke test of the installation on gcd |

**Metric and rules** (Sec. "Evaluation" of the paper). HPWL after OpenROAD
detailed placement and before detailed-placement optimization, for every
placer alike; ratios are to RePlAce on the same floorplan; aggregates are
geometric means over floorplans with the count of floorplans won in
parentheses. A RePlAce run that diverges, fails legalization or stops
above its 20 % overflow target is a failure
(`results/<exp>/replace_convergence.csv`); an RB run whose partitioner
failed at the root or on more than 1 % of the bisections is a failure
(`results/<exp>/rb_engine_reports.csv`); every ratio is taken over the
floorplans common to the configurations compared. Every legal placement of
the few-row and square sets was also global-routed
(`global_route -allow_congestion`, after DPO); routed wirelength and
overflow are compared in `results/tables/7_global_routing.md` on the
floorplans FastRoute completed for every placer.

---

## 7. Results

`results/` holds, per experiment, the per-run table (`runs.csv`: design,
placer, seed, HPWL before and after legalization, routed wirelength,
runtimes, legality), the failure-accounting files, and the experiment's own
summaries. `results/tables/` holds the Markdown tables:

| table | what it is | paper |
|---|---|---|
| `0_paper_tables.md` | **the paper's Tables I–III and the numbers behind every figure**, recomputed under the paper's rules; also the held-out designs | all |
| `1_few_rows.md` | few-row set by row count, design, PDK, utilization; routed wirelength; runtime | Table II, Fig. few-row |
| `2_ar1_designs.md` | square set per floorplan; runtime | Table II, Fig. square |
| `3_ar_util_sweep.md` | aspect ratio × utilization by design and row count; outliers; runtime | Figs. 1, sweep |
| `4_capo.md` | Capo against RePlAce and against RB on both sets; Capo settings | Table II (Capo), Sec. few-row |
| `5_american_maps.md` | RePlAce-derived assignments through the min-cut tree (H3) | Sec. "Why the Square-Die Gap Persists" |
| `6_anatomy_of_gap.md` | development history of the gap; H1 / H2 arms; legalization and DPO; hybridization | background, Fig. anatomy (a) |
| `7_global_routing.md` | routed wirelength and overflow of every placer | Sec. few-row and square (routing) |
| `8_capo_attribution.md` | where the advantage over Capo comes from: ladder, leave-one-out, partitioners | Sec. few-row |
| `9_seed_noise.md` | seed sensitivity of RB / RB+*k* over three seeds on the sweep | footnote in Sec. "Evaluation" |

`results/tables/README.md` has the long form of this table (floorplans,
placers, data folders, generating script per table). Tables 1–6 use a
weaker condition for RePlAce than the paper (any legal placement, 149
few-row floorplans instead of 137), so their aggregates differ slightly from
the paper's; table 0 and tables 7–9 use the paper's rule. All scripts read
only the CSVs in `results/`.

---

## 8. Solutions

Placement solutions are not stored in the repository; they are produced
by the harness. `exp/run.sh --keep-def` saves, for every run, the legal
placement whose HPWL is the reported number (the placer's output after
OpenROAD `detailed_placement`, before DPO) as `<floorplan>__<placer>__s<seed>.def.gz`
next to the run's JSON, and `exp/collect_solutions.py` arranges them as
`solutions/<set>/<floorplan>/<placer>_s<seed>.def.gz` with a manifest:

```bash
X3_SET=krow_small exp/run.sh exp/solutions_x3.cfg -j 8 --keep-def   # few-row set (also krow_big, base_small, base_big)
X4_SET=small      exp/run.sh exp/solutions_x4.cfg -j 8 --keep-def   # sweep set (also big)
python3 exp/collect_solutions.py solutions                            # -> solutions/<set>/<floorplan>/*.def.gz, manifest.csv
```

Placer names in the archive: `replace`, `rb_tp`, `rbk_tp`, `rb_hm`,
`rb_mk`, `rb_fp`, `capo`. A solution can be checked independently of
the harness, from the DEF and the public PDK LEFs alone:

```bash
MCP_DEF=solutions/few_row/aes_u70_r2_nangate45/rbk_tp_s1.def.gz MCP_PLATFORM=nangate45 \
  openroad -exit bench/verify_solution.tcl
# movable_placed 12794  movable_unplaced 0
# hpwl_um 1234567.8
# legal 1
```

---

## 9. Notes for reviewers

* **Anonymity.** Author names, affiliations and machine-specific paths
  have been removed from all files. Copyright notices follow OpenROAD's
  convention ("The OpenROAD Authors", BSD-3-Clause) because the module is
  written as an OpenROAD module; they do not identify the authors.
* **Not included.** The hMETIS library (license), the Capo binary (built
  from the public UMpack mirror by `tools/capo/build.sh`), the PDK files
  (part of the OpenROAD checkout), the binary checkpoints and the
  placement DEFs (both regenerated by the scripts of §6 and §8), and the
  per-run JSON reports of the placer (the CSVs in `results/` are exported
  from them).
* **Compute.** Re-placing the three sets with RePlAce, RB and RB+*k*
  (one seed) takes about 230 hours of run time at four threads per run,
  or about 30 hours with eight runs in parallel on a 40-core server;
  the largest square floorplans (megaboom, mempool_group) take up to
  six hours each. `e0_smoke.cfg` and any few-row floorplan run in
  minutes. All reported experiments used four threads per run on a
  two-socket Xeon Gold 6148 server.
* **Seeds and determinism.** RePlAce is deterministic. The min-cut placer
  is seeded but partitions regions in parallel, so repeated runs differ.
  The sweep set was run with three seeds per configuration and is the
  basis of `results/tables/9_seed_noise.md`: the per-floorplan coefficient
  of variation of RB (TP) and RB+*k* (TP) has median 0.8 % (max–min spread
  median 1.7 %, 90th percentile 4 %, worst 16.5 % on one ibex floorplan at
  80 % utilization), the geometric-mean ratios to RePlAce move by at most
  0.5 % between seeds, and the win/loss sign changes on 15 of 181
  floorplans, all with ratios near 1. The few-row and square grids were run
  with one seed.

---

## License

BSD-3-Clause, see [`LICENSE`](LICENSE). OpenROAD, TritonPart, Mt-KaHyPar
and the benchmark designs keep their own licenses.
