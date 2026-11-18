# X4 — mcp on the floorplan author's aspect-ratio × utilization DOE

1635 runs, 182 floorplans (181 with a gpl baseline). HPWL = after detailed placement, before DPO. Ratio = seed mean of the arm / seed mean of gpl on the same floorplan; tables give the geometric mean over floorplans (n in parentheses). Wins = floorplans where the arm's HPWL is lower than gpl's.

## Overall

| set | designs | floorplans | k-way / gpl | wins | cutnet / gpl | wins | k-way / cutnet | wins |
|---|---|---|---|---|---|---|---|---|
| small | gcd, aes, ibex, jpeg | 86 | **1.019** | 29/86 | 1.026 | 28/86 | 0.993 | 70/86 |
| big | swerv, cva6, mempool_tile, xiangshan | 95 | **0.956** | 46/95 | 0.971 | 41/95 | 0.985 | 95/95 |
| all | gcd, aes, ibex, jpeg, swerv, cva6, mempool_tile, xiangshan | 181 | **0.986** | 75/181 | 0.997 | 69/181 | 0.989 | 165/181 |
| big, excl. gpl outliers | (k-way/gpl ≥ 0.7, see below) | 89 | **1.013** | 40/89 | 1.029 | 35/89 | 0.985 | 89/89 |
| all, excl. gpl outliers | (k-way/gpl ≥ 0.7, see below) | 175 | **1.016** | 69/175 | 1.027 | 63/175 | 0.989 | 159/175 |

Big set by AR, excluding the gpl outliers: AR 0.03: 0.902 (20/21); AR 0.06: 0.987 (12/23); AR 0.09: 1.022 (8/21); AR 1.0: 1.141 (0/24).

## k-way / gpl by aspect ratio × utilization

### small set (gcd aes ibex jpeg)

| AR \ util | 60 | 70 | 80 | all |
|---|---|---|---|---|
| 0.03 | 0.974 (4/8) | 0.973 (3/7) | 0.961 (4/6) | 0.970 (11/21) |
| 0.06 | 1.017 (4/8) | 1.001 (2/6) | 0.982 (2/6) | 1.001 (8/20) |
| 0.09 | 1.043 (3/8) | 1.028 (3/7) | 1.016 (2/6) | 1.030 (8/21) |
| 1.0 | 1.096 (0/8) | 1.087 (0/8) | 1.031 (2/8) | 1.071 (2/24) |
| all | 1.031 (11/32) | 1.024 (8/28) | 1.000 (10/26) | 1.019 (29/86) |

### big set (swerv cva6 mempool_tile xiangshan)

| AR \ util | 60 | 70 | 80 | all |
|---|---|---|---|---|
| 0.03 | 0.840 (7/8) | 0.829 (8/8) | 0.824 (8/8) | 0.831 (23/24) |
| 0.06 | 0.949 (4/8) | 0.979 (5/8) | 0.977 (4/8) | 0.968 (13/24) |
| 0.09 | 0.876 (3/8) | 0.860 (5/8) | 1.012 (2/7) | 0.910 (10/23) |
| 1.0 | 1.152 (0/8) | 1.142 (0/8) | 1.128 (0/8) | 1.141 (0/24) |
| all | 0.947 (14/32) | 0.945 (18/32) | 0.978 (14/31) | 0.956 (46/95) |

## k-way / gpl by design × aspect ratio (both PDKs, 3 utilizations)

| design | cells (asap7 / ng45) | AR 0.03 | AR 0.06 | AR 0.09 | AR 1.0 | all |
|---|---|---|---|---|---|---|
| gcd | 261 / 498 | 0.946 (3/3) | 0.974 (2/2) | 0.972 (3/3) | 1.052 (1/6) | 1.000 (9/14) |
| aes | 10027 / 12794 | 1.041 (1/6) | 1.073 (0/6) | 1.071 (0/6) | 1.063 (0/6) | 1.062 (1/24) |
| ibex | 17139 / 13413 | 1.019 (1/6) | 1.067 (0/6) | 1.106 (0/6) | 1.121 (0/6) | 1.077 (1/24) |
| jpeg | 45598 / 53317 | 0.871 (6/6) | 0.885 (6/6) | 0.950 (5/6) | 1.049 (1/6) | 0.936 (18/24) |
| swerv | 77438 / 79088 | 0.928 (6/6) | 0.998 (4/6) | 0.991 (4/6) | 1.184 (0/6) | 1.021 (14/24) |
| cva6 | 93993 / 82923 | 0.979 (5/6) | 1.051 (0/6) | 1.095 (0/6) | 1.185 (0/6) | 1.075 (5/24) |
| mempool_tile | 112937 / 110805 | 0.841 (6/6) | 0.967 (3/6) | 0.994 (3/6) | 1.125 (0/6) | 0.977 (12/24) |
| xiangshan | 411571 / 290236 | 0.624 (6/6) | 0.867 (6/6) | 0.590 (3/5) | 1.074 (0/6) | 0.774 (15/23) |

## k-way / gpl by design × aspect ratio, per PDK

### asap7

| design | AR 0.03 | AR 0.06 | AR 0.09 | AR 1.0 | rows at AR 0.03 … 1 |
|---|---|---|---|---|---|
| gcd | 0.955 (1/1) | 0.990 (1/1) | 0.972 (2/2) | 0.990 (1/3) | 4, 6, 7, 23–27 |
| aes | 1.065 (0/3) | 1.078 (0/3) | 1.078 (0/3) | 1.069 (0/3) | 24–27, 34–39, 42–48, 141–163 |
| ibex | 1.005 (1/3) | 1.071 (0/3) | 1.067 (0/3) | 1.148 (0/3) | 32–38, 46–54, 57–66, 192–222 |
| jpeg | 0.865 (3/3) | 0.859 (3/3) | 0.913 (3/3) | 0.999 (1/3) | 54–63, 77–89, 95–110, 319–368 |
| swerv | 0.939 (3/3) | 1.003 (1/3) | 1.014 (1/3) | 1.188 (0/3) | 79–91, 112–130, 138–159, 461–533 |
| cva6 | 0.974 (3/3) | 1.057 (0/3) | 1.075 (0/3) | 1.183 (0/3) | 77–89, 109–126, 133–154, 447–516 |
| mempool_tile | 0.893 (3/3) | 1.022 (0/3) | 1.001 (1/3) | 1.135 (0/3) | 84–97, 119–137, 146–168, 488–563 |
| xiangshan | 0.468 (3/3) | 0.792 (3/3) | 0.268 (2/2) | 1.037 (0/3) | 165–191, 234–270, 287–331, 958–1106 |

### nangate45

| design | AR 0.03 | AR 0.06 | AR 0.09 | AR 1.0 | rows at AR 0.03 … 1 |
|---|---|---|---|---|---|
| gcd | 0.942 (2/2) | 0.958 (1/1) | 0.971 (1/1) | 1.117 (0/3) | 3, 4, 6, 19–22 |
| aes | 1.017 (1/3) | 1.068 (0/3) | 1.063 (0/3) | 1.057 (0/3) | 17–20, 24–28, 30–35, 103–119 |
| ibex | 1.033 (0/3) | 1.064 (0/3) | 1.146 (0/3) | 1.095 (0/3) | 22–25, 31–36, 38–44, 130–150 |
| jpeg | 0.878 (3/3) | 0.912 (3/3) | 0.989 (2/3) | 1.102 (0/3) | 39–45, 56–65, 69–79, 231–267 |
| swerv | 0.918 (3/3) | 0.992 (3/3) | 0.969 (3/3) | 1.179 (0/3) | 52–61, 74–86, 91–106, 307–355 |
| cva6 | 0.984 (2/3) | 1.044 (0/3) | 1.115 (0/3) | 1.186 (0/3) | 52–60, 74–86, 91–105, 305–352 |
| mempool_tile | 0.791 (3/3) | 0.916 (3/3) | 0.988 (2/3) | 1.116 (0/3) | 57–66, 81–93, 99–115, 333–384 |
| xiangshan | 0.830 (3/3) | 0.949 (3/3) | 0.998 (1/3) | 1.112 (0/3) | 111–129, 158–182, 194–224, 648–748 |

## Ratio versus number of rows

Rows = core height / site height. The k-row study (E17) covered 1–8 rows; this DOE starts at 3–4 rows (gcd) and reaches a few thousand (xiangshan at AR 1).

| rows | floorplans | k-way / gpl | wins | cutnet / gpl | designs |
|---|---|---|---|---|---|
| 1–4 | 4 | 0.949 | 4/4 | 0.947 | gcd |
| 5–8 | 4 | 0.976 | 4/4 | 0.983 | gcd |
| 17–32 | 22 | 1.044 | 3/22 | 1.050 | gcd, aes, ibex |
| 33–64 | 35 | 0.989 | 15/35 | 0.998 | aes, ibex, jpeg, swerv, cva6, mempool_tile |
| 65–128 | 51 | 0.970 | 33/51 | 0.982 | aes, ibex, jpeg, swerv, cva6, mempool_tile, xiangshan |
| 129–256 | 34 | 0.957 | 12/34 | 0.969 | aes, ibex, jpeg, swerv, cva6, mempool_tile, xiangshan |
| ≥257 | 31 | 1.007 | 4/31 | 1.024 | jpeg, swerv, cva6, mempool_tile, xiangshan |

## Seed spread (max/min − 1 over 3 seeds, median over floorplans)

| set | gpl | cutnet | k-way |
|---|---|---|---|
| small | 0.00 % | 1.80 % | 1.83 % |
| big | 0.00 % | 1.82 % | 1.69 % |

## Placer runtime (seconds, 4 threads, mean over floorplans and seeds)

| design | gpl | cutnet | k-way | k-way / gpl |
|---|---|---|---|---|
| gcd | 0.5 | 2 | 2 | 5× |
| aes | 4.8 | 55 | 53 | 11× |
| ibex | 5.6 | 63 | 70 | 12× |
| jpeg | 16.2 | 163 | 173 | 11× |
| swerv | 33.6 | 354 | 366 | 11× |
| cva6 | 36.3 | 341 | 395 | 11× |
| mempool_tile | 42.5 | 417 | 478 | 11× |
| xiangshan | 168.6 | 8913 | 8936 | 53× |

## gpl outliers (k-way / gpl < 0.7)

| floorplan | rows | gpl HPWL (min–max over seeds) | k-way HPWL | k-way / gpl |
|---|---|---|---|---|
| sweep_xiangshan_ar0p03_util60_asap7 | 191 | 5,901,914 (5,901,914–5,901,914) | 2,805,457 | 0.475 |
| sweep_xiangshan_ar0p03_util70_asap7 | 176 | 5,554,131 (5,554,131–5,554,131) | 2,604,344 | 0.469 |
| sweep_xiangshan_ar0p03_util80_asap7 | 165 | 5,382,930 (5,382,930–5,382,930) | 2,483,116 | 0.461 |
| sweep_xiangshan_ar0p06_util60_asap7 | 270 | 3,533,670 (3,533,670–3,533,670) | 2,212,947 | 0.626 |
| sweep_xiangshan_ar0p09_util60_asap7 | 331 | 7,459,027 (7,459,027–7,459,027) | 1,999,311 | 0.268 |
| sweep_xiangshan_ar0p09_util70_asap7 | 306 | 7,042,787 (7,042,787–7,042,787) | 1,893,564 | 0.269 |

## Failures

* `sweep_xiangshan_ar0p09_util80_asap7__gpl__s1` — [ERROR DPL-0033] detailed placement checks failed during check placement.
* `sweep_xiangshan_ar0p09_util80_asap7__gpl__s2` — [ERROR DPL-0033] detailed placement checks failed during check placement.
* `sweep_xiangshan_ar0p09_util80_asap7__gpl__s3` — [ERROR DPL-0033] detailed placement checks failed during check placement.

## Cross-check against the floorplan author's RePlAce baseline (`summary.csv`, ar0.03_util60 only)

His flow: `global_placement -routability_driven` + detailed placement, one run. Ours (`gpl`): `-disable_pin_density_adjust -overflow 0.2`, 3 seeds.

| design | pdk | the floorplan author HPWL | our gpl HPWL (mean) | ours / the floorplan author | k-way / the floorplan author |
|---|---|---|---|---|---|
| aes | asap7 | 48,119 | 44,868 | 0.932 | 1.019 |
| gcd | asap7 | 984 | 1,016 | 1.032 | 0.985 |
| ibex | asap7 | 106,463 | 99,511 | 0.935 | 0.953 |
| jpeg | asap7 | 190,853 | 180,936 | 0.948 | 0.802 |
| swerv | asap7 | 1,023,883 | 907,282 | 0.886 | 0.839 |
| aes | nangate45 | 247,211 | 213,544 | 0.864 | 0.902 |
| gcd | nangate45 | 5,857 | 5,998 | 1.024 | 0.968 |
| ibex | nangate45 | 333,311 | 272,092 | 0.816 | 0.853 |
| jpeg | nangate45 | 1,012,615 | 955,744 | 0.944 | 0.821 |

