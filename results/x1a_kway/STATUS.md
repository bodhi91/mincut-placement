# X1a: from-scratch top-level k-way construction in the tree — status

Plan Step 2 (after X1b, `results/x1b_kway/STATUS.md`).  Question: X0
measured offline that a *from-scratch* 4-way Steiner-tree mapping into the
tree's level-2 boxes (with the tree's loads as budgets) beats the two
bisections by 12–14 % of the HPWL-at-centers proxy on ibex_sky130hd (and
loses 1–2 % on aes).  Does that survive inside the placer, on the X1b grid,
down to the final post-DPO HPWL — and does it compose with the X1b
refinement?

**Answer: no.**  Inside the placer the from-scratch construction is worse
than the tree's own two bisections in 23 of 26 attempts — worse in the
placer's HPWL-at-centers (+0.3 … +12.9 %) *and* worse on Mt-KaHyPar's own
Steiner objective (+4.6 %, +7.8 % on the two aes_ar1 runs checked), so this
is not the pin-modeling mismatch of X0 finding 4 but a weaker assignment.
The acceptance test rejects it, so the arm is harmless (`x1a` = `base` in
11 of 13 runs) but useless; the Step 1 refinement remains the carrier of
the gain, and construction + refinement equals refinement alone.

## Mechanism

`-kway_construct_levels {2}` (`OpenROAD/src/mcp/src/KwayRefine.cpp`,
`Decomposer::kwayRefine(..., from_scratch = true)`): same hypergraph,
fixed vertices (pins and fixed cells, weight 1, fixed to the nearest box),
target graph (boxes, edges between adjacent boxes weighted by the center
distance in sites) and budgets (`load × 1.02 + fixed weight`) as the X1b
refinement, but `mt_kahypar_map` from scratch instead of
`mt_kahypar_improve_mapping` from the tree's assignment.  Same acceptance
test (the placer's HPWL at the region centers must improve, otherwise the
tree's assignment stays).  A level in both lists is constructed first and
refined afterwards.  Report: per-level `kwayc_moved`,
`kwayc_hpwl_before_um`, `kwayc_hpwl_after_um`, `kwayc_accepted`,
`kwayc_seconds`.  Cost 0.3–0.5 s per construction.

Binary `OpenROAD/build/bin/openroad` (X1b source + X1a, linked
with `tools/mtkahypar/lib`; defaults reproduce E25).  E23/E24/E25 were
not touched.

## Smoke (ibex_sky130hd, seed 1, no DPO — X0's design)

Construction accepted: proxy 354 935 → 336 172 µm (−5.3 %; X0 offline:
−12 … −13 % on the same tree assignment, 354 935 µm), then the X1b
refinements −1.8 / −1.9 / −2.8 / −1.0 % at levels 2–5; final HPWL
728 674 → 716 428 µm (−1.7 %).  So the in-tree construction reproduces the
*direction* of X0 on X0's design, at under half the size.  The gap to X0
was not chased (candidates: X0's offline vertex weights / net weights, and
Mt-KaHyPar's nondeterminism — see below).

## Grid (`exp/x1a_kway.cfg`, 52 runs, 27 min, screen `x1a_kway`)

X1b conditions: krow_small (aes_u70_r2_nangate45, ibex_u70_r6_nangate45;
3 seeds), base_small (aes_ar1_nangate45, ibex_ar1_asap7; 3 seeds, soft
blockages), krow_mid (jpeg_u70_r8_asap7; 1 seed); DPO; 4 threads.  Arms:
`base`, `x1a` (`-kway_construct_levels {2}`), `x1a_r2345` (construction +
`-kway_refine_levels {2 3 4 5}`), `kway_2345` (X1b alone, rerun paired).

Post-DPO HPWL, ratio to `base`, paired by seed (same seed = same tree):

| arm | mean | wins / ties / losses (13) | worst |
|---|---|---|---|
| `x1a` | 0.998 | 2 / 11 / 0 | 1.000 |
| `x1a_r2345` | 0.986 | 11 / 1 / 1 | 1.002 |
| `kway_2345` (X1b) | 0.987 | 13 / 0 / 0 | 0.999 |

Per design (mean of seeds; routed WL ratio in parentheses):

| design | `x1a` | `x1a_r2345` | `kway_2345` |
|---|---|---|---|
| aes_ar1_nangate45 | 1.000 (1.000) | 0.989 (0.997) | 0.983 (0.990) |
| aes_u70_r2_nangate45 | 1.000 (1.000) | 0.974 (1.004) | 0.988 (0.988) |
| ibex_ar1_asap7 | 1.000 (1.000) | 0.981 (0.980) | 0.982 (0.982) |
| ibex_u70_r6_nangate45 | 0.990 (0.991) | 0.994 (0.997) | 0.991 (0.996) |
| jpeg_u70_r8_asap7 | 1.000 (1.000) | 1.002 (1.000) | 0.999 (0.997) |

Placement time: `x1a` 1.00× (rejected constructions cost 0.3–0.5 s),
`x1a_r2345` 1.04×, `kway_2345` 1.06×.

The construction itself, level 2 (k = 4), proxy after / before, 26 attempts:

| design | attempts | accepted | proxy change |
|---|---|---|---|
| aes_ar1_nangate45 | 6 | 0 | +4.9 … +12.9 % |
| aes_u70_r2_nangate45 | 6 | 1 (−0.8 %) | −0.8 … +8.9 % |
| ibex_ar1_asap7 | 6 | 0 | +0.3 … +8.4 % |
| ibex_u70_r6_nangate45 | 6 | 2 (−0.3, −1.0 %) | −1.0 … +11.6 % |
| jpeg_u70_r8_asap7 | 2 | 0 | +1.8, +9.5 % |

Debug run (aes_ar1_nangate45, seeds 1–2, `set_debug_level MCP kway 1`):
Steiner cost of the tree's assignment 213 900 / 246 895, of the
from-scratch mapping 223 652 / 266 202 (+4.6 / +7.8 %) — the construction
loses on the objective it optimizes, with the same budgets.

## Findings

1. **The tree's two bisections are a better 4-way assignment than
   Mt-KaHyPar's from-scratch 4-way mapping into the same boxes and
   budgets**, on 4 of the 5 grid designs (every attempt on aes_ar1,
   ibex_ar1, jpeg; 10 of 12 on the u70 designs), by the placer's proxy and
   by the Steiner objective alike.  X0's 12–14 % on ibex_sky130hd is a
   property of that design (in-tree it is −5 %), not of the top of the
   tree in general.  X0 finding 1 ("RB's first cut is irrevocable and wrong
   by this much") does not generalize.
2. **The from-scratch mapping is erratic**: the same input gives +0.3 % and
   +8.4 % (ibex_ar1 s1), +1.8 % and +9.5 % (jpeg), −0.3 % and +1.6 %
   (ibex_u70 s1) in two runs — Mt-KaHyPar's default preset is
   nondeterministic with 4 threads, and the 4-node Steiner objective with
   tight individual budgets (2 % over unequal loads) has many near-ties.
   The refinement from the tree's assignment does not share this: 90/91
   (X1b) and 39/39 (this grid) accepted.
3. **Composition adds nothing**: `x1a_r2345` 0.986 vs `kway_2345` 0.987;
   the only accepted construction in that arm (aes_u70_r2 s1, −0.8 % proxy,
   then −3.6 % refinement) gave the grid's best run (0.933), but n = 1 and
   the same arm lost 0.2 % on jpeg (noise in the tree below).
4. The acceptance test did its job: with 23 rejections the arm never lost
   against `base` (worst 1.000 for `x1a`), so a guarded construction is
   safe to leave in the code, default off.

## Decision

Step 2 is negative: the from-scratch top-level construction does not earn
a place in the default flow.  The gain of the k-way direction is in the
whole-level *refinement* of the tree's assignment (X1b, −1.2 … −1.3 %
post-DPO HPWL, 25/26 paired wins across both grids), which keeps RB's
locality and the placer's objective and only repairs its irrevocable
decisions locally.  Plan Step 3 (composition on the full set) should carry
`-kway_refine_levels {2 3 4 5}` without construction.

If the construction is revisited, the two cheap probes are: best-of-N
constructions (finding 2; 0.3 s each, pick the best proxy before the
acceptance test) and a looser top-level budget (slack 1.05–1.10 at level 2
only), both of which address the near-tie / tight-budget behaviour; a `km1`
control (X0 arm) would separate the joint decision from the objective.
Neither is expected to recover more than the −5 % seen on ibex_sky130hd.

## Files

- Code: `OpenROAD/src/mcp/src/KwayRefine.{h,cpp}` (`from_scratch`),
  `Decomposer.{h,cpp}` (`kwayRefine(level, next_level, from_scratch,
  stats)`, `kwayc_*` level stats), `MinCutPlacer.{h,cpp}`
  (`Options::kway_construct_levels`, report), `mcp.i`, `mcp.tcl`
  (`-kway_construct_levels`), `README.md`.
- Grid: `exp/x1a_kway.cfg`, `exp/x1a_launch.sh`, log
  `results/x1a_kway.log`, runs `results/x1a_kway/*.json` (+ `.mcp.json`
  per-level `kwayc_*`), `python3 exp/compare.py results/x1a_kway --baseline
  base`.
- Build log `results/kway_build5.log`; binary
  `OpenROAD/build/bin/openroad`.
