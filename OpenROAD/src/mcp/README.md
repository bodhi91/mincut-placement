# Min-Cut Placement

The min-cut placer (`mcp`) is a top-down global placer. The core area is
recursively decomposed into regions; at every level the cells of a region are
partitioned into two or four children with a hypergraph partitioner
(TritonPart by default), and the children become the regions of the next
level. Recursion stops at small regions, where cells are placed at their region
and written back with `PLACED` status. The end game follows Capo: regions of a
few cells are partitioned exactly, small leaves are placed optimally for HPWL
by branch and bound, and a window slides along every row afterwards
reordering its cells optimally ("row ironing"). Run `detailed_placement`
afterwards to legalize.

## Commands

```{note}
- Parameters in square brackets `[-param param]` are optional.
- Parameters without square brackets `-param2 param2` are required.
```

### Min-Cut Placement

```tcl
mincut_placement
    [-k k]
    [-objective cutnet|conn|hpwl|phpwl|rsmt]
    [-fine_objective cutnet|conn|hpwl|phpwl|rsmt]
    [-fine_level level]
    [-net_weight uniform|exphpwl|invdeg]
    [-net_weights_file file]
    [-balance_objective bounds|ratio|ncut]
    [-balance_fractions fractions]
    [-balance_sweep_tolerance tolerance]
    [-balance_levels levels]
    [-cut_direction longer|sparsest]
    [-engine fm|tritonpart|hmetis|mtkahypar|fastpart]
    [-top_engine engine]
    [-top_levels levels]
    [-density density]
    [-balance_tolerance tolerance]
    [-reserve uniform|minlocal|congestion|mixed]
    [-max_tolerance tolerance]
    [-terminals none|propagate]
    [-terminal_band fraction]
    [-waves waves]
    [-end_case_cells cells]
    [-exact_cells cells]
    [-optimal_cells cells]
    [-optimal_rows 0|1]
    [-iron_window cells]
    [-iron_passes passes]
    [-end_case_passes passes]
    [-leaf_positions even|optimal|joint]
    [-leaf_pad sites]
    [-max_fanout fanout]
    [-pad_left sites]
    [-pad_right sites]
    [-soft_blockages ignore|block]
    [-seed_placement]
    [-assignment none|transport|transport_only]
    [-displacement_weight weight]
    [-seed_levels levels]
    [-seed_terminals]
    [-feedback_passes passes]
    [-feedback_weight weight]
    [-feedback_assignment none|transport|transport_only]
    [-max_moves moves]
    [-refiner_iters passes]
    [-threads threads]
    [-seed seed]
    [-kway_refine_levels levels]
    [-kway_construct_levels levels]
    [-sweep_levels levels]
    [-sweep_passes passes]
    [-sweep_slack slack]
    [-sweep_multilevel]
    [-sweep_cold]
    [-sweep_audit]
    [-kway_slack slack]
    [-kway_vcycles cycles]
    [-report file]
    [-regions file]
    [-hypergraph file]
    [-hmetis_path path]
    [-mtkahypar_path path]
    [-fastpart_path path]
    [-scratch_dir dir]
```

#### Options

| Switch Name | Description |
| ----- | ----- |
| `-k` | Number of children per region: `2`, `4`, or `0` for automatic (4 when the region aspect ratio is in [1/2, 2] and both directions can be cut, otherwise 2). A split never creates a child narrower than the widest cell or shorter than the tallest cell of the region. Default 2 (in E3 quadrisection trailed bisection by 7–15 % HPWL even with the `hpwl` vectors; use k = 4 only with a vector objective, which is what makes quadrisection competitive at all). |
| `-objective` | Net cost model. `cutnet` (default): a net costs its weight when it spans more than one child; external pins become terminal vertices (propagated to the nearest child). The other objectives give every net a cost vector over the 2^k − 1 occupancy masks of the children, evaluated with the net's external pins at their current locations (fixed cells and pins exactly, movable cells at their region center): `conn` = number of distinct children touched (external pins counted at their nearest child) − 1; `hpwl` = half-perimeter of the bounding box of the occupied child centers and the external pins; `rsmt` = FLUTE Steiner length over the same points (HPWL when there are more than 9 points). Costs are in row heights times the net weight. Vector objectives need the `fm` or `tritonpart` engine (TritonPart's k-way FM is replaced by `par::NetVectorRefine`); with another engine the `fm` engine is used for those levels. |
| `-fine_objective`, `-fine_level` | Switch to `fine_objective` from level `fine_level` on (default 1 when only `-fine_objective` is given), e.g. `-objective hpwl -fine_objective rsmt -fine_level 4`. |
| `-objective phpwl` | Probabilistic HPWL (E25): like `hpwl`, but a local pin is not at its child's center; it is uniformly distributed over the union of the occupied child boxes, and the vector entry is the *expected* half-perimeter (expected extreme of d uniform points over a span L lies L/(d + 1) inside each end), combined with the external pins at their locations. An uncut net is not free (its expected span inside its child counts), and cutting a d-pin net costs (L/2)(d − 1)/(d + 1) more than keeping it, so the penalty grows with the degree and the geometry instead of being the flat weight of `cutnet`. |
| `-net_weight` | Degree-dependent net weights for every objective and engine (E25). `uniform` (default): 1. `exphpwl`: the expected HPWL gain of *not* cutting a d-pin net under a uniform prior, normalized to 1 at d = 2 (rises to 1.5 for large d). `invdeg`: 1/(d − 1), a cut net costs its spanning-tree share, as in the connectivity-weighted graph models. External engines receive the weights scaled to integers (100 at the smallest weight). |
| `-net_weights_file` | Per-net weight factors, one `net_name weight` per line (`#` comments), multiplied onto the rule weights; nets not listed keep theirs. `bench/common.tcl` `mcp_timing_weights` writes STA criticality weights 1 + 4 (T − slack)/(T − WNS) (E25 arm 6). |
| `-balance_objective` | What decides the balance of a bisection, i.e. where the cut line goes (E25). `bounds` (default): the capacity bounds from `-density` / `-balance_tolerance` / `-max_tolerance`. `ratio`: the region is partitioned at every fraction of `-balance_fractions` (default 0.3 0.4 0.5 0.6 0.7, tolerance `-balance_sweep_tolerance`, default 0.05) and the split with the lowest ratio cut, cost / (a (1 − a)), is kept; `ncut`: lowest normalized cut, cost (1/vol₀ + 1/vol₁) with vol the weighted pin volume of each part. The exact search of `-exact_cells` runs afterwards on the chosen bounds. `-balance_levels L` restricts the sweep to the first L levels (default −1: all). Bisection only; seeded or quadrisected regions use the bounds. |
| `-cut_direction` | `longer` (default): a region is cut across its longer side. `sparsest` (E25): bisections are partitioned in both directions and the one with the fewer cut nets per unit length of the cut line (the inessential nets, which cross the line whatever the partition, included) is kept — a geometric sparsest cut, which trades a longer cut line for a smaller cut. Roughly doubles the partitioning time. |
| `-engine` | Partitioner. `tritonpart` (default): TritonPart multilevel. `fm`: TritonPart with coarsening disabled (initial partitioning plus k-way FM on the flat hypergraph). `hmetis`, `mtkahypar`: external binaries driven through hMETIS-format files; they honor the upper capacity bounds only. When an engine fails the `fm` engine is used for that region. |
| `-top_engine`, `-top_levels` | Use a different engine for the first `levels` levels. |
| `-balance_tolerance` | Two-sided tolerance on each child's proportional share of cell area. Default 0.05. |
| `-density` | Target utilization per region, of padded cells when `-pad_left`/`-pad_right` are given. With `-reserve uniform` a child may take up to `density` of its planned free area even when that exceeds its proportional share (the lower bound still applies); with the adaptive policies it is the density the cells are packed to, and only the whitespace above it is moved. Default: derived from total cell area and usable area (uniform whitespace). Use the value you would give `global_placement -density`. |
| `-reserve` | How a region's whitespace is divided between its children when the cut line is shifted after partitioning. `uniform`: every child gets free area in proportion to its cell area, so all regions keep the core's utilization and the cells are spread over the whole core. `minlocal` (default): every child gets the room for its cells at `-density` and the excess goes to the children in proportion to their distance from the region's *external pull* (the mean location of the pins outside the region over the nets leaving it), so cells gather where their connections are and the whitespace ends up at the periphery; the partitioner's balance tolerance grows with the excess (see `-max_tolerance`). `congestion`: the excess is divided in proportion to a routing-demand surrogate integrated over each child (RUDY from the current locations plus pin density, equally weighted and normalized). `mixed`: the excess follows the mean of the two rules. All adaptive policies need `-density` above the (padded) core utilization; otherwise they behave like `uniform` (warning MCP-0105 when `-reserve` is given without `-density`). E5: with gpl's density and padding, `minlocal` cut HPWL against `uniform` by 14 % on gcd, 51 % on aes and 2 % on ibex (whose padded utilization of 44 % leaves little to move); `congestion` was never better than `uniform`. |
| `-max_tolerance` | `minlocal`/`mixed`: upper limit of the balance tolerance derived from the whitespace above the target density, `(density − utilization) / utilization` clamped to [`-balance_tolerance`, `-max_tolerance`]. 1.0 (default) leaves the split free apart from the density; the cut line follows the result. HPWL improved monotonically up to 1.0 in the M5 sweeps. |
| `-terminals` | Handling of pins outside a region. `propagate` (default): every external pin is projected onto the nearest child and the net gets a fixed terminal vertex there; nets whose terminals already span two children are dropped as inessential. `none`: external pins are ignored. |
| `-terminal_band` | Vector objectives only. A pin of a movable (not yet placed) external cell whose coordinate lies within this fraction of the child extent of a cut line is *ambiguous* along that axis: it does not pull the net, so the net pays the full price for being cut, as in classic terminal propagation. Default 0: every external pin is used at its region center (1/3 would be the middle third of a bisected region; in E3 it cost 2.5–3 % HPWL on aes and ibex). Pins of fixed cells and I/O pins are never ambiguous. |
| `-waves` | The regions of a level are partitioned in this many waves, colored like a checkerboard, and locations are updated between waves, so a region in a later wave sees the children its neighbors chose rather than the neighbors' centers (which lie on its own cut lines). Default 2; 1 partitions a whole level against the previous level's centers. |
| `-end_case_cells` | Regions with at most this many cells are not split further; their cells are packed into the region's rows ordered by the centroid of their connections (by their seed location with `-seed_placement`). Default 12. |
| `-exact_cells` | Regions with at most this many cells are partitioned *exactly*: the engine's result is the incumbent of a branch-and-bound search over all assignments of the region's cells to the children (same objective, capacity bounds and terminals as the engine; the cost of the nets touched so far is the lower bound, since every net cost is monotone in the set of occupied children). The result is never worse than the engine's. A budget of 200 000 search nodes per region bounds the runtime; with k = 2 the default 16 is always searched completely (`exact_incomplete` in the report counts the others). 0 disables. Default 16. |
| `-optimal_cells` | Leaves with at most this many cells are placed by branch and bound after the heuristic layout: first the assignment of cells to rows for the vertical HPWL (a cell's y is fixed by its row; row capacities are respected), then the ordering of the cells within every row for the full HPWL, with the pins outside the leaf fixed and the positions of `-leaf_positions`. Both searches start from the heuristic layout and the cheaper of {heuristic, heuristic rows + optimal orders, optimal rows + optimal orders} is kept. Budget 200 000 nodes per search. 0 disables. Default 12 (= `-end_case_cells`, every leaf; E6: 10 left 1.5–5 % on the table). |
| `-optimal_rows` | The first of the two `-optimal_cells` searches. `0` keeps the heuristic assignment of cells to rows and searches only the order within every row — the single-row end case (E27: what the multi-row search is worth against ordering alone; identical on one-row leaves). Default `1`. |
| `-iron_window`, `-iron_passes` | Row ironing after the end cases: a window of `iron_window` consecutive cells slides along every free row segment (stride half a window) and its cells are reordered optimally for HPWL by the same branch and bound within the window's free space (up to the neighbors outside it), at the positions of `-leaf_positions`; a window is rewritten only when the new order is cheaper than the current positions, so HPWL never increases. Up to `iron_passes` passes, stopping early when a pass changes nothing. Budget 100 000 nodes per window. `-iron_window 0` disables. Defaults 7 and 2. |
| `-end_case_passes` | The leaves are placed one after another, so a leaf is laid out while the cells of the leaves placed after it still sit at their region centers. Passes beyond the first revisit every leaf (before the ironing) with its neighbors at their placed positions: the current layout is the incumbent, the heuristic layout is recomputed and the branch and bound of `-optimal_cells` runs again, and the leaf is rewritten only when the new layout is cheaper for the leaf's nets (which lowers the HPWL by exactly that amount). A pass that rewrites nothing ends the loop. Default 2 (E10: the second pass gains 1.4 / 0.3 / 0.8 % HPWL on gcd / aes / ibex for 15–20 % more placer time; a third gains at most 0.1 % more). 1 disables. |
| `-leaf_positions` | Where the cells of a row go once their order is known, in the optimal end case and in the ironing windows (the heuristic layout always packs them with the row's slack spread evenly). `even`: the same even-slack packing (the rule of E6–E8). `optimal` (default): the order is searched under the packing rule and the cells are then put at the positions that minimize the HPWL for that order — with the order fixed, the leftmost and rightmost cell of every net are known, so the cost is a sum of convex piecewise-linear functions of single cell positions, which the clumping algorithm minimizes exactly under abutment; the result is snapped to sites, cells without connections keep their packed position. `joint`: the order is searched under the optimal positions as well (the lower bound of a prefix is the exact optimum of the relaxed prefix problem), exact up to site snapping; it roughly doubles the placer time on aes (the bound of a prefix is a clumping solve, ten times the cost of a packing-rule node) for 0.1–0.5 % (E9). Under `optimal`/`joint` the cells are placed with the padding of `-leaf_pad` on both sides where a row affords it, so the legalizer has little to do. |
| `-leaf_pad` | Padding (sites per side) the optimal positions keep between cells; set it to the `set_placement_padding` the legalizer will use. `-1` (default) uses `-pad_left`/`-pad_right`; `0` packs the cells against each other. The padding of a row is reduced, in whole sites, to what the row's slack affords. |
| `-max_fanout` | Ignore nets with more pins than this. Default: no limit. |
| `-pad_left`, `-pad_right` | Placement padding in sites added to every movable cell's footprint for the capacity accounting, as in `global_placement -pad_left/-pad_right`. `-density` then refers to padded cells, which is what `detailed_placement` (with `set_placement_padding`) and the router see; use the same values as for gpl. Default 0. |
| `-soft_blockages` | Fixed instances (placed macros, tap cells) and hard placement blockages are always obstacles: their footprint is removed from the free row segments, so cut lines are chosen on *usable* area, the children's capacities exclude it, and the end cases place into the free segments; the pins of fixed instances act as terminals at their real positions. Soft blockages (DEF `PLACEMENT + SOFT`, e.g. the halos the macro placer leaves) are `ignore`d by default, as `detailed_placement` does. `block` makes them obstacles too, which is what `global_placement` does with a soft blockage without `MAXDENSITY` (every site under it is blocked); use it when comparing against gpl on floorplans with macro halos. MCP-0050 reports the blockage counts. |
| `-seed_placement` | Use the current locations of the movable cells as a *seed* (every movable instance must be placed, error MCP-0026; e.g. after `global_placement -skip_nesterov_place` or a full `global_placement`). Every split starts from the geometric assignment of the seed (`-assignment`), the refinement pays for moving a cell away from it (`-displacement_weight`), the cut lines are shifted only within the gap between the two sides' seeds (so a child always contains the seeds of its cells unless the capacities or the refiner moved them across), and the end cases order cells by their seed. The transport bounds are the children's capacities at `-density` (not the partitioner's balance bounds), so the seed decides how the cells are distributed; the balance bounds of the refinement are widened to the assignment. Use an adaptive `-reserve` with `-density` (the default): `uniform` spreads any seed to equal utilization. |
| `-assignment` | With `-seed_placement`: `transport` (default) solves the transportation problem of the cells (weights in sites) to the children (capacities in sites) with the Manhattan distance of a cell's seed to a child's box as cost (network simplex, LEMON); the ≤ k − 1 cells split by the basic optimum are rounded to their largest share and a greedy repair restores the bounds (per-level `assignment_split`, `assignment_repairs`, `assignment_infeasible` in the report). The result is the initial solution of the partitioner (TritonPart skips its multilevel partitioning and runs its V-cycle refinement on it, error PAR-0930 if the sizes disagree). `transport_only` commits the assignment without refinement (pure decomposition of the seed, for diagnosis). `none`: partition from scratch; only the displacement cost sees the seed. |
| `-displacement_weight` | With `-seed_placement`: weight δ of the displacement cost `δ · (a_v / mean area) · d(seed_v, box_q) / unit` a cell v pays in child q, added to the net cost as a vertex vector of the refinement (hence it needs a vector objective; `cutnet` is replaced by `conn`, which is the same cost for k = 2). `d` is the distance to the child's box (0 inside, the distance to the cut line across it). `unit` is a row height for `hpwl`/`rsmt` and the mean distance between the child centers for `conn`, so that δ = 1 makes moving an average cell from one child's center to the other as expensive as one net spanning them. Default 0 (the assignment alone seeds the refinement). |
| `-seed_levels` | With `-seed_placement`: the seed drives only the levels `< levels` of the tree (assignment, clamped cut lines, displacement cost, seed-ordered end cases); from that level on the decomposition continues from scratch with the regular capacity policy. `0` ignores the seed entirely, `-1` (default) uses it at every level. A diagnostic: HPWL as a function of `levels` attributes the loss of the from-scratch decomposition against a seed to the levels of the tree (E8). |
| `-seed_terminals` | With `-seed_placement`: a movable cell that is not yet placed is assumed at its seed clamped into its current region instead of at the region's center — for the terminal propagation and the vector objectives, the external pull of `-reserve minlocal`, the demand map and the neighbors of an end case. E11: on the `gpl` seed it turns the refinement from a loss into a gain on gcd (`transport` δ = 1: 4 342 → 4 144 µm) and is what makes the feedback passes work. Default off (the region center, the classical estimate). |
| `-feedback_passes`, `-feedback_weight`, `-feedback_assignment` | Placement feedback. Passes beyond the first run the whole placer again seeded by the best result so far: `-seed_placement` with `-seed_terminals`, the assignment mode `feedback_assignment` (default `none`: every region is partitioned from scratch, the previous pass enters only through the terminals and the displacement cost; `transport` reproduces the previous tree and gains nothing) and the displacement weight `feedback_weight` (default 0.3; 1 is too stiff, 0.1 oscillates). After every pass the HPWL is compared with the best so far; a pass that does not improve it is discarded (the database is restored) and ends the loop. E11: converges in 2–7 passes to −0.6 % / −3.2 % / −6.9 % HPWL on gcd / aes / ibex (seed 1); each pass costs 2–3 base runs, since the displacement cost routes through the vector refiner. Default 1 (off). |
| `-max_moves`, `-refiner_iters` | Refinement budget of the `fm`/`tritonpart` engines: moves per FM pass and passes per level. Defaults: TritonPart's 50 moves and 2 passes for `cutnet`; for the vector objectives a full pass (one move per vertex), because every vertex is a candidate there and only the k-way FM refiner runs. |
| `-threads` | Number of threads used to partition the regions of a level; results do not depend on it. Default 1. |
| `-seed` | Random seed. Default 0. |
| `-report` | Write netlist statistics, per-level statistics (regions, cut nets, balance violations, runtime; `exact_regions`/`exact_improved`/`exact_incomplete` for the exact partitioner; with `-seed_placement` also the assignment statistics and `seed_outside`/`seed_distance_um`, the cells whose seed lies outside the child they were put in and their mean distance to it), the final HPWL, the leaf utilization (mean and maximum over leaves holding cells, fraction of the usable area they cover), the optimal end case (`optimal_regions`, `optimal_improved`, `optimal_incomplete`, `optimal_nodes`, `optimal_gain_um`), the feedback passes (`end_case_passes`, `refine_regions`, `refine_improved`, `refine_gain_um`), the leaf diagnostics (`leaf_rows_available`, `leaf_rows_used`: rows of the leaves and rows holding a cell, summed over the leaves; `leaf_final_cost_um`: the HPWL of the nets touching each leaf with the leaf's cells at their final positions, summed over the leaves — a net touching several leaves is counted once per leaf; with `-seed_placement` also `leaf_seed_rows_used` and `leaf_seed_cost_um`, the same with the leaf's cells at their seed locations clamped into the leaf, so that the within-leaf quality of the end case can be compared with the seed's), the feedback passes (`feedback_passes` run, `feedback_best_pass`, `feedback_hpwl_um` per pass; the report and the per-level statistics are those of the kept pass) and the row ironing (`ironing_passes`, `ironing_windows`, `ironing_improved`, `ironing_incomplete`, `ironing_nodes`, `ironing_gain_um`, `hpwl_before_ironing_um`, `ironing_seconds`) as JSON. |
| `-kway_refine_levels`, `-kway_slack`, `-kway_vcycles` | Whole-level k-way refinement (X1b). Recursive bisection is irrevocable: once a level is cut, a cell can only move across the cuts below it. After the bisections of a level whose regions are listed in `levels` (`{2 4}`: the 4 and the 16 regions of a bisection tree), the assignment of *all* cells to the regions of that level is refined as one k-way mapping problem with Mt-KaHyPar's Steiner-tree process mapping, `cycles` V-cycles (default 3) started from the tree's own assignment: the regions are the nodes of the target graph, adjacent regions are joined by their center distance, every region keeps the cell area the tree gave it times `slack` (default 1.02 — the whitespace decision stays with `-reserve`), and the I/O pins, fixed cells and the cells of leaves already decided are fixed to their regions. The result is kept only if the placer's HPWL at the region centers improves (the Steiner metric puts a fixed pin at the center of its region, so it can mis-price pin nets; MCP-0058 reports the accepted refinements, `kway_*` in the report every attempt). At most 64 regions per level (Mt-KaHyPar's limit). Needs a build configured with `-DMCP_MTKAHYPAR_DIR=tools/mtkahypar` (`tools/mtkahypar/build.sh`); otherwise warning MCP-0057. X0 (`results/x0_kway/STATUS.md`): −2 % (k = 4) to −4 % (k = 12–16) of the level's HPWL-at-centers on ibex / aes, fading by k = 64; X1b (`results/x1b_kway/STATUS.md`): `{2 3 4 5}` −1.2 % post-DPO HPWL, 12 of 13 paired runs better. Default off. |
| `-kway_construct_levels` | X1a: at these levels the assignment is *constructed* from scratch by the same k-way mapping (Mt-KaHyPar multilevel process mapping into the level's boxes with the tree's loads as budgets, pins fixed) instead of refined from the tree's; the tree supplies geometry and whitespace only. Same acceptance test; a level in both lists is constructed first and refined afterwards. X0: a joint 4-way beat two bisections by 12–14 % of the proxy on ibex and lost 1–2 % on aes, and from scratch beyond k = 4 lost everywhere — meant for `{2}`. Default off. |
| `-sweep_levels` | X2 (hypothesis H1 of the boundary-sweep study): reversible boundary sweeps over the regions of these levels (level of the regions swept, as `-kway_refine_levels`). After the level is committed every pair of adjacent regions is re-bisected as one two-bin problem whose objective is the placer's surrogate itself (the `hpwl` net vectors: local pins at the two region centers, every other pin where it is) under the regions' current loads × `-sweep_slack`, and the result is kept only if the exact HPWL at the centers decreases (`surrogate_strict`). Passes over all pairs (x boundaries, then y) repeat until a pass accepts nothing or `-sweep_passes` (default 4). Cells may move back or on across several regions over the passes; the level's decisions stop being irrevocable. `-sweep_multilevel` uses the main engine per pair instead of the flat FM engine; `-sweep_cold` starts each pair from scratch instead of from the current assignment; `-sweep_audit` recomputes the full HPWL after every accepted move (debug). Report: per-level `sweep_*` fields and `sweep_curve_um` (HPWL after each pass). Default off. |
| `-regions` | Write the region tree for visualization: one line per region (`id parent level xmin ymin xmax ymax cells cell_area usable_area cut_x cut_y_left cut_y_right`, dbu) followed by one line per movable cell (`C inst leaf seed_x seed_y final_x final_y width height`). `exp/plot_regions.py` in the project root turns it into the cut-line, leaf and progress plots. |
| `-hypergraph` | Write the placer's hypergraph, the companion of `-regions` for offline partitioning experiments (`exp/x0_kway.py`): `H vertices nets dbu site_width row_height core_xmin core_ymin core_xmax core_ymax`, one line per vertex `V id kind name x y width height area` (kind `M` movable / `F` fixed instance / `P` I/O pin), one line per net `N id weight name v1 ox1 oy1 v2 ox2 oy2 ...` (vertex ids with the pin offsets from the vertex center), all in dbu. |
| `-hmetis_path`, `-mtkahypar_path`, `-fastpart_path` | Executables for the external engines. `-hmetis_path` expects the driver built in `tools/hmetis` (`hmetis_driver HGraphFile FixFile Nparts UBfactor Seed`), not the stock `hmetis` binary. `-mtkahypar_path` expects the `MtKaHyPar` binary. `-fastpart_path` expects `tools/fastpart/fastpart_driver.py` (run with the `python3` of the environment; it needs the `FastPart/` checkout for the presets and the pinned Mt-KaHyPar Python module built by `tools/fastpart/build.sh`). |
| `-fastpart_time`, `-fastpart_decay`, `-fastpart_min_time`, `-fastpart_min_cells`, `-fastpart_stall` | FastPart is a *fixed-budget portfolio controller* over Mt-KaHyPar (engine templates chosen by a success-weighted roster, feasible-only pool, warm V-cycle follow-ons on the incumbent, highest-quality polish in the tail), not a single partitioner call, so the budget is the knob. The driver runs as one server per placement (`-threads` persistent single-threaded engine workers, Unix socket in `-scratch_dir`; a region costs its budget, not the engine start-up) and re-expresses FastPart's default mode for one region: the terminals are fixed vertices, the children's bounds (both sides) replace the symmetric ε window, the seed assignment (with `-seed_placement`) enters as a warm start. A region at level L gets `fastpart_time · fastpart_decay^L` seconds (defaults 10 s and 0.5: the root's budget is halved per level, so the tree costs about twice the root per lane; decay 1 = the same budget everywhere), at least `fastpart_min_time` (0.2 s). Regions with ≤ `fastpart_min_cells` cells go to TritonPart (default 16, what the exact end game solves anyway; 0 = FastPart everywhere). `fastpart_stall` ends a region after that many consecutive runs without improvement (default 10; 0 = FastPart's fixed budget). E15: the defaults cost 10 / 134 / 108 s on gcd / aes / ibex (TritonPart 2 / 111 / 97 s) for 1.054 / 1.053 / 1.121× `gpl` against TritonPart's 1.074 / 1.139 / 1.221×; the top four levels alone (`-top_engine fastpart -top_levels 4 -fastpart_decay 1 -fastpart_time 5 -fastpart_stall 0`) already give 1.078 / 1.076 / 1.115×. `MCP_FASTPART_TRACE=<file>` in the environment makes the server append one JSON line per region (level, size, budget, wall, engine runs, winning template, early stop); `tools/fastpart/trace.py` tabulates it per level. |
| `-scratch_dir` | Directory for external engines' temporary files. Default `/tmp`. |

Debug groups (`set_debug_level MCP <group> 1`): `timing` (runtime per level,
end cases and ironing), `balance` (children outside their capacity bounds,
regions whose seed assignment could not meet the capacities), `endcase`
(end-case regions whose cells did not fit, leaves rewritten per feedback pass), `ironing` (per-pass gains). `set_debug_level PAR netvector 1` checks
after every `NetVectorRefine` pass that the accumulated gain equals the change
of the vector cost (warning PAR-0928 otherwise) and that none of the pair-wise,
k-way and greedy refiners increases it (PAR-0929).

## Example scripts

See `test/mincut_place01.tcl`:

```tcl
read_lef Nangate45/Nangate45.lef
read_def simple01.def
mincut_placement -k 2 -engine tritonpart -seed 1
detailed_placement
check_placement -verbose
```

## Regression tests

There are a set of regression tests in `./test`. For more information, refer to
this [section](../../README.md#regression-tests).

## License

BSD 3-Clause License. See [LICENSE](../../LICENSE) file.
