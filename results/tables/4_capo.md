# 4. Capo against gpl and against the min-cut placers

Capo 10.5 (UMpack `CapoTest1.exe`) through the Bookshelf bridge `bench/capo.tcl`, `-densityTarget` = the flow's density (percent), cells padded as gpl pads them. Same evaluation as every other arm: detailed placement, (DPO where the grid had it), HPWL after detailed placement / gpl. Here `mcp` = mcp-TritonPart (`tp`) unless stated.

## 4.1 Few-row floorplans (E17, 149 floorplans with a gpl result)

| subset | n | Capo / gpl | Capo / mcp-TritonPart | Capo / best mcp engine |
|---|---|---|---|---|
| all | 149 | 0.830 (88/149) | 1.052 (27/149) | 1.089 (6/149) |
| 1 row | 30 | 0.959 (15/30) | 1.030 (11/30) | 1.069 (3/30) |
| 2 rows | 35 | 0.872 (21/35) | 1.046 (6/35) | 1.083 (2/35) |
| 4 rows | 18 | 0.949 (10/18) | 1.063 (2/18) | 1.100 (0/18) |
| 6 rows | 31 | 0.836 (21/31) | 1.064 (3/31) | 1.097 (0/31) |
| 8 rows | 35 | 0.648 (21/35) | 1.062 (5/35) | 1.100 (1/35) |
| gcd | 28 | 1.073 (3/28) | 1.076 (1/28) | 1.083 (0/28) |
| aes | 29 | 1.017 (8/29) | 1.040 (6/29) | 1.093 (0/29) |
| ibex | 24 | 0.929 (19/24) | 1.045 (6/24) | 1.074 (0/24) |
| jpeg | 24 | 0.592 (23/24) | 1.062 (3/24) | 1.088 (1/24) |
| swerv_no_macro | 22 | 0.730 (16/22) | 1.048 (6/22) | 1.097 (2/22) |
| cva6_no_macro | 22 | 0.666 (19/22) | 1.042 (5/22) | 1.102 (3/22) |
| nangate45 | 82 | 0.840 (48/82) | 1.046 (18/82) | 1.085 (4/82) |
| asap7 | 67 | 0.818 (40/67) | 1.061 (9/67) | 1.094 (2/67) |

Over all 180 floorplans (gpl failures included, Capo and mcp both placed everything): Capo / mcp-TritonPart = 1.049, Capo better on 38/180. Capo placed all 180; gpl failed 31 (GPL-0305 ×5, DPL-0033 ×26).

Runtime: Capo / mcp-TritonPart placer time median 0.53×.

## 4.2 AR-1 floorplans (E23)

| floorplan | Capo / gpl | mcp-TritonPart / gpl | Capo / mcp-TritonPart | Capo / best mcp engine |
|---|---|---|---|---|
| gcd_ar1_asap7 | 1.159 | 1.033 | 1.122 | 1.124 |
| gcd_ar1_nangate45 | 1.152 | 1.107 | 1.041 | 1.153 |
| aes_ar1_asap7 | 1.254 | 1.123 | 1.117 | 1.127 |
| aes_ar1_nangate45 | 1.192 | 1.119 | 1.065 | 1.065 |
| ibex_ar1_asap7 | 1.307 | 1.172 | 1.116 | 1.135 |
| ibex_ar1_nangate45 | 1.245 | 1.119 | 1.113 | 1.120 |
| jpeg_ar1_asap7 | 1.185 | 1.047 | 1.132 | 1.160 |
| jpeg_ar1_nangate45 | 1.269 | 1.166 | 1.089 | 1.093 |
| swerv_wrapper_ar1_asap7 | 1.365 | 1.265 | 1.079 | 1.079 |
| swerv_wrapper_ar1_nangate45 | 1.540 | 1.210 | 1.273 | 1.294 |
| cva6_ar1_asap7 | 1.286 | 1.241 | 1.036 | 1.067 |
| cva6_ar1_nangate45 | 1.346 | 1.243 | 1.084 | 1.094 |
| mempool_tile_ar1_asap7 | 1.235 | 1.192 | 1.036 | 1.075 |
| mempool_tile_ar1_nangate45 | 1.304 | 1.130 | 1.154 | 1.194 |
| xiangshan_ar1_asap7 | 1.185 | 1.053 | 1.125 | 1.125 |
| xiangshan_ar1_nangate45 | 1.357 | 1.210 | 1.121 | 1.121 |
| megaboom_ar1_asap7 | 0.947 | 0.960 | 0.987 | 1.064 |
| megaboom_ar1_nangate45 | 1.145 | 1.122 | 1.020 | 1.067 |
| mempool_group_ar1_asap7 | 1.187 | 1.133 | 1.048 | 1.048 |
| mempool_group_ar1_nangate45 | 1.148 | 1.034 | 1.110 | 1.110 |
| **geomean (wins/n)** | 1.235 (1/20) | 1.131 (1/20) | 1.092 (1/20) | 1.114 (0/20) |

## 4.3 Capo settings on the development checkpoints (E14; gcd / aes nangate45, ibex sky130hd; 3 seeds, DPO on; pre-DPO metric)

| arm (/ gpl) | gcd_nangate45 | aes_nangate45 | ibex_sky130hd |
|---|---|---|---|
| Capo defaults | 1.125 | 0.916 | 1.834 |
| `-densityTarget` = flow density | 1.221 | 1.192 | 1.054 |
| `-tryHarder` | 1.123 | 0.937 | 1.782 |
| padded cells + `-densityTarget` (the arm used from E16 on) | 1.318 | 1.470 | 1.279 |
| mcp-TritonPart (E12 defaults) | 1.083 | 1.155 | 1.289 |

## 4.4 The six placers on the five development checkpoints (E16; 3 seeds, DPO on; pre-DPO metric, / gpl)

| placer | gcd_nangate45 | aes_nangate45 | ibex_sky130hd | jpeg_sky130hd | tinyRocket_nangate45 | geomean |
|---|---|---|---|---|---|---|
| Capo | 1.318 | 1.470 | 1.279 | 1.378 | 1.435 | 1.374 |
| mcp-TritonPart | 1.083 | 1.155 | 1.289 | 1.344 | 1.256 | 1.222 |
| mcp-hMETIS | 1.122 | 1.144 | 1.289 | 1.320 | 1.175 | 1.207 |
| mcp-Mt-KaHyPar | 1.124 | 1.184 | 1.238 | 1.432 | 1.290 | 1.249 |
| mcp-FastPart | 1.090 | 1.116 | 1.198 | 1.338 | 1.259 | 1.197 |

## 4.5 Capo as the seed of the American map (AM1 arm `AMc`; post-DPO HPWL / gpl, 3 seeds)

| floorplan | X (min-cut) | AM2 (gpl τ0.3 seed) | AMc (Capo seed) | AMg (final gpl seed) | AMx (X's own result as seed) |
|---|---|---|---|---|---|
| aes_ar1_nangate45 | 1.166 | 1.038 | 1.152 | 1.037 | 1.166 |
| ibex_ar1_asap7 | 1.201 | 1.062 | 1.236 | 1.056 | 1.201 |
| ibex_u70_r8_asap7 | 0.794 | 1.044 | 0.825 | 0.919 | 0.794 |
| jpeg_ar1_nangate45 | 1.254 | 1.077 | 1.259 | 1.025 | 1.254 |
| jpeg_u70_r4_nangate45 | 0.610 | 1.040 | 0.644 | 0.933 | 0.610 |
| swerv_wrapper_ar1_nangate45 | 1.237 | 1.468 | 1.552 | 1.528 | 1.237 |

