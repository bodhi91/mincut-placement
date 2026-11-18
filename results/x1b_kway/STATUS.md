# X1b: whole-level k-way refinement — status

Launched 2026-10-27 21:50 on host-A (screen `x1b_kway`, log
`results/x1b_kway.log`, `exp/x1b_launch.sh` → `exp/x1b_kway.cfg`).  Binary
`OpenROAD/build/bin/openroad`, separate from E23/E24/E25's builds,
which keep running; configured with `-DMCP_MTKAHYPAR_DIR=tools/mtkahypar`
(Mt-KaHyPar 1.6.2 as a library, `tools/mtkahypar/build.sh`).  The defaults
reproduce E25 bit for bit (ibex_sky130hd seed 1 base = X0's 728 674.2 µm).

## What is tested

Step 1 of the k-way plan after X0 (`results/x0_kway/STATUS.md`): the only
X0 result positive on both designs was *refining* the tree's level
assignment as one k-way Steiner-tree mapping (−2 % at k = 4, −3/−4 % at
k = 16/12 of the level's HPWL-at-centers), with the tree's boxes and loads
kept.  Implementation (`OpenROAD/src/mcp/src/KwayRefine.{h,cpp}`,
`Decomposer::kwayRefine`, option `-kway_refine_levels`):

- after the bisections of a level are committed, the regions of the new
  level are the blocks of a Mt-KaHyPar process-mapping problem: target
  graph = regions, edges between adjacent regions weighted by the center
  distance in sites; vertex weights = padded cell area in sites; budgets =
  the region's current cell area × `-kway_slack` (1.02) + the fixed weight
  it holds; pins, fixed cells and cells of leaves already decided are fixed
  vertices of weight 1 (weight 0 is silently unfixed by Mt-KaHyPar, X0);
- `mt_kahypar_improve_mapping`, 3 V-cycles, default preset, from the tree's
  assignment;
- acceptance: the placer's own HPWL with every movable cell at its region
  center (pins at their true positions) must improve, otherwise the result
  is discarded (guards the pin-modeling flaw of the Steiner metric, X0
  finding 4); accepted refinements log MCP-0058, every attempt is in the
  report (`kway_k`, `kway_moved`, `kway_hpwl_before_um`, `kway_hpwl_after_um`,
  `kway_accepted`, `kway_seconds`).

Arms (E25 cutnet_tp placer settings, TritonPart, 4 threads):

| arm | `-kway_refine_levels` | regions refined |
|---|---|---|
| base | — | — |
| kway_24 | {2 4} | 4 and 16 (X0's arms) |
| kway_2345 | {2 3 4 5} | 4, 8, 16, 32 |
| kway_3 | {3} | 8 |

Designs: krow_small (aes_u70_r2_nangate45, ibex_u70_r6_nangate45; 3 seeds,
DPO), base_small (aes_ar1_nangate45, ibex_ar1_asap7; 3 seeds, DPO, soft
blockages), krow_mid (jpeg_u70_r8_asap7; 1 seed, DPO).  Metric: HPWL after
detailed placement + DPO (`exp/compare.py results/x1b_kway --baseline base`).

Gate (set before the run): keep X1b if the mean post-DPO HPWL gain is
≥ 1 % with no design worse than +0.5 %; otherwise the k-way line stops
here with X0 + X1b as the evidence.

## Smoke test (before the grid)

ibex_sky130hd seed 1, `-kway_refine_levels {3 5}` (8 and 32 regions, the
pre-renaming semantics): refinements moved 626 cells (−4.06 % proxy) and
720 cells (−2.34 %); final placer HPWL 728 674 → 714 915 µm (−1.9 %),
legalized 742 426 → 728 137 µm (−1.9 %).  Runtime of a refinement: < 2 s.
First grid run, aes_u70_r2 seed 1 kway_24: 4 regions −3.0 % proxy (186
cells), 16 regions −0.7 % (232 cells); placer HPWL 1 749 733 → 1 723 034 µm
(−1.5 %).

## Results (finished 22:17, 27 min, 52 runs, 0 failures)

HPWL after detailed placement + DPO, paired by seed (the same seed gives
the same tree up to the first refinement), ratio to base:

| design | kway_24 (4, 16) | kway_2345 (4, 8, 16, 32) | kway_3 (8) |
|---|---|---|---|
| aes_ar1_nangate45 | 0.989 0.983 0.981 | 0.989 0.979 0.987 | 1.001 1.001 0.996 |
| aes_u70_r2_nangate45 | 0.985 0.988 1.000 | 0.978 0.984 0.992 | 0.997 0.995 0.999 |
| ibex_ar1_asap7 | 1.012 0.993 0.986 | 0.999 0.974 0.989 | 1.005 0.991 0.994 |
| ibex_u70_r6_nangate45 | 0.989 0.989 0.992 | 0.990 0.986 1.000 | 0.992 0.994 0.996 |
| jpeg_u70_r8_asap7 (1 seed) | 1.005 | 0.996 | 0.999 |
| **mean / worst / wins** | **0.992** / 1.012 / 11 of 13 | **0.988** / 1.000 / 12 of 13 | 0.997 / 1.005 / 10 of 13 |
| routed WL (mean ratio) | 1.000 | 0.996 | 0.997 |
| placer time (mean ratio) | 1.01 | 1.05 | 0.99 |

Per-level behaviour over all kway runs (report `kway_*` fields): 90 of 91
refinements were accepted (one k = 4 attempt was +0.1 % and discarded);
proxy gain per level −1.8 % at k = 4, −1.5 % at k = 8 and 16, −0.9 % at
k = 32 (range −4.8 % to +0.1 %), 200–560 cells moved per level (1.5–4 % of
the cells), < 2 s per level.

## Conclusions

1. **The gate is met by `kway_2345`**: −1.2 % mean post-DPO HPWL, 12 of 13
   paired runs better, worst +0.03 % (ibex_u70_r6 seed 3), routed
   wirelength −0.4 %, +5 % placer time.  `kway_24` (X0's two levels) is
   −0.8 % with one loss of +1.2 % (ibex_ar1 seed 1); `kway_3` alone is
   −0.3 %: the gain comes from refining *every* level of the top of the
   tree, and the per-level proxy gains compound to roughly their sum.
2. The in-tree proxy gain per level (−1.5 %) is about half of X0's
   offline number (−2 to −4 %): the X0 dumps were of the final tree, while
   in the tree the next level's terminal propagation and the reserve
   policy re-adapt to the refined assignment, so part of the proxy gain is
   taken back below.  The final HPWL gain (−1.2 %) is nevertheless of the
   same order as the per-level proxy gain, i.e. most of what the k-way
   pass finds at a level survives the levels below it.
3. The acceptance test was almost never needed (1 of 91) with `load` caps
   (slack 1.02): the pin-modeling flaw of the Steiner metric that cost 14 %
   in X0's aes physical-caps run does not bite when the whitespace
   decision is held fixed.
4. Modest, consistent, cheap: this is a keeper as a default-off option
   and a candidate default once the E25 objective line has settled (it
   composes with any engine and objective, since it runs on the committed
   assignment).  Untested knobs that could add to it: refining k = 64
   (X0: ~0, 40–60 s), slack 1.05, more V-cycles / the `quality` preset,
   and pin groups as zero-capacity target nodes (X0 finding 4).

## Next (plan Step 2)

X1a — a *from-scratch* 4-way mapping into the level-2 boxes with the
level-2 loads as budgets (X0: −12 to −14 % proxy on ibex, +1–2 % on aes),
guarded by the same acceptance test, then the tree continues.  Same
mechanism (`mt_kahypar_map` instead of `mt_kahypar_improve_mapping`), same
grid.

## Files

- `OpenROAD/src/mcp/src/KwayRefine.{h,cpp}`, `Decomposer::kwayRefine`,
  options in `MinCutPlacer.h` / `mcp.i` / `mcp.tcl` / `README.md`, messages
  MCP-0055..0059; CMake `MCP_MTKAHYPAR_DIR`.
- `exp/x1b_kway.cfg`, `exp/x1b_launch.sh`; per-run `*.json` (schema v1,
  `levels[].kway_*`), `*.mcp.json`, `*.log` in this directory;
  `exp/compare.py results/x1b_kway --baseline base`.
- Builds: `results/kway_build3_configure.log`, `results/kway_build3.log`,
  `results/kway_build4.log`.
