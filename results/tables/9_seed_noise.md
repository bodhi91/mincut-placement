# 9. Seed sensitivity (X4 sweep, 3 seeds)

The paper reports seed 1 everywhere. This table measures, on the one experiment run with three seeds per placer, how much the seed moves the results: the 182 floorplans of the aspect-ratio x utilization sweep (§5; macro-free designs, AR 0.03 / 0.06 / 0.09 / 1 x utilization 60 / 70 / 80 % x NanGate45 / ASAP7), RePlAce, RB (TP) and RB+k (TP), HPWL after detailed placement (no routing), seeds 1, 2, 3. Source: `x4_ar_sweep/runs.csv`; regenerate with `python3 exp/seed_noise.py`.

CV = population standard deviation / mean over the three seeds of one floorplan; spread = (max − min) / mean.

## 9.1 Per-floorplan seed noise by placer

| placer | floorplans (3 seeds) | CV median | CV mean | CV max | spread median | spread 90th pct | spread max | floorplans with spread > 5 % |
|---|---|---|---|---|---|---|---|---|
| RePlAce | 181 | 0.00 % | 0.00 % | 0.00 % | 0.00 % | 0.00 % | 0.00 % | 0 |
| RB (TP) | 182 | 0.79 % | 0.97 % | 7.55 % | 1.78 % | 4.22 % | 16.39 % | 14 |
| RB+k (TP) | 182 | 0.75 % | 0.92 % | 7.39 % | 1.74 % | 3.85 % | 16.51 % | 12 |

RePlAce is deterministic in this flow: the three seeds give the same placement on every floorplan.

## 9.2.1 RB (TP) and RB+k (TP) seed spread by design

| design | n | RB (TP) CV median | RB (TP) spread median | RB (TP) spread max | RB+k CV median | RB+k spread median | RB+k spread max |
|---|---|---|---|---|---|---|---|
| aes | 24 | 0.90 % | 2.09 % | 5.5 % | 0.98 % | 2.38 % | 5.0 % |
| cva6 | 24 | 0.74 % | 1.69 % | 7.0 % | 0.65 % | 1.47 % | 5.9 % |
| gcd | 14 | 0.73 % | 1.75 % | 6.6 % | 0.51 % | 1.25 % | 5.0 % |
| ibex | 24 | 0.62 % | 1.43 % | 16.4 % | 0.59 % | 1.39 % | 16.5 % |
| jpeg | 24 | 0.74 % | 1.67 % | 4.6 % | 0.78 % | 1.81 % | 4.0 % |
| mempool_tile | 24 | 0.80 % | 1.85 % | 4.6 % | 0.67 % | 1.54 % | 4.9 % |
| swerv | 24 | 1.55 % | 3.57 % | 9.4 % | 1.27 % | 3.01 % | 8.8 % |
| xiangshan | 24 | 0.38 % | 0.92 % | 4.2 % | 0.45 % | 1.05 % | 3.0 % |

## 9.2.2 RB (TP) and RB+k (TP) seed spread by aspect ratio

| aspect ratio | n | RB (TP) CV median | RB (TP) spread median | RB (TP) spread max | RB+k CV median | RB+k spread median | RB+k spread max |
|---|---|---|---|---|---|---|---|
| 0.03 | 45 | 0.90 % | 2.16 % | 6.4 % | 0.80 % | 1.90 % | 6.2 % |
| 0.06 | 44 | 0.71 % | 1.54 % | 9.4 % | 0.63 % | 1.51 % | 7.2 % |
| 0.09 | 45 | 0.76 % | 1.83 % | 16.4 % | 0.70 % | 1.60 % | 16.5 % |
| 1 | 48 | 0.83 % | 1.88 % | 6.6 % | 0.88 % | 2.12 % | 7.2 % |

## 9.2.3 RB (TP) and RB+k (TP) seed spread by utilization

| utilization | n | RB (TP) CV median | RB (TP) spread median | RB (TP) spread max | RB+k CV median | RB+k spread median | RB+k spread max |
|---|---|---|---|---|---|---|---|
| 60 | 64 | 0.78 % | 1.88 % | 8.7 % | 0.82 % | 1.95 % | 7.1 % |
| 70 | 60 | 0.69 % | 1.59 % | 9.0 % | 0.75 % | 1.74 % | 8.8 % |
| 80 | 58 | 0.83 % | 1.88 % | 16.4 % | 0.72 % | 1.71 % | 16.5 % |

## 9.2.4 RB (TP) and RB+k (TP) seed spread by PDK

| PDK | n | RB (TP) CV median | RB (TP) spread median | RB (TP) spread max | RB+k CV median | RB+k spread median | RB+k spread max |
|---|---|---|---|---|---|---|---|
| asap7 | 91 | 0.74 % | 1.76 % | 9.4 % | 0.74 % | 1.71 % | 8.8 % |
| nangate45 | 91 | 0.79 % | 1.83 % | 16.4 % | 0.78 % | 1.75 % | 16.5 % |

## 9.3 Stability of the comparison with RePlAce

| placer | geomean / RePlAce, seed 1 (wins/n) | seed 2 | seed 3 | geomean range | floorplans whose win/loss vs RePlAce depends on the seed |
|---|---|---|---|---|---|
| RB (TP) | 0.9951 (68/181) | 0.9995 (73/181) | 0.9960 (66/181) | 0.44 % | 15 / 181 |
| RB+k (TP) | 0.9844 (77/181) | 0.9874 (74/181) | 0.9855 (72/181) | 0.30 % | 15 / 181 |

The geometric-mean ratio moves by a few tenths of a percent between seeds; the win/loss sign changes only on floorplans whose ratio is close to 1.

## 9.4 Floorplans with the largest RB+k seed spread

| floorplan | design | AR | util % | PDK | CV | spread | RB+k / RePlAce by seed |
|---|---|---|---|---|---|---|---|
| sweep_ibex_ar0p09_util80_nangate45 | ibex | 0.09 | 80 | nangate45 | 7.39 % | 16.5 % | 1.103 / 1.297 / 1.124 |
| sweep_swerv_ar0p09_util70_asap7 | swerv | 0.09 | 70 | asap7 | 3.99 % | 8.8 % | 0.994 / 1.077 / 0.987 |
| sweep_swerv_ar0p06_util80_asap7 | swerv | 0.06 | 80 | asap7 | 3.28 % | 7.2 % | 0.956 / 1.028 / 1.023 |
| sweep_swerv_ar1_util80_asap7 | swerv | 1 | 80 | asap7 | 2.97 % | 7.2 % | 1.138 / 1.223 / 1.192 |
| sweep_swerv_ar0p09_util60_asap7 | swerv | 0.09 | 60 | asap7 | 2.96 % | 7.1 % | 1.016 / 1.091 / 1.041 |
| sweep_ibex_ar0p03_util80_asap7 | ibex | 0.03 | 80 | asap7 | 2.78 % | 6.2 % | 1.019 / 0.966 / 0.958 |
| sweep_cva6_ar0p06_util70_nangate45 | cva6 | 0.06 | 70 | nangate45 | 2.47 % | 5.9 % | 1.030 / 0.987 / 1.047 |
| sweep_swerv_ar0p06_util80_nangate45 | swerv | 0.06 | 80 | nangate45 | 2.33 % | 5.6 % | 0.999 / 0.963 / 1.019 |
| sweep_swerv_ar0p06_util60_asap7 | swerv | 0.06 | 60 | asap7 | 2.61 % | 5.5 % | 1.067 / 1.010 / 1.009 |
| sweep_cva6_ar0p03_util70_nangate45 | cva6 | 0.03 | 70 | nangate45 | 2.22 % | 5.4 % | 0.982 / 0.948 / 1.001 |
| sweep_swerv_ar0p03_util70_nangate45 | swerv | 0.03 | 70 | nangate45 | 2.27 % | 5.2 % | 0.953 / 0.904 / 0.944 |
| sweep_aes_ar0p09_util70_nangate45 | aes | 0.09 | 70 | nangate45 | 2.17 % | 5.0 % | 1.089 / 1.078 / 1.036 |

Of the 12 floorplans with an RB+k spread above 5 %, 7 are swerv, 10 are at 70–80 % utilization and 11 have AR ≤ 0.09. On these the three seeds also disagree on the comparison with RePlAce (last column), so single-seed results on narrow dense floorplans of swerv-size designs carry a few percent of seed noise; the aggregates of 9.3 do not.
