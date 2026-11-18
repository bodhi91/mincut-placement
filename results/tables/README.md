# Tables

**Start with [0_paper_tables.md](0_paper_tables.md)**: every table and figure of the paper, recomputed from the CSVs in
`../` under the paper's rules (RePlAce runs that diverge, fail legalization or stop above their overflow target are
failures; RB runs whose partitioner failed at the root or on > 1 % of the bisections are failures; every ratio is over
the floorplans common to the configurations compared). Its header says which section reproduces which paper item.

Tables 1–10 are the extended views: more subsets, per-floorplan listings, runtime, routing, and the studies the paper
only cites. Tables 1–6 use a weaker condition for RePlAce (any legal placement counts, 149 of the 180 few-row
floorplans instead of 137), so their aggregates differ slightly from the paper's; tables 0 and 7–10 use the paper's rule.

| table | contents | floorplans; placers | paper | data (`../<exp>/`) | script |
|---|---|---|---|---|---|
| [0_paper_tables.md](0_paper_tables.md) | the paper's Tables I–III and the numbers behind Figs. 1, few-row, sweep, square; held-out designs | all three sets; all placers | Tables I–III, Figs. 1, few-row, sweep, square, footnotes | `x3_mega`, `e17_krow`, `e23_base`, `x4_ar_sweep`, `e26b_norevisit`, `r5_ablation_square`, `r1_fewrow_oracle`, `x5_heldout` | `exp/paper_numbers.py` |
| [1_few_rows.md](1_few_rows.md) | few-row set by row count, design, PDK, utilization; routed wirelength; runtime; RB+k | 180 few-row; RB x {TP, HM, MK, FP}, RB+k (TP) | Table II (few-row), Fig. few-row | `e17_krow`, `x3_mega` | `exp/paper_tables.py` |
| [2_ar1_designs.md](2_ar1_designs.md) | square set per floorplan and geomeans; runtime; RB+k; AR-1 part of the sweep | 20 square; RB x 4 engines, RB+k (TP) | Table II (square), Fig. square | `e23_base`, `x3_mega`, `x4_ar_sweep` | `exp/paper_tables.py` |
| [3_ar_util_sweep.md](3_ar_util_sweep.md) | aspect ratio x utilization, by design, by row count; outliers; runtime | 182 sweep; RB (TP), RB+k (TP) | Fig. 1, Fig. sweep | `x4_ar_sweep` | `exp/paper_tables.py` |
| [4_capo.md](4_capo.md) | Capo 10.5 against RePlAce and against RB, both sets; Capo settings | 180 few-row + 20 square; Capo | Table II (Capo column), Sec. few-row and square | `e17_krow`, `e23_base`, `e14_capo`, `e16_compare`, `am1` | `exp/paper_tables.py` |
| [5_american_maps.md](5_american_maps.md) | RePlAce-derived assignments through the min-cut tree (American maps): seed quality, map above / below a level, min-cut at one level | 6 floorplans, 3 seeds | H3 (Sec. "Why the Square-Die Gap Persists", Fig. anatomy (b)) | `am1` | `exp/paper_tables.py` |
| [6_anatomy_of_gap.md](6_anatomy_of_gap.md) | development history of the gap: component ladder on the development checkpoints, per-level attribution, legalization and DPO, hybridization spectrum, H1 / H2 arms | development checkpoints; square set (H1 / H2) | H1, H2 (Fig. anatomy (a)); background | `e0`–`e12`, `e18`–`e24`, `i1_info` | `exp/paper_tables.py` |
| [7_global_routing.md](7_global_routing.md) | routed wirelength and routing overflow of every placer | few-row and square; all placers | Sec. few-row ("On 87 commonly global-routable floorplans ..."), Sec. square (10.8 %) | `e17_krow`, `x3_mega`, `e23_base` | `exp/grt_tables.py` |
| [8_capo_attribution.md](8_capo_attribution.md) | where RB's advantage over Capo comes from: component ladder vs Capo, leave-one-out (exact bisection, leaf search, ironing, multi-row assignment), partitioner at the base rung | 180 few-row; RB (TP) variants, Capo | Sec. few-row ("The advantage predates our implementation") | `e27_capo_attr`, `e26b_norevisit`, `e17_krow`, `x3_mega` | `exp/capo_attribution.py` |
| [9_seed_noise.md](9_seed_noise.md) | seed sensitivity: per-floorplan coefficient of variation and spread of RB / RB+k over three seeds, stability of the ratios to RePlAce | 182 sweep, 3 seeds | footnote in Sec. "Evaluation" (median CV 0.8 %) | `x4_ar_sweep` | `exp/seed_noise.py` |

Regenerate: `python3 exp/paper_export.py` (the CSVs), then `python3 exp/paper_numbers.py` (table 0),
`python3 exp/paper_tables.py` (tables 1–6), `python3 exp/grt_tables.py` (table 7),
`python3 exp/capo_attribution.py` (table 8), `python3 exp/seed_noise.py` (table 9).

Placer codes in the tables: `gpl` = RePlAce (OpenROAD `global_placement`); `capo` = Capo 10.5; `tp`, `hm`, `mk`, `fp`
= RB with TritonPart, hMETIS, Mt-KaHyPar, FastPart (`mcp-TritonPart` etc. in older headings); `cutnet_tp` / `mcp cutnet`
= RB (TP); `kway_tp` / `mcp k-way` = RB+k (TP). Metric, unless a table says otherwise: HPWL after detailed placement and
before DPO, ratio to RePlAce on the same floorplan, seed means per floorplan, geometric mean over floorplans; (wins/n) =
floorplans where the placer beats RePlAce / floorplans compared.
