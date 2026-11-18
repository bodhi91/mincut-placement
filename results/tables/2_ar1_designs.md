# 2. Min-cut placers against gpl on the aspect-ratio-1 floorplans

Source: E23 (`e23_base`): the ORFS designs re-floorplanned square (AR 1) at their flow utilization, macros kept where the design has them (swerv_wrapper, mempool_tile, mempool_group), one seed, no DPO, 4 threads; gpl in the same krow mode as E17. Metric: HPWL after detailed placement / gpl. Capo is in table 4.

## 2.1 Per floorplan

| floorplan | cells | gpl HPWL (µm) | mcp-TritonPart / gpl | mcp-hMETIS / gpl | mcp-Mt-KaHyPar / gpl | mcp-FastPart / gpl |
|---|---|---|---|---|---|---|
| gcd_ar1_asap7 | 261 | 575 | 1.033 | 1.048 | 1.031 | 1.031 |
| gcd_ar1_nangate45 | 498 | 2,788 | 1.107 | 1.045 | 1.146 | 0.999 |
| aes_ar1_asap7 | 10027 | 33,472 | 1.123 | 1.113 | 1.154 | 1.152 |
| aes_ar1_nangate45 | 12794 | 132,787 | 1.119 | 1.127 | 1.144 | 1.125 |
| ibex_ar1_asap7 | 17139 | 52,249 | 1.172 | 1.211 | 1.266 | 1.151 |
| ibex_ar1_nangate45 | 13413 | 159,883 | 1.119 | 1.123 | 1.227 | 1.112 |
| jpeg_ar1_asap7 | 45598 | 94,630 | 1.047 | 1.027 | 1.085 | 1.021 |
| jpeg_ar1_nangate45 | 53317 | 449,570 | 1.166 | 1.161 | 1.203 | 1.170 |
| swerv_wrapper_ar1_asap7 | 78285 | 642,677 | 1.265 | 1.383 | 1.518 | 1.446 |
| swerv_wrapper_ar1_nangate45 | 79990 | 2,304,347 | 1.210 | 1.190 | 1.277 | 1.243 |
| cva6_ar1_asap7 | 95658 | 411,916 | 1.241 | 1.205 | 1.302 | 1.281 |
| cva6_ar1_nangate45 | 84585 | 1,355,912 | 1.243 | 1.231 | 1.346 | 1.236 |
| mempool_tile_ar1_asap7 | 123963 | 697,532 | 1.192 | 1.150 | 1.233 | 1.159 |
| mempool_tile_ar1_nangate45 | 128029 | 2,124,134 | 1.130 | 1.092 | 1.391 | 1.142 |
| xiangshan_ar1_asap7 | 422412 | 2,314,518 | 1.053 | 1.088 | 1.079 | 1.124 |
| xiangshan_ar1_nangate45 | 299596 | 8,135,343 | 1.210 | 1.259 | 1.211 | invalid¹ |
| megaboom_ar1_asap7 | 2401185 | 9,117,322 | 0.960 | 0.890 | 1.029 | invalid¹ |
| megaboom_ar1_nangate45 | 1725544 | 24,647,051 | 1.122 | 1.072 | 1.158 | invalid¹ |
| mempool_group_ar1_asap7 | 2954367 | 21,800,514 | 1.133 | 17.341 | 1.187 | invalid¹ |
| mempool_group_ar1_nangate45 | 2240074 | 87,177,289 | 1.034 | 12.906 | running | invalid¹ |

¹ FastPart's engine process could not start on these runs (its Mt-KaHyPar module had been removed from `/tmp`); every region fell back to FM. Not a FastPart result; excluded from the geomeans.

## 2.2 Geomeans

| subset | n | mcp-TritonPart / gpl | mcp-hMETIS / gpl | mcp-Mt-KaHyPar / gpl | mcp-FastPart / gpl |
|---|---|---|---|---|---|
| all | 20 | 1.131 (1/20) | 1.462 (1/20) | 1.204 (0/19) | 1.155 (1/15) |
| < 200 k cells | 14 | 1.152 (0/14) | 1.147 (0/14) | 1.231 (0/14) | 1.157 (1/14) |
| ≥ 200 k cells (xiangshan, megaboom, mempool_group) | 6 | 1.082 (1/6) | 2.577 (1/6) | 1.131 (0/5) | 1.124 (0/1) |
| nangate45 | 10 | 1.144 (0/10) | 1.456 (0/10) | 1.231 (0/9) | 1.144 (1/7) |
| asap7 | 10 | 1.118 (1/10) | 1.469 (1/10) | 1.180 (0/10) | 1.164 (0/8) |

## 2.3 Placer time / gpl (median over floorplans)

| placer | t_place / gpl | n |
|---|---|---|
| mcp-TritonPart | 12× | 20 |
| mcp-hMETIS | 7× | 20 |
| mcp-Mt-KaHyPar | 16× | 19 |
| mcp-FastPart | 14× | 15 |

## 2.4 k-way refinement on the AR-1 floorplans (X3)

Source: `x3_mega`, seed 1 (DPO on for the small designs; pre-DPO metric). Same gpl mode.

| floorplan | mcp cutnet / gpl | mcp k-way / gpl | k-way / cutnet |
|---|---|---|---|
| gcd_ar1_asap7 | 1.033 | 1.015 | 0.983 |
| gcd_ar1_nangate45 | 1.107 | 1.078 | 0.974 |
| aes_ar1_asap7 | 1.123 | 1.116 | 0.994 |
| aes_ar1_nangate45 | 1.119 | 1.099 | 0.982 |
| ibex_ar1_asap7 | 1.172 | 1.168 | 0.997 |
| ibex_ar1_nangate45 | 1.119 | 1.111 | 0.993 |
| jpeg_ar1_asap7 | 1.047 | 1.023 | 0.977 |
| jpeg_ar1_nangate45 | 1.166 | 1.130 | 0.969 |
| swerv_wrapper_ar1_asap7 | 1.265 | 1.267 | 1.002 |
| swerv_wrapper_ar1_nangate45 | 1.210 | 1.205 | 0.996 |
| cva6_ar1_asap7 | 1.241 | 1.194 | 0.962 |
| cva6_ar1_nangate45 | 1.243 | 1.200 | 0.965 |
| mempool_tile_ar1_asap7 | 1.192 | 1.177 | 0.987 |
| mempool_tile_ar1_nangate45 | 1.130 | 1.099 | 0.972 |
| xiangshan_ar1_asap7 | 1.053 | 1.043 | 0.990 |
| xiangshan_ar1_nangate45 | 1.210 | 1.185 | 0.980 |
| megaboom_ar1_asap7 | 0.960 | 0.940 | 0.979 |
| megaboom_ar1_nangate45 | 1.122 | 1.114 | 0.993 |
| mempool_group_ar1_asap7 | – | – | – |
| mempool_group_ar1_nangate45 | 1.034 | 1.019 | 0.986 |
| **geomean (wins/n)** | 1.131 (1/19) | 1.112 (1/19) | 0.983 (18/19) |

## 2.5 AR 1 in the floorplan author's DOE (X4): mcp k-way / gpl by design and utilization

Source: `x4_ar_sweep`, AR 1 configurations, macro-free (`REMOVE_MACROS=1`), 3 seeds, both PDKs. Pre-DPO metric.

| design | util 60 | util 70 | util 80 | all (wins/n) |
|---|---|---|---|---|
| gcd | 1.034 | 1.107 | 1.016 | 1.052 (1/6) |
| aes | 1.118 | 1.054 | 1.020 | 1.063 (0/6) |
| ibex | 1.146 | 1.126 | 1.092 | 1.121 (0/6) |
| jpeg | 1.091 | 1.060 | 1.000 | 1.049 (1/6) |
| swerv | 1.185 | 1.191 | 1.174 | 1.184 (0/6) |
| cva6 | 1.192 | 1.187 | 1.175 | 1.185 (0/6) |
| mempool_tile | 1.144 | 1.131 | 1.101 | 1.125 (0/6) |
| xiangshan | 1.091 | 1.064 | 1.066 | 1.074 (0/6) |
| **all** | 1.124 | 1.114 | 1.079 | 1.105 (2/48) |

