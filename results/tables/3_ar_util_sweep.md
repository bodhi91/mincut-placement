# 3. Min-cut placers against gpl on the aspect-ratio × utilization sweep

Source: X4 (`x4_ar_sweep`): the floorplan author's DOE, {gcd, aes, ibex, jpeg, swerv, cva6, mempool_tile, xiangshan} × aspect ratio {0.03, 0.06, 0.09, 1} × utilization {60, 70, 80 %} × {asap7, nangate45}, macro-free, 182 floorplans (10 gcd configurations absent from his tree), 3 seeds. Arms: gpl (krow mode, density = util + 0.10), mcp cutnet, mcp k-way (`-kway_refine_levels {2 3 4 5}`). Metric: HPWL after detailed placement (DPO ran on the small set only; pre-DPO throughout), seed means, ratio to gpl, geomean (wins/n). Full tables: `../x4_ar_sweep/SUMMARY.md`.

## 3.1 Overall

| set | n | mcp cutnet / gpl | mcp k-way / gpl | k-way / cutnet |
|---|---|---|---|---|
| small (gcd aes ibex jpeg) | 86 | 1.026 (28/86) | 1.019 (29/86) | 0.993 (70/86) |
| big (swerv cva6 mempool_tile xiangshan) | 95 | 0.971 (41/95) | 0.956 (46/95) | 0.985 (95/95) |
| all | 181 | 0.997 (69/181) | 0.986 (75/181) | 0.989 (165/181) |
| big, excluding 6 gpl outliers (§3.5) | 89 | 1.029 (35/89) | 1.013 (40/89) | 0.985 (89/89) |

## 3.2 mcp k-way / gpl by aspect ratio × utilization

### Small set

| AR \ util | 60 % | 70 % | 80 % | all |
|---|---|---|---|---|
| 0.03 | 0.974 (4/8) | 0.973 (3/7) | 0.961 (4/6) | 0.970 (11/21) |
| 0.06 | 1.017 (4/8) | 1.001 (2/6) | 0.982 (2/6) | 1.001 (8/20) |
| 0.09 | 1.043 (3/8) | 1.028 (3/7) | 1.016 (2/6) | 1.030 (8/21) |
| 1 | 1.096 (0/8) | 1.087 (0/8) | 1.031 (2/8) | 1.071 (2/24) |
| all | 1.031 (11/32) | 1.024 (8/28) | 1.000 (10/26) | 1.019 (29/86) |

### Big set

| AR \ util | 60 % | 70 % | 80 % | all |
|---|---|---|---|---|
| 0.03 | 0.840 (7/8) | 0.829 (8/8) | 0.824 (8/8) | 0.831 (23/24) |
| 0.06 | 0.949 (4/8) | 0.979 (5/8) | 0.977 (4/8) | 0.968 (13/24) |
| 0.09 | 0.876 (3/8) | 0.860 (5/8) | 1.012 (2/7) | 0.910 (10/23) |
| 1 | 1.152 (0/8) | 1.142 (0/8) | 1.128 (0/8) | 1.141 (0/24) |
| all | 0.947 (14/32) | 0.945 (18/32) | 0.978 (14/31) | 0.956 (46/95) |

### Big set, gpl outliers excluded

| AR \ util | 60 % | 70 % | 80 % | all |
|---|---|---|---|---|
| 0.03 | 0.911 (6/7) | 0.899 (7/7) | 0.895 (7/7) | 0.902 (20/21) |
| 0.06 | 1.007 (3/7) | 0.979 (5/8) | 0.977 (4/8) | 0.987 (12/23) |
| 0.09 | 1.038 (2/7) | 1.015 (4/7) | 1.012 (2/7) | 1.022 (8/21) |
| 1 | 1.152 (0/8) | 1.142 (0/8) | 1.128 (0/8) | 1.141 (0/24) |
| all | 1.028 (11/29) | 1.009 (16/30) | 1.003 (13/30) | 1.013 (40/89) |

## 3.3 mcp k-way / gpl by design × aspect ratio (both PDKs, 3 utilizations)

| design (cells asap7/ng45) | AR 0.03 | AR 0.06 | AR 0.09 | AR 1 | all |
|---|---|---|---|---|---|
| gcd (261/498) | 0.946 (3/3) | 0.974 (2/2) | 0.972 (3/3) | 1.052 (1/6) | 1.000 (9/14) |
| aes (10027/12794) | 1.041 (1/6) | 1.073 (0/6) | 1.071 (0/6) | 1.063 (0/6) | 1.062 (1/24) |
| ibex (17139/13413) | 1.019 (1/6) | 1.067 (0/6) | 1.106 (0/6) | 1.121 (0/6) | 1.077 (1/24) |
| jpeg (45598/53317) | 0.871 (6/6) | 0.885 (6/6) | 0.950 (5/6) | 1.049 (1/6) | 0.936 (18/24) |
| swerv (77438/79088) | 0.928 (6/6) | 0.998 (4/6) | 0.991 (4/6) | 1.184 (0/6) | 1.021 (14/24) |
| cva6 (93993/82923) | 0.979 (5/6) | 1.051 (0/6) | 1.095 (0/6) | 1.185 (0/6) | 1.075 (5/24) |
| mempool_tile (112937/110805) | 0.841 (6/6) | 0.967 (3/6) | 0.994 (3/6) | 1.125 (0/6) | 0.977 (12/24) |
| xiangshan (411571/290236) | 0.624 (6/6) | 0.867 (6/6) | 0.590 (3/5) | 1.074 (0/6) | 0.774 (15/23) |

## 3.4 Rows: the same ratios against core rows

| rows | n | mcp cutnet / gpl | mcp k-way / gpl | designs |
|---|---|---|---|---|
| 1–8 | 8 | 0.965 (8/8) | 0.963 (8/8) | gcd |
| 9–32 | 22 | 1.050 (3/22) | 1.044 (3/22) | gcd, aes, ibex |
| 33–64 | 35 | 0.998 (15/35) | 0.989 (15/35) | aes, ibex, jpeg, swerv, cva6, mempool_tile |
| 65–128 | 51 | 0.982 (27/51) | 0.970 (33/51) | aes, ibex, jpeg, swerv, cva6, mempool_tile, xiangshan |
| 129–256 | 34 | 0.969 (12/34) | 0.957 (12/34) | aes, ibex, jpeg, swerv, cva6, mempool_tile, xiangshan |
| ≥ 257 | 31 | 1.024 (4/31) | 1.007 (4/31) | jpeg, swerv, cva6, mempool_tile, xiangshan |

## 3.5 gpl outliers, failures, seeds, runtime

gpl outliers (k-way / gpl < 0.7): all xiangshan asap7 at AR ≤ 0.09, where RePlAce returns a legal but badly spread placement (the nangate45 twins are normal). Shown with and without above.

| floorplan | rows | gpl HPWL | k-way HPWL | k-way / gpl |
|---|---|---|---|---|
| sweep_xiangshan_ar0p03_util60_asap7 | 191 | 5,901,914 | 2,805,457 | 0.475 |
| sweep_xiangshan_ar0p03_util70_asap7 | 176 | 5,554,131 | 2,604,344 | 0.469 |
| sweep_xiangshan_ar0p03_util80_asap7 | 165 | 5,382,930 | 2,483,116 | 0.461 |
| sweep_xiangshan_ar0p06_util60_asap7 | 270 | 3,533,670 | 2,212,947 | 0.626 |
| sweep_xiangshan_ar0p09_util60_asap7 | 331 | 7,459,027 | 1,999,311 | 0.268 |
| sweep_xiangshan_ar0p09_util70_asap7 | 306 | 7,042,787 | 1,893,564 | 0.269 |

Failures: 3 (gpl on `xiangshan_ar0.09_util80_asap7`, seeds 1–3, DPL-0033 after RePlAce). All 1 092 mcp runs legal.

| design | gpl t_place (s) | k-way t_place (s) | ratio | k-way seed spread (max/min − 1, median) |
|---|---|---|---|---|
| gcd | 0.4 | 2 | 5× | 1.3 % |
| aes | 4.6 | 53 | 12× | 2.4 % |
| ibex | 5.5 | 69 | 13× | 1.4 % |
| jpeg | 16.0 | 172 | 11× | 1.8 % |
| swerv | 33.4 | 364 | 11× | 3.0 % |
| cva6 | 35.9 | 393 | 11× | 1.5 % |
| mempool_tile | 42.3 | 476 | 11× | 1.6 % |
| xiangshan | 167.2 | 8396 | 50× | 1.1 % |

gpl is deterministic (seed has no effect). Legalization costs gpl 6.7 % (big set) / 14.5 % (small set) of HPWL at the median, mcp 0.01 %; DPO (small set) helps gpl −7.7 % vs mcp −3.1 %, so on post-DPO HPWL the small-set ratio is 1.072 instead of 1.019.

