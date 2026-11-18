# American maps in `mcp`: connectivity versus geometry at every level of the tree

**Status:** study design, 2026-10-30. Nothing below has run beyond what is
cited from `HYBRID_PLACEMENT_PLAN.md` §6.1 (E8, E18–E21, E24). Numbers
without a citation are hypotheses or estimates.

**Replaces** the ICCAD04 table in the paper (grid `x5_iccad04` stopped
2026-10-30; partial results kept in `results/x5_iccad04/`).

---

## 1. The idea and where it comes from

An *American map* (Vygen, "Geometric quadrisection in linear time, with
application to VLSI placement", Discrete Optimization 2005; the assignment
step of BonnPlace) is the answer to this question: given approximate cell
coordinates \(p_i\) from an analytical placement and a region divided into
child regions \(R_q\) with capacities \(C_q\), assign every cell to a child so
that the total area-weighted Manhattan displacement is minimal,

\[
\min_\pi \sum_i a_i\, d_1(p_i, R_{\pi(i)}) \quad\text{s.t.}\quad
\sum_{i:\pi(i)=q} a_i \le C_q .
\]

The optimal assignment is not "the quadrant the cell is in": overloaded
children shed cells across the lines, and the resulting regions of
assignment have the irregular, state-like borders that gave the method its
name. Vygen shows the fractional four-child problem is solvable in \(O(n)\)
with at most three fractional cells; later BonnPlace replaced the special
routine by a general transportation solver.

The contrast with what `mcp` does from scratch is the point of the study:

| | min-cut split (`mcp` default) | American-map split |
|---|---|---|
| input | hypergraph + region geometry | cell coordinates + region geometry |
| objective | cut nets / geometric net objective | displacement from the coordinates |
| uses connectivity | directly | only through the coordinates |
| needs a seed | no | yes |
| capacity constraints | yes | yes |

Both decide the same thing — which child each cell goes to — under the same
capacities, so they can be swapped level by level inside one tree with one
back end. That makes the American map a *diagnostic instrument* for the
placement-quality gap, not only a placer.

## 2. What already exists in `mcp`

* **The operator.** `OpenROAD/src/mcp/src/Assignment.{h,cpp}`:
  `solveTransport()` is the fractional transportation problem cells ×
  children with cost \(a_i\, d_1(p_i, \text{box}_q)\)
  (`SubproblemBuilder::boxDistance`), per-child lower/upper bounds, solved
  by `lemon::NetworkSimplex`; the ≤ k−1 split cells are rounded to their
  larger share and the bounds repaired greedily. This *is* the American-map
  problem; the solver differs (general min-cost flow instead of Vygen's
  linear-time quadrisection).
* **Quadrisection.** `RegionTree::chooseK` plans k = 4 when the region's
  aspect ratio is in [0.5, 2] (`-k 4`), k = 2 otherwise; the transport
  handles any k. `-k 4 -seed_placement -assignment transport_only` is
  geometric quadrisection of a seed placement.
* **Seeds.** `bench/mincut.tcl` `MCP_PRE=gpl_ovf<τ>` runs `global_placement`
  stopped at density overflow τ as the seed (E19: τ 0.3 best on ibex,
  0.5 on aes, worse than 0.1 on gcd).
* **Per-level mixing.** `-seed_levels L`: seed drives levels < L
  (assignment, clamped terminals), min-cut from scratch below
  (`Decomposer::seededAt`). `exp/seedlevels.py` attributes the loss per
  level (E8).
* **Net-aware variant.** `-assignment_net_weight λ`: cost
  \(d_1(p_i, \text{box}_q) + \lambda\,\Delta\text{HPWL}_i(q)\), two
  Gauss–Seidel rounds (E20b; λ 0.5 best).
* **Per-level oracle.** `-oracle qp` re-solves a B2B QP of all cells after
  every level, anchored to their region centres, and (with
  `oracle_terminals`, default) writes the clamped positions into `loc_` —
  the same vector `buildAssignment` reads. So the BonnPlace loop
  "positions re-solved inside the regions → American-map the cells into
  the children" is expressible today as
  `-seed_placement -assignment transport_only -oracle qp`. Never run.
* **Report.** `.mcp.json` per level: `assignment_dhpwl_um`,
  `assignment_split`, `assignment_repairs`, `assignment_infeasible`,
  `seed_outside`, `oracle_hpwl`, `oracle_seconds`, `decompose_seconds`.
  No timer on the transport itself yet.

## 3. Evidence so far (gcd / aes / ibex, 3 seeds, post-DPO HPWL / `gpl`)

From `HYBRID_PLACEMENT_PLAN.md` §6.1:

| arm | gcd | aes | ibex | source |
|---|---|---|---|---|
| X: min-cut from scratch (TritonPart) | 1.074 | 1.139 | 1.221 | E16/E21 |
| AM: `gpl -overflow 0.3` → transport_only → end game | 1.016 | 0.940 | 1.000 | E19 |
| AM at τ 0.5 | 1.053 | 0.929 | 1.049 | E19 |
| AM + min-cut refinement of the assignment (δ 0.3–1, conn/hpwl/rsmt) | 1.016–1.019 | 0.950–0.959 | 1.015–1.021 | E19b, E20a |
| AM-λ: net-aware transport, λ 0.5, τ 0.3 / 0.5 | 1.021 / 1.072 | 0.942 / **0.924** | 1.001 / 1.050 | E20b |
| X + QP oracle + ACG (min-cut decides, analytical informs) | 1.05 | 1.00 | 1.165 | E21d |
| X on the k-row dies (105 floorplans, geomean) | — | 0.879 | — | E17/E24 |
| X + QP oracle + ACG on the k-row dies | — | 0.896 | — | E24 |
| X on the 14 AR-1 base designs | — | 1.152 | — | E23/E24 |

What these establish: (i) with analytical positions available, the
min-displacement assignment beats every min-cut refinement of the same
positions, under every objective and position model tried (E4, E11, E19b,
E20a, E20b); (ii) the gap is not the partitioner (E21e: four engines within
3 % after hybridization) nor the objective (E20a); (iii) the analytical
guide is worth less than connectivity where `gpl` itself is weak (k-row,
E24). What they do *not* establish: any of this beyond three macro-free
designs; quadrisection vs bisection; the BonnPlace loop; *where in the
tree* and *how much* min-cut and geometry disagree.

## 4. Hypotheses

Each with the measurement that would falsify it.

* **H-AM1 (geometry beats connectivity at every level).** On the square
  base designs, replacing the min-cut split by the American-map split of a
  `gpl -overflow τ` seed, with the identical tree, capacity policy, end
  game and DPO, lowers post-DPO HPWL at every level L of the tree: HPWL(AM
  above L, min-cut below) is monotone decreasing in L. *Falsified if* the
  per-level marginal gain changes sign on ≥ 1/3 of the designs, or if the
  fully seeded AM is not below X by ≥ 5 % geomean over the 20 base designs.
* **H-AM2 (disagreement at a level predicts the final loss from deciding
  that level by min-cut).** Two quantities per design and level L, measured
  independently: the *disagreement* between the min-cut and the
  American-map partition of the same regions (area fraction, and the
  surrogate ΔHPWL between the two partitions with the region's local
  pins), and the *consequence*, the post-DPO HPWL of "American map at every
  level except L, min-cut at L" relative to the all-American-map run
  (`AM2_notL`, §5). A large disagreement fraction does not by itself mean
  a large loss — the two partitions may be equally good — so the
  hypothesis is about prediction: across designs × levels, the
  consequence correlates with the surrogate ΔHPWL of the disagreement
  (Spearman ρ ≥ 0.6) and more strongly than with the bare area fraction;
  and both the disagreement and the consequence peak at levels 2–5
  (regions of 10³–10⁵ cells), falling towards the leaves where the end
  game decides anyway. *Falsified if* ρ < 0.3, i.e. levels with large
  disagreement are not the levels whose min-cut decision costs wirelength
  — then disagreement is a symptom without consequence and the mechanism
  claim has to be dropped. (E8's marginal-loss curve predicts the middle
  levels; this is the direct, paired measurement.)
* **H-AM3 (quadrisection ≥ bisection).** One capacitated four-way
  assignment per region is at least as good as two successive bisections
  of the same seed (the vertical and horizontal lines are chosen jointly
  with the capacities): post-DPO HPWL of AM-4 ≤ AM-2 within noise, with
  fewer `assignment_repairs` and lower `seed_outside`. *Falsified if*
  AM-4 is > 1 % worse geomean. (From scratch, k = 4 lost everywhere —
  `MinCutPlacer.h` comment at `kway_construct_levels`; the seeded case is
  different because the assignment is not a partitioning search.)
* **H-AM4 (re-solving positions per level beats a frozen seed on dense
  designs).** The pre-/post-DPO gap of E19 (aes τ 0.5: 1.041 before, 0.929
  after DPO) is side-of-cut error from decomposing a half-spread seed. The
  BonnPlace loop — QP re-solved with cells confined to their regions, then
  American map — reduces `seed_outside` and the pre-DPO HPWL and improves
  the post-DPO result on the dense designs (ibex-like, ≥ 40 %
  utilization), where E19's optimum τ was the smallest. *Falsified if*
  AM-oracle is not better than AM on the dense half of the base set, or
  if, as in E24, the QP oracle is worse than the frozen seed on the macro
  designs — a *weak* QP is the known risk (E4: unconstrained QP is a
  clump; E24: the QP knows nothing of macros).
* **H-AM5 (the converse holds on the k-row dies).** Where `gpl` is weak
  (narrow dies, pins on one edge; min-cut beats it by 12 %), the
  American-map split of a `gpl` seed is *worse* than min-cut from scratch,
  and worse by more the narrower the die: geometric information is worth
  only as much as the analytical placer that produced it. Two conditions
  on the measurement. (a) *Failed `gpl` runs count.* On the k-row set
  `gpl` diverges (GPL-0305) or produces un-legalizable results on ~7 % of
  the floorplans (E17 11/180, x3_mega 13/360 so far); a seed that
  diverges gives AM nothing. These are not dropped: every table reports,
  per arm, the number of floorplans that failed, and the geomean is given
  both over the common successful subset *and* over the full set with a
  failed arm scored at the worst successful arm's ratio on that floorplan
  (a conservative imputation; if the ranking differs between the two
  views, that itself is reported). A fallback policy "AM when the seed
  exists, X otherwise" is a third, honest row. (b) *The control for the
  source of the weakness.* "AM loses on k-row" can mean weak coordinates
  or a weakness of the transportation assignment itself. The control is
  AM seeded by something other than `gpl`: the min-cut placement X itself
  (`-feedback_passes 2 -feedback_assignment transport_only`, which is
  exactly "run X, then American-map X's coordinates through the same
  tree"), Capo's placement, and `gpl`'s *final* placement rather than its
  early stop. Prediction: AM(X seed) ≈ X on every set (the transport is
  faithful to good coordinates; E11 saw this on three designs), AM(gpl
  seed) < X on square dies and > X on k-row, AM(Capo seed) between. The
  weakness is then located in the coordinates. *Falsified if* AM beats X
  on the k-row set under the full-set accounting, or if AM(X seed) is
  worse than X by > 1 % — the latter would indict the assignment (its
  rounding/repair, or the whitespace following the seed instead of the
  capacity policy), not the seed.
* **H-AM6 (the transport is not the runtime).** With the LEMON network
  simplex the transport is < 10 % of `t_place` at every design size up to
  3 M cells, so Vygen's linear-time algorithm is a citation, not an
  implementation. *Falsified if* the transport share exceeds 25 % on the
  largest designs, in which case AM-4 with the specialized routine becomes
  worth building.

The paper claim, if H-AM1/2/5 hold: the 2D placement-quality gap of
top-down min-cut is not optimizer strength (engine, objective, exactness —
E20a, E21e, E5/E6) but *information at decision time*: cut-minimizing
assignments contradict geometric information that an analytical placement
already has, mostly in the middle of the tree; preserving that geometry
with the same discrete back end closes the gap, and where the analytical
placer is weak the ranking inverts.

## 5. Study AM-1: connectivity versus geometry in one tree

Designs (six, chosen 2026-10-30 from the x3_mega results so that every
hypothesis has at least one clean test; x3_mega `cutnet_tp` / `gpl`
post-DPO in parentheses):

| # | floorplan | cells | why |
|---|---|---|---|
| 1 | `aes_ar1_nangate45` | 20 k | square, 60 %, no macros; X 1.15. The design on which the American-map commit first beat `gpl` (E18–E20b, on the ORFS floorplan); the H-AM1/H-AM2 reference |
| 2 | `ibex_ar1_asap7` | 20 k | square, second PDK; X 1.19. The dense-regime design of E19–E21 (AM reached parity, not a win); pairs with #5 |
| 3 | `jpeg_ar1_nangate45` | 65 k | the largest macro-free square floorplan; X 1.26, the worst macro-free base case; pairs with #6 |
| 4 | `swerv_wrapper_ar1_nangate45` | 100 k | fixed macros with soft-blockage halos; X 1.23; the case where hybrid B failed (E24, +14–20 %). Tests the transport under obstacles and keeps the macro regime in the study |
| 5 | `ibex_u70_r8_asap7` | 20 k | k-row, 8 rows, 70 %; X **0.80** — the same netlist and PDK as #2 with the ranking inverted. The cleanest H-AM5 pair: connectivity identical, only the geometry differs |
| 6 | `jpeg_u70_r4_nangate45` | 65 k | k-row, 4 rows, 70 %; X **0.62**, routes (r1/r2 jpeg crash FastRoute); same netlist and PDK as #3. Second H-AM5 pair, and the floorplan with the most to lose from a `gpl` seed |

Three seeds on all six (the runs are 1–9 min each, so the full arm list
with 3 seeds is ≈ 400 runs, under a machine-day at −j 6). Not chosen, and
why: gcd (347 cells; no tree to speak of, E19 negative for lack of
structure), cva6 / mempool_tile (macro designs like #4 but 3–5× the
runtime; add one later if #4 is interesting), xiangshan / megaboom /
mempool_group (hours per run; the scale-up comes after the mechanism is
established), the k-row floorplans where `gpl` fails (ibex r4 asap7,
jpeg r4: `DPL-0033` with the negotiation legalizer; aes/ibex u70 r1 asap7:
GPL-0305 divergence) — they belong to the failure accounting of the full
k-row set, not to a six-design mechanism study; `ibex_u70_r4_asap7` can
be added as the seventh, "seed-failure" floorplan once the legalizer
question (§9) is settled.

Binary: `OpenROAD/build/bin/openroad` — the x3 source plus the
options of §7, built incrementally in `build-kway` on host-B and snapshotted;
the running `snap/openroad-x3` (x3_mega) and `build-e23/bin/openroad`
(e23) are untouched. Defaults reproduce x3.

**Host.** AM-1 runs on host-B only (`exp/am1_launch.sh` refuses to start on
host-A); host-A keeps x3_mega and e23. Launched 2026-10-30 14:50, −j 8,
waves core → levels → extra; log `results/am1.log`, results
`results/am1/`, analysis `exp/american_maps.py results/am1`.

Common `mcp` string (the x3_mega one, k-way refinement off so that the
assignment is the only thing that changes between arms):

```
MCP="-k 2 -reserve minlocal -density \$::global_place_density -max_tolerance 1.0
     -pad_left \$::global_place_pad -pad_right \$::global_place_pad
     -optimal_cells 12 -feedback_passes 1 [-soft_blockages block on base]"
AM="$MCP -seed_placement -seed_terminals -assignment transport_only"
```

Seed τ: 0.3 (E19's robust choice); 0.5 as a second arm on the base
designs only (sparse designs gain, dense ones lose — an adaptive τ is a
finding of this study, not an input).

| arm | variant string | what it tests |
|---|---|---|
| `gpl` | `gpl\|gpl\|krow` | baseline |
| `X` | `$MCP` (= x3_mega `cutnet_tp`; reuse) | min-cut from scratch |
| `X_kway` | `$MCP -kway_refine_levels {2 3 4 5}` (= x3_mega `kway_tp`; reuse) | best from-scratch arm |
| `AM2_t30` | `$AM\|none\|gpl_ovf0.3` | American map, bisection |
| `AM2_t50`, `AM2_t70` | `$AM\|none\|gpl_ovf0.5`, `…0.7` | τ sensitivity: cruder seeds |
| `seed50`, `seed70` | `gpl -overflow 0.5 / 0.7` + dpl + DPO alone | the matching seed controls: `AM2_t<τ> − seed<τ>` is the transport's own contribution at each seed quality |
| `AM4_t30` | `$AM -k 4\|none\|gpl_ovf0.3` | quadrisection (H-AM3) |
| `AMl_t30` | `$AM -assignment_net_weight 0.5\|none\|gpl_ovf0.3` | net-aware transport (E20b's best) |
| `AM2_L2`, `_L4`, `_L6` | `$AM -seed_levels L\|none\|gpl_ovf0.3` | AM above level L, min-cut below (H-AM1) |
| `AM2_not1` … `_not6` | `$AM -am_mincut_levels {L}\|none\|gpl_ovf0.3` | AM everywhere except level L, where min-cut decides: the *consequence* of one level's disagreement (H-AM2) |
| `MC_diag` | `$AM -am_diagnose -am_commit mincut\|none\|gpl_ovf0.3` | min-cut decides at every level (seed as terminals, planned balance), American map computed and compared: the disagreement along the min-cut tree |
| `seed` | `gpl -overflow 0.3` + dpl + DPO alone | what the seed is worth without the tree (note: the `krow` gpl baseline stops at overflow 0.2, so τ = 0.3 is nearly the finished placement) |
| **seed controls (H-AM5b)** | | |
| `AMx` | `$MCP -feedback_passes 2 -feedback_assignment transport_only -feedback_weight 0` | AM seeded by X's own result (pass 2 of the feedback loop); the faithfulness control |
| `AMc` | `$AM\|none\|capo` | AM seeded by Capo (needs `MCP_PRE=capo`, §7) |
| `AMg` | `$AM\|none\|gpl` | AM seeded by the *final* `gpl` placement |

With six designs and three seeds the whole list fits in one grid: 20
arms × 6 floorplans × 3 seeds ≈ 360 runs (`X` / `X_kway` / `gpl` seed 1
already exist in x3_mega; rerun for seeds 2–3). Order inside the grid:
`gpl`, `X`, `X_kway`, `AM2_t30`, `MC_diag`, `AMx`, `seed` first (the H-AM1/H-AM5 core),
then `AM2_not1…6` and `AM2_L2/4/6` (H-AM2), then `AM4`, `AMl`, `AMg`,
`AMc`, `AM2_t50/t70`, `seed50/70`. The tree on these designs has 8–12 levels; `_not*` is
run for levels 1–6 (below that the regions are end cases on the 20 k-cell
designs).

**Failure accounting (H-AM5a).** `exp/american_maps.py` reports for every
set and arm: runs attempted / placed / legal / routed, with the failure
class (`gpl` divergence GPL-0305, legalizer DPL-0033, OOM, router crash).
Every geomean comes in three views: common successful subset; full set
with a failed arm imputed at the worst successful arm's ratio on that
floorplan; and the fallback policy row (AM if the seed exists, else X).
`exp/compare.py`'s current behaviour — geomean over the floorplans where
all arms succeeded — is the silent exclusion to avoid; the script prints
the excluded floorplans by name.

Metrics (all from the existing harness): post-DPO HPWL and routed WL
relative to `gpl`, pre-DPO HPWL (the side-of-cut diagnostic), `t_place`
and the transport share (§7), per-level `assignment_dhpwl_um`,
`assignment_split`, `assignment_repairs`, `seed_outside`, legality
(`check_placement`, dpl displacement).

## 6. Study AM-2: the BonnPlace loop

Same designs (base set first), arms on top of `AM2_t30`:

| arm | variant string | what it tests |
|---|---|---|
| `AMo_a1` | `$AM -oracle qp -oracle_anchor 1 -oracle_min_level 1\|none\|gpl_ovf0.3` | positions re-solved per level, then American map (H-AM4) |
| `AMo_a03`, `AMo_a3` | anchor 0.3 / 3 | anchor stiffness (E21b: 0.3–3 was the useful range) |
| `AMo4_a1` | `AMo_a1 -k 4` | quadrisection in the loop |
| `AMo_nt` | `AMo_a1 -no_oracle_terminals` | control: oracle does *not* overwrite `loc_` → identical to `AM2_t30` if the plumbing is as read; a cheap consistency check |

Expected outcome, honestly: the QP oracle is weak (E4, E21, E24). If
H-AM4 fails for that reason, the conclusion is "the loop needs a density-
aware oracle" (Track C of `HYBRID_PLACEMENT_PLAN.md` §9.3), and AM-2
stays a one-table negative result. It is still worth running because it
is the one configuration of the BonnPlace methodology we can test without
new code.

## 7. Code: the disagreement diagnostic and a transport timer

The only new code in AM-1. In `Decomposer::runJob`, under a new option
`-am_diagnose` (bool, `Options::am_diagnose`), when the split is seeded
and `assignment != kNone`:

1. The transport assignment is computed as today (`result.part`).
2. The min-cut partition is computed from the *planned* balance (not from
   the assignment-widened bounds) — i.e. the from-scratch path, so the two
   are decided independently from the same region and plan. Which one is
   committed is a second flag, `-am_commit mincut|transport`
   (`MC_diag` commits min-cut; `AM2_t30` commits the transport and is the
   transport-only arm with the diagnostic alongside). Implemented as
   `-am_diagnose` / `-am_commit mincut|transport` in `Decomposer::runJob`
   (`amCompare`).
3. Per region, record in the level stats: cell area and count on which the
   two partitions differ (after aligning child labels by maximum
   overlap), the surrogate HPWL of both partitions with the region's local
   pins (`builder.assignmentDeltaHpwl` exists for this), the transport's
   displacement cost of the min-cut partition (how far min-cut moves cells
   from the seed), and wall time of `solveTransport`. Sum per level into
   `.mcp.json`: `am_disagree_area_frac`, `am_disagree_cells`,
   `am_dhpwl_mincut_um`, `am_dhpwl_transport_um`, `am_displacement_mincut`,
   `transport_seconds`.
4. `transport_seconds` is recorded in every seeded run regardless of the
   flag (H-AM6).
5. `-am_mincut_levels {L ...}`: in a seeded run, the listed levels are
   decided by min-cut from scratch (the planned balance, no assignment),
   all other levels by the American map; the seed-located terminals stay
   in force everywhere. One line in `Decomposer::seededAt` plus the option
   plumbing. This is the `_not*` arm of H-AM2; `-seed_levels L` is the
   complementary "all levels ≥ L".
6. Per-cell disagreement follow-through (cheap, same flag): for the
   disagreeing cells of each level, the final displacement between the
   `MC_diag` and `AM2_t30` placements is computed offline by
   `exp/american_maps.py` from the two `.db`s on the 3-seed subset (keep
   those `.db`s; the harness deletes them by default) — whether the cells
   min-cut moved across the line stay there or are pulled back by the
   lower levels and the end game.
7. `MCP_PRE=capo` in `bench/mincut.tcl`: run the Capo bridge of
   `bench/capo.tcl` (export, `CapoTest1.exe`, import of the `.pl`) as the
   seed step; needs the bridge's procs moved into `bench/common.tcl` so
   both scripts share them. Capo is `/path/to/UMpack/Capo/CapoTest1.exe`
   (live for the E23 arm; do not touch while e23_base runs).

Roughly 120 lines in `Decomposer.cpp` / `Decomposer.h` / `MinCutPlacer.cpp`
(report) / `mcp.tcl`, plus the Tcl for the Capo seed; no change to any
existing arm's behaviour when the flags are off. Consistency checks before
the grid: `AMo_nt` ≡ `AM2_t30`; `AM2_t30 -am_diagnose` ≡
`AM2_t30`; `-am_mincut_levels {}` ≡ `AM2_t30`; `-am_mincut_levels {0 1 2
…}` ≡ `X` with seed terminals — all to the micron. `AMx`'s pass-2 HPWL is
in `feedback_hpwl_um` even when pass 1 is kept as the best.

Script: `exp/american_maps.py results/am1 [--gpl ...]` — the §8 tables with
the three failure-accounting views, the per-level disagreement and
consequence curves and their rank correlation, `exp/seedlevels.py`'s
marginal-loss attribution on the `_L*` arms, and the per-cell
follow-through of item 6.

## 8. Tables and figures for the paper

* **Table AM-a** (main): post-DPO HPWL / `gpl`, routed WL / `gpl`,
  `t_place` / `gpl` for `X`, `X_kway`, `AM2`, `AM4`, `AMl`, `AMo`, `seed`
  — geomean over the 20 base designs, over the k-row set, and per design
  for the base set (appendix). Today's three-design row: X 1.07–1.22,
  AM2 0.92–1.02.
* **Figure AM-b** (mechanism, two panels): left, per level, the
  disagreement area fraction and the surrogate ΔHPWL between the min-cut
  and American-map partitions (`AM2_t30`'s diagnostic), one curve per utilization
  class; right, the consequence — post-DPO HPWL of `AM2_notL` / `AM2` per
  level — with the cumulative view from `_L*`. Plus a scatter of
  consequence against surrogate ΔHPWL over all (design, level) points with
  the rank correlation: the figure that shows disagreement *predicts*
  loss, not merely that it exists.
* **Table AM-c** (converse): the same arms on the k-row set by rows and
  utilization, with the failure counts per arm and the three accounting
  views of §5 — where the ranking inverts and by how much, and whether
  the inversion survives counting the `gpl` failures.
* **Table AM-c′** (source of the weakness): `X`, `AMx`, `AMg`, `AMc`,
  `AM2` on both sets. Reads as: how much of AM's result is the seed, how
  much the assignment.
* **Table AM-d** (runtime): transport seconds vs cells per level and in
  total, share of `t_place`, split/repair counts; the sentence about the
  linear-time algorithm.
* The ICCAD04 table is dropped.

## 9. Caveats to state in the paper

* AM needs a seed and inherits its failures: on the k-row set `gpl`
  diverges or is not legalizable on ~7 % of the floorplans (E17: 11/180;
  x3_mega: 13 FAIL of 360 so far). A divergent seed (GPL-0305) means no AM
  result; an un-legalizable `gpl` *result* does not affect the early-stop
  seed. Both are counted, never dropped (§5 failure accounting); the
  fallback row makes the practical comparison explicit.
* The seed controls separate "weak coordinates" from "weak assignment".
  Without `AMx` ≈ X, an AM loss on the k-row set could be blamed on the
  transport (rounding and repair of the ≤ k−1 split cells, the whitespace
  following the seed instead of `minlocal`); with it, the loss is the
  seed's.
* The legalizer: dpl's default negotiation legalizer did not converge on
  some `gpl` k-row results (x3_mega) and on the ICCAD04 designs;
  `MCP_DPL_DIAMOND=1` in `bench/common.tcl` switches every arm to the
  diamond legalizer. Decide once before AM-1 and use it for all arms.
* Transport respects usable area, so macros are fine for the assignment;
  the QP oracle (AM-2) is not macro-aware (E24) — expected to fail on
  swerv / cva6 / megaboom, which is a finding about the oracle, not about
  American maps.
* All prior hybrid evidence is gcd/aes/ibex, 3 seeds; AM-1 is the
  scale-up. Seed variance of `mcp` on these designs was ±1 % (E4); of
  `gpl -overflow τ` seeds, unmeasured — the 3-seed subset covers it.
* `-seed_levels` counts levels from the root; with k = 4 a level is a
  quadrisection, so `_L*` arms are only comparable within one k.

## 10. Order

1. (now, no machine load) the §7 diagnostic, `-am_mincut_levels`, the
   transport timer and `MCP_PRE=capo` into `build-kway/snap/openroad-am`;
   smoke on gcd with the consistency checks of §7. DONE 2026-10-30:
   `transport_only` ≡ `-am_diagnose` ≡ `-am_mincut_levels {}` (2688.9 µm);
   `-am_mincut_levels {0..15}` ≡ `-seed_levels 0` (2714.9); `AMx` pass 2
   3074.1 vs pass 1 3085.4 (−0.4 %: the transport is faithful to X's
   coordinates on gcd); `-k 4` seeded runs. Capo seed: the Capo binary
   has to be built on host-B (host-A's `-march=native` build dies with an
   illegal instruction there).
2. `exp/am1.cfg` + launcher (base small → k-row subset → base big), ready
   to start when `x3_mega` frees the machine; the core arms of §5 first on
   every design (`AM2_t30`, `MC_diag`, `AMx`, `seed`, then `AM2_not*`),
   the second wave after.
3. `exp/american_maps.py` with the failure accounting first (it is also
   what the x3_mega summary needs); Figure AM-b from the first complete
   design set.
4. `exp/am2.cfg` (BonnPlace loop) on the base set.
5. Only if H-AM3 holds and H-AM6 fails: Vygen's linear-time quadrisection
   in `Assignment.cpp` as an alternative solver, timed against LEMON.

## 11. Interim results (2026-10-30 16:30, host-B; aes / ibex / jpeg squares complete, swerv_wrapper and the k-row pair in flight)

Preliminary — three of six designs, core wave plus `AM2_not*` / `AM2_L*`.
Numbers are post-DPO HPWL / `gpl`, mean of three seeds; routed WL in
parentheses.

| design | `seed` (gpl τ 0.3 + dpl) | `AM2_t30` | `MC_diag` | `X` | `X_kway` |
|---|---|---|---|---|---|
| aes_ar1_nangate45 | 1.023 (1.010) | 1.038 (1.022) | 1.138 (1.088) | 1.166 (1.105) | 1.155 (1.102) |
| ibex_ar1_asap7 | 1.063 (1.052) | 1.062 (1.049) | 1.160 (1.099) | 1.201 (1.148) | 1.184 (1.128) |
| jpeg_ar1_nangate45 | 1.057 (1.044) | 1.077 (1.061) | 1.278 (1.182) | 1.254 (1.167) | 1.247 (1.163) |

* **H-AM1 / H-AM5 on squares:** the American map closes most of the gap
  (1.06 vs 1.21 geomean), but `seed` alone — the same gpl stopped at
  overflow 0.3 and legalized — is as good or better (1.047). On these
  designs the transport adds nothing to the seed; what it preserves is
  the seed, and the `krow` baseline stops at overflow 0.2, so τ 0.3 is
  nearly the finished gpl placement. Hence the `AM2_t50/t70` + `seed50/70`
  sweep added to the extra wave: the question is whether the transport
  ever beats "legalize the seed" and at which seed quality.
* **`MC_diag`** (min-cut with the seed as terminals, no refinement) is
  between `X` and `AM` on aes/ibex and *worse* than `X` on jpeg: fixing the
  seed as terminals helps the cut objective on small designs and hurts on
  the large one; the seed's value is in the assignment, not in the
  terminal propagation.
* **`AMx`** (American map of X's own pass 1): pass 2 / pass 1 = 1.008
  (aes), 1.007 (ibex), 1.031 (jpeg). The transport reproduces X's own
  geometry to within 1–3 %, so the AM-vs-X gap is seed coordinates, not
  the transport (H-AM5b, X-seed control); the DB keeps pass 1, so `AMx`
  `final` ≡ `X`.
* **H-AM2 (consequence of disagreement):** deciding one level by min-cut
  inside the AM tree always costs; `AM2_notL / AM2_t30` = 1.01–1.19, worst
  at L = 1–2 (2–4 regions: aes 1.063, ibex 1.137, jpeg 1.194 at L 2) and
  1–2 % at L ≥ 5. Pooled over 18 (design, level) points: Spearman with the
  disagreement fraction 0.60 (per design 0.54 / 0.89 / 1.00), with the
  area-weighted displacement of the min-cut partition from the seed 0.83
  (0.83 / 0.94 / 0.77), with the surrogate HPWL ratio mc/am **−0.83**: the
  min-cut partition has the *lower* surrogate HPWL at every level, and the
  more it wins there the worse the final placement. The cut objective and
  the final HPWL disagree about the top of the tree; the predictor that
  works is how far the min-cut split moves area away from the seed.
  Gate of §4 (ρ ≥ 0.6) met on the fraction, clearly on the displacement.
* Transport time share of t_place: 0.1–1.7 % on all seeded arms (H-AM6
  holds; LEMON is not the bottleneck).
* No failures so far (0 FAIL, 0 PART over 154 finished runs).

### 11.1 Core wave complete (18:05): k-row and macro floorplans

| design | `seed` | `AM2_t30` | `MC_diag` | `X` | `X_kway` |
|---|---|---|---|---|---|
| ibex_u70_r8_asap7 | FAIL ×3 (DPL-0033) | 1.044 (1.171) | 0.795 (0.786) | 0.794 (0.785) | 0.795 (0.786) |
| jpeg_u70_r4_nangate45 | FAIL ×3 (DPL-0033) | 1.040 (1.067) | 0.609 (0.783) | 0.610 (0.783) | 0.607 (0.780) |
| swerv_wrapper_ar1_nangate45 | 1.029 (0.942) | **1.468** (1.561) | 1.203 (1.428) | 1.237 (1.394) | 1.220 (1.416) |

* **H-AM5 confirmed on k-row:** the American map of the gpl seed is
  *worse than gpl* (1.04) where min-cut from scratch is 20–40 % better
  (0.79 / 0.61). The seed carries gpl's narrow-row weakness into the
  tree and the transport preserves it faithfully (`AMx` pass 2 / pass 1
  = 1.001 / 1.000 there). Geometry inherited from a weak analytical
  placement is a liability, not an asset, on these floorplans; the
  min-cut tree's advantage is that it never consults it.
* **H-AM5a (failure accounting):** the `seed` control (gpl τ 0.3 + dpl's
  negotiation legalizer) fails `check_placement` on both k-row designs,
  all seeds (DPL-0033, 2–37 overlapping cells left): the early-stopped
  gpl is not even legalizable there with the default legalizer. Kept as
  FAILs in the accounting; `exp/am1_seed_diamond.cfg` adds the same arms
  with dpl's diamond legalizer (`seed_dia`, `seed50_dia`, `seed70_dia`)
  so a "seed alone" number exists for every design.
* **Macro floorplan (swerv_wrapper): the American map breaks.** 1.47 ×
  gpl against 1.24 for X and 1.03 for the seed alone, and `AMx` — the
  transport applied to X's *own* legal placement — ends 2.17 × worse than
  its pass 1 (0.7–3 % on the other five designs). Per-level stats of
  `AM2_t30`: `seed_outside` 12 011 of 79 990 cells (15 %) already at the
  root (seed over blocked area, mean 41 µm away), 34 % outside their
  child after level 1, 57 % after level 2, 83 % by level 5; area-weighted
  displacement from the seed 6 µm (L1) → 23 (L2) → 50 (L4) → 100+ (L9),
  against ≤ 1 µm on the macro-free designs; `assignment_repairs` 5 379
  at level 4 and 11 256 at level 5 with `assignment_infeasible` children.
  Mechanism: the transport's cost is the distance to the child *box* and
  its capacity is the child's usable area; a cell seeded over a macro or
  halo is "inside" the box that contains the blockage, is charged to that
  child's small capacity, and gets pushed out only when the tree reaches
  the blockage scale — by then wherever capacity is left, i.e. far away.
  The gpl seed has cells inside the 28 macro halos (gpl / dpl ignore soft
  blockages; mcp blocks them in the x3 conditions), so part of this is
  the blockage *policy* and part is the macros proper;
  `exp/am1_swerv_sbi.cfg` (`AM2_t30_sbi`, `X_sbi`: halos ignored) splits
  the two. Either way H-AM1's "the American map is a free win on the
  base set" does not survive the macro design: a blockage-aware transport
  cost (distance to the child's *free* area, not its box) is the missing
  piece if the hybrid is to be used there.
* Geomean over the six designs (post-DPO HPWL / gpl, FAILs imputed at
  the worst successful arm): `AM2_t30` 1.112, `X` 1.008, `X_kway` 1.000,
  `MC_diag` 0.997, `seed` 1.042 (two designs imputed). The fallback
  policy "AM when a seed exists, else X" is 1.112 — worse than X alone,
  because the seed exists everywhere and is wrong on half the set. A
  usable policy must *decide* per floorplan (row count, blockage
  fraction), which is exactly the k-row / macro diagnosis of §4 H-AM5.
* (19:50) `seed*_dia` (diamond legalizer) on the k-row pair: 15 of 18 fail
  as well (DPL-0036, the diamond legalizer gives up outright); only
  ibex_u70_r8 at τ 0.3 legalizes, at 0.978 × gpl HPWL (routed 1.028) —
  the AM of the same seed is 1.044. So on narrow-row floorplans the
  early-stopped gpl is not a placement at all for either dpl legalizer,
  and the American map's one real service there is to *make it legal*,
  at gpl's wirelength, 25–40 % behind min-cut from scratch. H-AM5a: the
  failures are reported, not dropped; imputation at the worst successful
  arm is the policy in `exp/american_maps.py`.
* (20:50) `AM2_t30_sbi` (macro halos ignored, as gpl sees them): 1.509 ×
  gpl — no better than 1.468 with the halos blocked. The halo policy is
  not the cause. With the halos ignored the seed still has 10 248 cells
  (13 %) over the 28 macros at the root (31 µm from usable area), and the
  cascade is the same: 25 790 outside their child after level 1, 43 556
  after level 2, `assignment_repairs` 4 395 / 14 957 / 21 802 at levels
  3 / 4 / 6. On swerv the `AM2_not*` arms are *better* than `AM2_t30`
  (not4 1.359, not3 1.313): the sign of H-AM2 flips where the seed is
  incompatible with the blockages — each min-cut level repairs some of
  the damage. Diagnosis for §7: the transport sees the child *box*
  (`boxDistance(plan.boxes[q], loc)`) and the child *capacity*
  (`usable × density`), so a cell seeded over a macro costs 0 in the
  child that contains the macro and is only evicted when a region is all
  blockage; the eviction then goes to whatever child has capacity, at
  macro scale (tens to 100+ µm). The seeded path needs a blockage-aware
  cost (distance to the child's free area) or a pre-pass that projects
  seeds off blockages before level 0; without it the American map is
  not usable on macro floorplans, and the x3 min-cut tree — which never
  consults the seed — is the right default there.
* (20:50) `AM2_t30_sbi` (macro halos ignored, as gpl sees them): 1.509 ×
  gpl — no better than 1.468 with the halos blocked. The halo policy is
  not the cause. With the halos ignored the seed still has 10 248 cells
  (13 %) over the 28 macros at the root (31 µm from usable area), and the
  cascade is the same: 25 790 outside their child after level 1, 43 556
  after level 2, `assignment_repairs` 4 395 / 14 957 / 21 802 at levels
  3 / 4 / 6. On swerv the `AM2_not*` arms are *better* than `AM2_t30`
  (not4 1.359, not3 1.313): the sign of H-AM2 flips where the seed is
  incompatible with the blockages — each min-cut level repairs some of
  the damage. Diagnosis for §7: the transport sees the child *box*
  (`boxDistance(plan.boxes[q], loc)`) and the child *capacity*
  (`usable × density`), so a cell seeded over a macro costs 0 in the
  child that contains the macro and is only evicted when a region is all
  blockage; the eviction then goes to whatever child has capacity, at
  macro scale (tens to 100+ µm). The seeded path needs a blockage-aware
  cost (distance to the child's free area) or a pre-pass that projects
  seeds off blockages before level 0; without it the American map is
  not usable on macro floorplans, and the x3 min-cut tree — which never
  consults the seed — is the right default there.
