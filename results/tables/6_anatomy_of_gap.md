# 6. Anatomy of the gap between min-cut and analytical placement

Development checkpoints gcd (nangate45, 0.3 k cells), aes (nangate45, 20 k), ibex (sky130hd, 20 k), ORFS floorplans (AR ≈ 1), 3 seeds. Reference: `gpl_default` of E0 (plain `global_placement`, HPWL after detailed placement: gcd 4 071, aes 461 800, ibex 595 400 µm). Ratios are HPWL after detailed placement / that reference unless stated.

## 6.1 The from-scratch result over the project (HPWL after detailed placement / E0 gpl)

| configuration | gcd | aes | ibex |
|---|---|---|---|
| M1 placer: cut-net, 1 wave, uniform whitespace (E0) | 1.784 | 2.846 | 1.468 |
| + waves (E3) | 1.748 | 2.749 | 1.457 |
| + `minlocal` whitespace, padding (E5 / Gate 3) | 1.503 | 1.359 | 1.426 |
| + end game: exact ≤16, optimal ≤10, ironing (E6 `all`) | 1.272 | 1.273 | 1.351 |
| + `-optimal_cells 12` (E6 `opt12`) | 1.205 | 1.241 | 1.331 |
| + optimal in-row positions (E9) | 1.099 | 1.158 | 1.299 |
| + leaf feedback pass (E10 `p2`, current default) | 1.083 | 1.155 | 1.289 |
| + 8 placement feedback passes (E11 `fb8`, 5–11× time) | 1.076 | 1.140 | 1.218 |
| gpl seed → transport → end game (E9 `gpl_optimal`) | 1.049 | 0.954 | 1.036 |
| gpl seed → end game, 3 passes (E10 `gpl_p3`) | 1.039 | 0.943 | 1.028 |
| gpl seed → refinement with seed terminals → end game (E11) | 1.018 | 0.981 | 1.050 |

The first eight rows are the from-scratch placer; the last three use gpl's placement as a seed and show what the tree's lower levels and end game are worth when the top-level decisions are analytical.

## 6.2 Per-level attribution (E8): gpl decides levels < L, min-cut decides from L down

`-seed_levels L` with the gpl seed assigned by transport above level L; L = 0 is pure min-cut, `Lall` = every level seeded (= the seed through the tree). HPWL / E0 gpl.

| seeded above | gcd | aes | ibex |
|---|---|---|---|
| L = 0 | 1.205 | 1.241 | 1.331 |
| L = 1 | 1.225 | 1.308 | 1.263 |
| L = 2 | 1.208 | 1.238 | 1.293 |
| L = 3 | 1.261 | 1.198 | 1.288 |
| L = 4 | 1.243 | 1.181 | 1.223 |
| L = 5 | 1.222 | 1.169 | 1.190 |
| L = 6 | 1.197 | 1.176 | 1.171 |
| L = 7 | 1.198 | 1.127 | 1.145 |
| L = 8 | 1.198 | 1.094 | 1.129 |
| L = 9 | 1.198 | 1.069 | 1.112 |
| L = 10 | 1.198 | 1.054 | 1.101 |
| L = 11 | 1.198 | 1.043 | 1.085 |
| L = 12 | 1.198 | 1.031 | 1.069 |
| L = 13 | 1.198 | 1.024 | 1.069 |
| all levels seeded | 1.198 | 1.021 | 1.069 |

Reading: on aes and ibex the gap closes almost linearly as more levels are handed to gpl (aes 1.24 → 1.02, ibex 1.33 → 1.07 over 13 levels, ≈ 0.02 per level): the loss is distributed over the tree, no single level carries it. gcd stays at 1.20 whatever is seeded — its gap was in the leaves and end game, which E9/E10 later recovered (table 6.1).

## 6.3 Legalization and DPO on both flows (E12; 3 seeds)

| design | arm | HPWL placer output | after DPL | legalization | after DPO | DPO gain | post-DPO / gpl post-DPO |
|---|---|---|---|---|---|---|---|
| gcd_nangate45 | gpl | 3,904 | 4,071 | +4.3 % | 3,908 | -4.0 % | 1.000 |
| gcd_nangate45 | mcp fb1 | 4,385 | 4,410 | +0.6 % | 4,196 | -4.8 % | 1.074 |
| gcd_nangate45 | mcp fb4 | 4,343 | 4,380 | +0.8 % | 4,185 | -4.5 % | 1.071 |
| aes_nangate45 | gpl | 457,376 | 461,832 | +1.0 % | 399,378 | -13.5 % | 1.000 |
| aes_nangate45 | mcp fb1 | 531,812 | 533,289 | +0.3 % | 455,054 | -14.7 % | 1.139 |
| aes_nangate45 | mcp fb4 | 525,259 | 526,857 | +0.3 % | 445,766 | -15.4 % | 1.116 |
| ibex_sky130hd | gpl | 560,052 | 595,423 | +6.3 % | 573,561 | -3.7 % | 1.000 |
| ibex_sky130hd | mcp fb1 | 754,368 | 767,766 | +1.8 % | 700,234 | -8.8 % | 1.221 |
| ibex_sky130hd | mcp fb4 | 718,153 | 731,446 | +1.9 % | 683,872 | -6.5 % | 1.192 |

mcp's output is already row-aligned and overlap-free (legalization ≈ 0); gpl pays 5–15 % to legalize and recovers more of it in DPO than mcp does.

## 6.4 Legalization cost on the large grids (median of HPWL after DPL / placer output − 1)

| grid | gpl | Capo | mcp-TritonPart | mcp cutnet | mcp k-way | mcp-hMETIS | mcp-Mt-KaHyPar | mcp-FastPart |
|---|---|---|---|---|---|---|---|---|
| E17 few-row (180 floorplans) | 0.28 % | 0.03 % | 0.00 % | – | – | 0.00 % | 0.00 % | 0.00 % |
| E23 AR 1 (20 floorplans) | 7.66 % | 1.35 % | 0.00 % | – | – | 0.00 % | 0.00 % | 0.00 % |
| X4 AR × util sweep (182 floorplans, 3 seeds) | 9.46 % | – | – | 0.01 % | 0.01 % | – | – | – |

gpl's legalization cost is small on the few-row floorplans (cells are already nearly in rows) and 7–10 % on the square and wide floorplans; every mcp arm (and Capo, which also places in rows) is below 1.5 %.

## 6.5 The hybridization spectrum (E18–E21e; post-DPO HPWL / gpl, 3 seeds)

| arm | gcd | aes | ibex |
|---|---|---|---|
| min-cut baseline X (TritonPart, minlocal) | 1.074 [1.059, 1.085] | 1.139 [1.130, 1.152] | 1.221 [1.180, 1.256] |
| X + ATP (QP oracle terminals, anchor 1) | 1.073 [1.070, 1.077] | 1.113 [1.103, 1.119] | 1.213 [1.164, 1.241] |
| X + ACG (oracle whitespace, no terminals) | 1.099 [1.090, 1.104] | 1.054 [1.037, 1.075] | 1.250 [1.243, 1.260] |
| X + ATP + ACG, adaptive (hybrid B) | 1.051 [1.043, 1.061] | 1.000 [0.988, 1.014] | 1.165 [1.154, 1.181] |
| hybrid B, FastPart engine | 1.067 [1.058, 1.072] | 0.971 [0.963, 0.984] | 1.208 [1.187, 1.225] |
| gpl (ovf 0.1) seed -> transport -> end game (H-commit) | 1.006 [1.003, 1.009] | 0.965 [0.965, 0.965] | 1.009 [1.008, 1.009] |
| gpl ovf 0.3 seed -> net-aware transport (hybrid A) | 1.021 [1.020, 1.023] | 0.942 [0.938, 0.949] | 1.001 [1.000, 1.001] |
| gpl ovf 0.5 seed -> net-aware transport (hybrid A) | 1.072 [1.066, 1.075] | 0.924 [0.924, 0.926] | 1.050 [1.049, 1.050] |

From the top: pure min-cut; min-cut informed by analytical positions (terminals, whitespace); analytical decides, min-cut commits (seed → transport → end game). The order of the rows is the order of how much of the decision is analytical. Figures: `results/viz/hybrid/`.

## 6.6 Does informing min-cut help where min-cut already wins? (E24 on the k-row floorplans)

| design | n | TritonPart | TritonPart + hybrid B | FastPart | FastPart + hybrid B |
|---|---|---|---|---|---|
| gcd | 28 | 1.025 | 1.034 | 1.027 | 1.030 |
| aes | 29 | 0.990 | 1.022 | 0.946 | 0.955 |
| ibex | 24 | 0.916 | 0.952 | 0.891 | 0.920 |
| jpeg | 24 | 0.609 | 0.610 | 0.628 | 0.643 |
| all | 105 | 0.879 | 0.896 | 0.869 | 0.883 |
| util 60 | 34 | 0.883 | 0.921 | 0.864 | 0.913 |
| util 70 | 35 | 0.868 | 0.878 | 0.864 | 0.865 |
| util 80 | 36 | 0.885 | 0.892 | 0.878 | 0.872 |
| rows 1 | 19 | 0.903 | 0.924 | 0.900 | 0.916 |
| rows 2 | 24 | 0.881 | 0.895 | 0.866 | 0.884 |
| rows 4 | 15 | 0.956 | 0.983 | 0.941 | 0.948 |
| rows 6 | 23 | 0.853 | 0.874 | 0.848 | 0.863 |
| rows 8 | 24 | 0.837 | 0.848 | 0.824 | 0.838 |
| platform nangate45 | 57 | 0.915 | 0.931 | 0.900 | 0.910 |
| platform asap7 | 48 | 0.838 | 0.857 | 0.833 | 0.851 |

floorplans where the hybrid beats its baseline: tp_hyb 39/105, fp_hyb 45/105

## 6.7 Cut quality versus information (I1; six AR-1 floorplans, 2 seeds, post-DPO HPWL / gpl)

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

`deg*`: the bisections deliberately degraded to ×1.4–×3.3 the cut; `imp`: best of 4 runs with 10 FM passes; `oT`: perfect gpl positions for every cell outside the region as terminals; `oA`: the gpl-derived assignment decides inside the region. Elasticity of HPWL to cut ≈ 0.17; a partitioner cutting 10 % fewer nets would be worth ~2 %; knowing where cells inside a region end up relative to each other closes 57–89 % of the gap on the macro-free floorplans.

