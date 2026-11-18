# 5. American maps: connectivity versus geometry in one tree (AM1)

Source: `am1` (`exp/am1.cfg`, `AMERICAN_MAPS_STUDY.md`). Six floorplans — three macro-free squares (aes/ng45 20 k, ibex/asap7 20 k, jpeg/ng45 65 k cells), one square with fixed macros (swerv_wrapper/ng45, 100 k) and two k-row floorplans with the same netlists as the ibex / jpeg squares (ibex 8 rows, jpeg 4 rows) — 3 seeds, x3 conditions (DPO on, routed, 4 threads). **Metric in this table: post-DPO HPWL / gpl** (the study's definition); the pre-DPO ratio is given in parentheses in 5.1. gpl = krow mode.

Arms: `X` min-cut from scratch (= mcp cutnet), `X_kway` (+ k-way refinement), `AM2_t30` the American map (a `gpl -overflow 0.3` seed assigned into the tree's regions by capacitated min-displacement transport at every level, min-cut nowhere), `MC_diag` min-cut decides at every level with the seed as terminals, `seed` the `gpl -overflow 0.3` placement legalized on its own (what the seed is worth without the tree), `AMx` / `AMg` / `AMc` the American map seeded by X's own result / the final gpl placement / Capo, `AM4_t30` quadrisection, `AMl_t30` net-aware transport (λ 0.5), `AM2_t50` / `AM2_t70` cruder seeds (`seed50` / `seed70` the matching controls).

## 5.1 Core arms, per floorplan (post-DPO HPWL / gpl; pre-DPO in parentheses; n = seeds if ≠ 3)

| floorplan | seed | X | X_kway | AM2_t30 | MC_diag |
|---|---|---|---|---|---|
| aes_ar1_nangate45 | 1.023 (1.038) | 1.166 (1.135) | 1.155 (1.113) | 1.038 (1.009) | 1.138 (1.102) |
| ibex_ar1_asap7 | 1.063 (1.070) | 1.201 (1.182) | 1.184 (1.160) | 1.062 (1.045) | 1.160 (1.138) |
| jpeg_ar1_nangate45 | 1.057 (1.093) | 1.254 (1.164) | 1.247 (1.156) | 1.077 (1.068) | 1.278 (1.187) |
| swerv_wrapper_ar1_nangate45 | 1.029 (1.052) | 1.237 (1.223) | 1.220 (1.195) | 1.468 (2.316) | 1.203 (1.183) |
| ibex_u70_r8_asap7 | – | 0.794 (0.692) | 0.795 (0.693) | 1.044 (1.090) | 0.795 (0.693) |
| jpeg_u70_r4_nangate45 | – | 0.610 (0.477) | 0.606 (0.475) | 1.040 (1.138) | 0.609 (0.477) |

`seed` failed legalization (DPL-0033, 3/3 seeds) on both k-row floorplans: the gpl τ 0.3 seed is not legalizable there on its own, yet the American map built from it is.

## 5.2 Geomeans over floorplan groups

| group | n | seed | X | X_kway | AM2_t30 | MC_diag |
|---|---|---|---|---|---|---|
| 3 macro-free squares | 3 | 1.047 | 1.206 | 1.195 | 1.059 | 1.190 |
| square with macros (swerv_wrapper) | 1 | 1.029 | 1.237 | 1.220 | 1.468 | 1.203 |
| 2 k-row floorplans | 2 | – | 0.696 | 0.694 | 1.042 | 0.696 |
| all 6 | 6 | 1.043 (4) | 1.008 | 1.000 | 1.112 | 0.997 |

## 5.3 Where does the American map's result come from: seed quality and seed source

| floorplan | seed | AM2_t30 | seed50 | AM2_t50 | seed70 | AM2_t70 | AMg | AMx | AMc |
|---|---|---|---|---|---|---|---|---|---|
| aes_ar1_nangate45 | 1.023 (1.038) | 1.038 (1.009) | 1.067 (1.225) | 1.075 (1.125) | 1.252 (2.011) | 1.222 (1.681) | 1.037 (0.995) | 1.166 (1.135) | 1.152 (1.129) |
| ibex_ar1_asap7 | 1.063 (1.070) | 1.062 (1.045) | 1.082 (1.202) | 1.113 (1.214) | 1.326 (2.057) | 1.236 (1.985) | 1.056 (1.032) | 1.201 (1.182) | 1.236 (1.235) |
| jpeg_ar1_nangate45 | 1.057 (1.093) | 1.077 (1.068) | 1.165 (1.442) | 1.268 (1.610) | – | 1.950 (4.483) | 1.025 (0.956) | 1.254 (1.164) | 1.259 (1.197) |
| swerv_wrapper_ar1_nangate45 | 1.029 (1.052) | 1.468 (2.316) | 1.037 (1.169) | 1.635 (3.020) | – | 2.098 (3.737) | 1.528 (2.458) | 1.237 (1.223) | 1.552 (2.125) |
| ibex_u70_r8_asap7 | – | 1.044 (1.090) | – | 1.185 (1.947) | – | 1.734 (4.177) | 0.919 (0.804) | 0.794 (0.692) | 0.825 (0.725) |
| jpeg_u70_r4_nangate45 | – | 1.040 (1.138) | – | 2.652 (7.152) | – | 3.117 (8.363) | 0.933 (0.788) | 0.610 (0.477) | 0.644 (0.506) |

Read `AM2_tτ − seedτ` as the transport's own contribution at each seed quality; `AMg` is the map of the finished gpl placement (seed quality ceiling); `AMx` maps X's own placement (faithfulness control: an American map adds nothing to a min-cut result).

## 5.4 Variants of the map: quadrisection and net-aware transport

| floorplan | AM2_t30 | AM4_t30 | AMl_t30 |
|---|---|---|---|
| aes_ar1_nangate45 | 1.038 (1.009) | 1.035 (1.017) | 1.039 (1.003) |
| ibex_ar1_asap7 | 1.062 (1.045) | 1.062 (1.046) | 1.062 (1.042) |
| jpeg_ar1_nangate45 | 1.077 (1.068) | 1.086 (1.110) | 1.076 (1.038) |
| swerv_wrapper_ar1_nangate45 | 1.468 (2.316) | 1.542 (2.340) | 1.385 (1.656) |
| ibex_u70_r8_asap7 | 1.044 (1.090) | 1.091 (1.367) | 0.999 (0.890) |
| jpeg_u70_r4_nangate45 | 1.040 (1.138) | 1.028 (1.216) | 1.000 (0.817) |

## 5.5 One level decided by min-cut inside the American map (`AM2_notL` / `AM2_t30`)

Each column: the American map everywhere except level L, where min-cut decides; ratio to the pure American map (> 1: the min-cut decision at that level costs HPWL; < 1: it repairs the seed).

| floorplan | L1 | L2 | L3 | L4 | L5 | L6 |
|---|---|---|---|---|---|---|
| aes_ar1_nangate45 | 1.012 (1.065) | 1.063 (1.190) | 1.023 (1.045) | 1.043 (1.084) | 1.012 (1.024) | 1.008 (1.010) |
| ibex_ar1_asap7 | 1.050 (1.223) | 1.137 (1.400) | 1.015 (1.072) | 1.022 (1.056) | 1.012 (1.022) | 1.012 (1.023) |
| jpeg_ar1_nangate45 | 1.169 (1.838) | 1.194 (1.717) | 1.025 (1.104) | 1.084 (1.241) | 1.022 (1.062) | 1.044 (1.097) |
| swerv_wrapper_ar1_nangate45 | 1.053 (0.999) | 1.037 (1.021) | 0.963 (1.068) | 0.926 (0.943) | 0.963 (0.903) | 0.958 (0.929) |
| ibex_u70_r8_asap7 | 1.029 (1.247) | 0.965 (0.987) | 0.958 (0.922) | 0.973 (0.934) | 0.974 (0.920) | 0.989 (0.926) |
| jpeg_u70_r4_nangate45 | 1.427 (2.567) | 1.041 (1.253) | 0.962 (0.965) | 0.946 (0.879) | 0.953 (0.848) | 0.970 (0.888) |

## 5.6 American map above level L, min-cut below (`AM2_LL` / gpl)

| floorplan | AM at every level | AM above L2 | AM above L4 | AM above L6 | min-cut everywhere (X) |
|---|---|---|---|---|---|
| aes_ar1_nangate45 | 1.038 (1.009) | 1.120 (1.084) | 1.097 (1.067) | 1.049 (1.011) | 1.166 (1.135) |
| ibex_ar1_asap7 | 1.062 (1.045) | 1.158 (1.136) | 1.096 (1.075) | 1.079 (1.058) | 1.201 (1.182) |
| jpeg_ar1_nangate45 | 1.077 (1.068) | 1.214 (1.128) | 1.176 (1.093) | 1.126 (1.048) | 1.254 (1.164) |
| swerv_wrapper_ar1_nangate45 | 1.468 (2.316) | 1.198 (1.174) | 1.188 (1.170) | 1.341 (1.521) | 1.237 (1.223) |
| ibex_u70_r8_asap7 | 1.044 (1.090) | 0.854 (0.745) | 0.941 (0.837) | 1.017 (0.954) | 0.794 (0.692) |
| jpeg_u70_r4_nangate45 | 1.040 (1.138) | 0.740 (0.582) | 0.840 (0.667) | 0.979 (0.867) | 0.610 (0.477) |

## 5.7 Routed wirelength and placer time (/ gpl)

| floorplan | X: WL / time | X_kway: WL / time | AM2_t30: WL / time | MC_diag: WL / time |
|---|---|---|---|---|
| aes_ar1_nangate45 | 1.105 / 11.8× | 1.102 / 12.8× | 1.022 / 11.5× | 1.088 / 13.1× |
| ibex_ar1_asap7 | 1.148 / 13.5× | 1.128 / 14.4× | 1.049 / 13.7× | 1.099 / 12.7× |
| jpeg_ar1_nangate45 | 1.167 / 8.5× | 1.163 / 9.6× | 1.061 / 9.6× | 1.182 / 9.3× |
| swerv_wrapper_ar1_nangate45 | 1.394 / 6.5× | 1.416 / 7.2× | 1.561 / 6.5× | 1.428 / 7.6× |
| ibex_u70_r8_asap7 | 0.785 / 9.9× | 0.786 / 12.4× | 1.171 / 14.1× | 0.786 / 10.9× |
| jpeg_u70_r4_nangate45 | 0.783 / 6.8× | 0.780 / 7.1× | 1.067 / 9.0× | 0.783 / 7.9× |

The full analysis with failure accounting, the per-level disagreement diagnostic and the transport's share of runtime: `python3 exp/american_maps.py results/am1` (`../am1/am1_analysis.md`).

