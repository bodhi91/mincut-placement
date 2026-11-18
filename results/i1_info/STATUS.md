# I1: cut quality versus information — status and findings

Grid: `exp/i1_info.cfg`, launched Nov 3 21:40 on host-B, finished Nov 4 04:34.
132 / 132 runs, 0 FAIL, 1 PART (swerv `oA` s1: placed, global route failed;
HPWL recorded).  Binary `OpenROAD/build/bin/openroad`.  Report:
`python3 exp/i1_report.py results/i1_info plots/i1` → `plots/i1/tables.md`
and `i1_cut_axis.png`, `i1_info_axis.png`, `i1_objective.png`,
`i1_amcut.png`, `i1_levels.png`.

Six square AR-1 floorplans (aes/ng45, ibex/asap7, jpeg/ng45, swerv/ng45,
cva6/asap7, mempool_tile/ng45; swerv and mempool_tile have fixed macros),
two seeds, x3 conditions.  HPWL after DPL+DPO over gpl (krow mode).

## Results

| arm | what | HPWL / gpl (geomean) | vs ctl |
|---|---|---:|---:|
| ctl | TritonPart, cut-net, standard terminals | 1.220 | 1.000 |
| imp | best of 4 runs, 10 FM passes (cut −1.4 %) | 1.200 | 0.983 |
| deg10 | bisections degraded, cut ×1.40 | 1.247 | 1.022 |
| deg25 | cut ×1.59 | 1.288 | 1.055 |
| deg50 | cut ×2.07 | 1.345 | 1.102 |
| deg100 | cut ×3.30 | 1.496 | 1.226 |
| oT | oracle terminals (gpl positions outside the region), TritonPart decides | 1.197 | 0.981 |
| oA | oracle assignment (gpl-derived transport decides), leaves unseeded | 1.157 (1.03–1.12 on the four macro-free; swerv 1.53, mempool 1.24) | 0.948 |
| hp | HPWL-vector objective, standard terminals | 1.266 | 1.038 |
| hp_oT | HPWL-vector objective, oracle terminals | 1.280 | 1.049 |

"cut" = nets newly cut by the bisections (levels[].cut_nets − inessential),
summed over the tree, relative to ctl.  Requested degradations 1.10 / 1.25 /
1.50 / 2.00 of the engine's weighted cut came out as 1.25–1.34 / 1.36–1.43 /
1.58–1.62 / 2.04–2.06 (small regions overshoot).

Cut of the gpl-derived assignment against the min-cut partition of the same
region, same hypergraph, same planned bounds (recorded in `oT` at every
bisection): 1.4–1.9× at the top five levels (where the transport is feasible
without repairs), 2–4× below; 2.1–3.7× over the tree.  Yet over the whole
tree the gpl-derived *tree* cuts 0.92–0.98× the control on aes / ibex / jpeg
(1.03–1.46× over the top six levels) and 2.8–4.0× on cva6 / swerv / mempool.

## Findings

1. **Cut quality is a weak lever in both directions.**  Upward: TritonPart is
   at its cut floor — 4 runs and 5× the FM passes remove 1.4 % of the cut and
   1.7 % of the HPWL.  Downward: HPWL rises with the cut at an elasticity of
   ~0.17 (×1.4 cut → +2.2 %, ×2.1 → +10 %, ×3.3 → +23 %).  A partitioner
   that cut 10 % fewer nets than TritonPart — which no engine does — would
   be worth ~2 %.
2. **External information is not what is missing.**  Perfect gpl positions
   for every cell outside the region buy 1.9 % under the cut-net objective
   (−12 % to +9 % per design) and nothing under the HPWL-vector objective.
   The objective change itself is a loss (1.266 vs 1.220), so "the objective
   cannot exploit the information" is true but is not the explanation: a
   geometry-aware objective with perfect terminals does not help either.
3. **Internal information is.**  The gpl-derived assignment — the only arm
   that knows where the cells *inside* a region end up relative to each
   other — closes 57–89 % of the gap on the macro-free floorplans with the
   tree, capacity policy, end game and DPO unchanged.  On the two macro
   floorplans the transport mapping of gpl positions into the tree's boxes
   breaks (as in AM-1), which says nothing about partitioning.
4. **HPWL is not a function of the cut.**  The gpl-derived tree on aes /
   ibex / jpeg sits at 0.92–0.98× the control's cut with 11–18 % better
   HPWL, where the degradation line predicts ~1 %; on cva6 it sits at 2.75×
   the cut with 12 % better HPWL, where the line predicts +18 %.  At each
   individual bisection the gpl-derived split cuts 1.4–1.9× more than the
   min-cut split (top levels); over the tree it can cut less.  The
   level-by-level cut optimum is not the tree optimum, and neither is what
   HPWL tracks.
5. **Conclusion.**  The square-die gap of hierarchical min-cut placement is
   a consequence of the decision model — a sequence of local bisections whose
   objective (cut, or center-based HPWL) sees the region's own cells as a
   point — not of how well each hypergraph is cut and not of what the
   partitioner is told about the outside.  Partitioning research aimed at
   placement should therefore not target cut quality or terminal modelling;
   it should target a formulation whose optimum coincides with the rounding
   of a global geometric embedding (coordinates carried through the
   multilevel hierarchy; many parts decided jointly across levels).

## Open

- Two seeds; seed spread is ≤ 0.02 on every arm except swerv `oA`.
- The HPWL-vector objective is ours (E25 found it weak); finding 2's second
  half rests on it.
- `oA` on macro floorplans needs the macro-aware mapping before it can be
  read as an oracle there.
- Knobs added for this study (`-cut_degrade`, `-engine_runs`,
  `-no_seed_leaf`, `am_cut_*` / `degrade_*` level statistics) are in the
  `build-kway` source; defaults reproduce x3 / AM-1.
