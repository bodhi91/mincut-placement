# 1. Min-cut placers against gpl on few-row floorplans

Source: E17 (`e17_krow`), 180 floorplans = {gcd, aes, ibex, jpeg, swerv_no_macro, cva6_no_macro} × {nangate45, asap7} × utilization {60, 70, 80 %} × {1, 2, 4, 6, 8} rows, one seed, no DPO. gpl = `global_placement -disable_pin_density_adjust -overflow 0.2` (the floorplan author's baseline; reproduces his HPWL within 0.5 %). mcp options `-k 2 -reserve minlocal -density <flow> -max_tolerance 1.0 -pad 0 -optimal_cells 12 -feedback_passes 1`, 4 threads. Metric: HPWL after detailed placement, ratio to gpl on the same floorplan, geomean over floorplans (wins/n). Capo is in table 4.

## 1.1 Coverage

gpl produced a legal placement on 149 / 180 floorplans; failures: failed: DPL-0033 ×26, failed: GPL-0305 ×5. Every mcp arm placed all 180 (legal, 0 violations). Ratios below are over the 149 floorplans with a gpl result.

## 1.2 Overall and by number of rows

| subset | n | mcp-TritonPart | mcp-hMETIS | mcp-Mt-KaHyPar | mcp-FastPart |
|---|---|---|---|---|---|
| all | 149 | 0.789 (127/149) | 0.785 (122/149) | 0.836 (103/149) | 0.813 (128/149) |
| 1 row | 30 | 0.931 (23/30) | 0.914 (23/30) | 1.003 (11/30) | 0.993 (22/30) |
| 2 rows | 35 | 0.834 (31/35) | 0.836 (29/35) | 0.877 (26/35) | 0.878 (33/35) |
| 4 rows | 18 | 0.893 (16/18) | 0.899 (12/18) | 0.924 (13/18) | 0.872 (16/18) |
| 6 rows | 31 | 0.786 (28/31) | 0.783 (26/31) | 0.839 (25/31) | 0.784 (27/31) |
| 8 rows | 35 | 0.610 (29/35) | 0.604 (32/35) | 0.646 (28/35) | 0.633 (30/35) |

## 1.3 By design

| subset | n | mcp-TritonPart | mcp-hMETIS | mcp-Mt-KaHyPar | mcp-FastPart |
|---|---|---|---|---|---|
| gcd (498 cells) | 28 | 0.997 (17/28) | 0.997 (18/28) | 1.003 (15/28) | 0.997 (17/28) |
| aes (12794 cells) | 29 | 0.978 (22/29) | 0.970 (23/29) | 1.015 (12/29) | 0.935 (27/29) |
| ibex (13413 cells) | 24 | 0.890 (24/24) | 0.961 (16/24) | 0.924 (20/24) | 0.866 (24/24) |
| jpeg (53317 cells) | 24 | 0.557 (24/24) | 0.544 (24/24) | 0.667 (21/24) | 0.575 (24/24) |
| swerv_no_macro (79088 cells) | 22 | 0.696 (19/22) | 0.672 (19/22) | 0.723 (16/22) | 0.833 (17/22) |
| cva6_no_macro (82923 cells) | 22 | 0.640 (21/22) | 0.611 (22/22) | 0.681 (19/22) | 0.696 (19/22) |

## 1.4 By PDK and by utilization

| subset | n | mcp-TritonPart | mcp-hMETIS | mcp-Mt-KaHyPar | mcp-FastPart |
|---|---|---|---|---|---|
| nangate45 | 82 | 0.803 (65/82) | 0.798 (64/82) | 0.856 (52/82) | 0.813 (70/82) |
| asap7 | 67 | 0.772 (62/67) | 0.768 (58/67) | 0.813 (51/67) | 0.814 (58/67) |
| util 60 % | 53 | 0.821 (41/53) | 0.804 (42/53) | 0.860 (36/53) | 0.807 (41/53) |
| util 70 % | 47 | 0.732 (40/47) | 0.731 (37/47) | 0.784 (32/47) | 0.796 (42/47) |
| util 80 % | 49 | 0.812 (46/49) | 0.818 (43/49) | 0.863 (35/49) | 0.838 (45/49) |

## 1.5 Design × rows (mcp-TritonPart / gpl)

| design | 1 row | 2 rows | 4 rows | 6 rows | 8 rows |
|---|---|---|---|---|---|
| gcd | 1.013 (4) | 0.980 (6) | 0.987 (6) | 1.003 (6) | 1.009 (6) |
| aes | 0.973 (5) | 0.993 (6) | 0.941 (6) | 0.969 (6) | 1.018 (6) |
| ibex | 0.969 (4) | 0.937 (6) | 0.921 (2) | 0.879 (6) | 0.798 (6) |
| jpeg | 0.733 (6) | 0.580 (6) | 0.481 (1) | 0.458 (5) | 0.492 (6) |
| swerv_no_macro | 1.030 (5) | 0.808 (6) | 0.886 (2) | 0.770 (4) | 0.330 (5) |
| cva6_no_macro | 0.962 (6) | 0.776 (5) | 0.634 (1) | 0.674 (4) | 0.350 (6) |

(n in parentheses = floorplans with a gpl result, out of 6 per cell: 2 PDKs × 3 utilizations.)

## 1.6 Routed wirelength and runtime

| placer | routed WL / gpl (wins/n, both routed) | placer time / gpl (median) |
|---|---|---|
| mcp-TritonPart | 0.918 (62/96) | 9.9× |
| mcp-hMETIS | 0.926 (55/96) | 5.3× |
| mcp-Mt-KaHyPar | 0.947 (49/97) | 13.6× |
| mcp-FastPart | 0.904 (70/96) | 12.3× |

## 1.7 k-way refinement on the same floorplans (X3)

Source: `x3_mega` (the x3_full grid): gpl, mcp cutnet (= E17 `tp` options) and mcp k-way (`-kway_refine_levels {2 3 4 5}`), seed 1, on the k-row floorplans that had a gpl result (DPO on for the small designs; metric is pre-DPO HPWL).

| subset | n | mcp cutnet / gpl | mcp k-way / gpl | k-way / cutnet |
|---|---|---|---|---|
| all | 149 | 0.789 (127/149) | 0.778 (130/149) | 0.985 (151/180) |
| 1 row | 30 | 0.931 (23/30) | 0.919 (24/30) | 0.987 (31/36) |
| 2 rows | 35 | 0.834 (31/35) | 0.820 (32/35) | 0.983 (28/36) |
| 4 rows | 18 | 0.893 (16/18) | 0.884 (16/18) | 0.984 (30/36) |
| 6 rows | 31 | 0.786 (28/31) | 0.776 (29/31) | 0.987 (31/36) |
| 8 rows | 35 | 0.610 (29/35) | 0.601 (29/35) | 0.985 (31/36) |
| gcd | 28 | 0.997 (17/28) | 0.997 (18/28) | 1.000 (12/30) |
| aes | 29 | 0.978 (22/29) | 0.962 (23/29) | 0.984 (30/30) |
| ibex | 24 | 0.890 (24/24) | 0.875 (24/24) | 0.981 (29/30) |
| jpeg | 24 | 0.557 (24/24) | 0.555 (24/24) | 0.996 (22/30) |
| swerv_no_macro | 22 | 0.696 (19/22) | 0.681 (19/22) | 0.979 (28/30) |
| cva6_no_macro | 22 | 0.640 (21/22) | 0.623 (22/22) | 0.973 (30/30) |

