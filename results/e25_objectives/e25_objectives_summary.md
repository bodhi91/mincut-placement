# E25 partitioner objectives: HPWL relative to gpl

HPWL after detailed placement, before DPO (hpwl_dpl), mean over seeds, divided by gpl on the same floorplan.  Geomean over the floorplans that have the arm (k-row / base / all).

| arm | variant | aes_u70_r2/ng45 | ibex_u70_r6/ng45 | jpeg_u70_r8/asap7 | swerv_u60_r1/ng45 | cva6_u70_r2/ng45 | aes/ng45 | ibex/asap7 | cva6/asap7 | mempool_tile/ng45 | geomean k-row | geomean base | geomean all |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ref gpl (RePlAce) | gpl | 1.000 | 1.000 | 1.000 | 1.000 | 1.000 | 1.000 | 1.000 | 1.000 | 1.000 | 1.000 | 1.000 | 1.000 |
| 0 cutnet (control) [tp] | cutnet_tp | 0.994 | 0.932 | 0.406 | 1.088 | 0.911 | 1.135 | 1.182 | 1.241 | 1.130 | 0.821 | 1.171 | 0.961 |
| 0 cutnet (control) [hm] | cutnet_hm | 0.977 | 1.087 | 0.399 | 1.032 | 0.866 | 1.124 | 1.199 | 1.205 | 1.092 | 0.824 | 1.154 | 0.957 |
| 0 cutnet (control) [mk] | cutnet_mk | 1.033 | 1.011 | 0.465 | 1.133 | 0.959 | 1.153 | 1.238 | 1.262 | 1.367 | 0.880 | 1.253 | 1.029 |
| 0 cutnet (control) [fp] | cutnet_fp | 0.937 | 0.914 | 0.447 | 9.859 | 12.217 | 1.122 | 1.162 | 1.293 | 1.179 | 2.152 | 1.188 | 1.652 |
| 1 net weight exphpwl [tp] | exphpwl_tp | 0.980 | 0.930 | 0.398 | 1.077 | 0.932 | 1.145 | 1.219 | 1.194 | 1.127 | 0.817 | 1.171 | 0.958 |
| 1 net weight exphpwl [hm] | exphpwl_hm | 1.016 | 1.038 | 0.392 | 1.046 | 0.907 | 1.140 | 1.246 | 1.201 | 1.108 | 0.829 | 1.173 | 0.967 |
| 1 net weight exphpwl [mk] | exphpwl_mk | 1.160 | 1.103 | 0.585 | 1.308 | 1.026 | 1.235 | 1.345 | 1.467 | 1.456 | 1.001 | 1.372 | 1.152 |
| 1 net weight exphpwl [fp] | exphpwl_fp | 0.959 | 0.928 | 0.456 | 8.866 | 0.965 | 1.133 | 1.230 | 1.351 | 1.306 | 1.283 | 1.252 | 1.269 |
| 1 net weight 1/(d-1) [tp] | invdeg_tp | 1.230 | 1.158 | 0.532 | 1.448 | 1.132 | 1.286 | 1.374 | 1.403 | 1.369 | 1.044 | 1.357 | 1.173 |
| 1 net weight 1/(d-1) [hm] | invdeg_hm | 1.170 | 1.309 | 2.226 | 9.602 | 10.093 | 1.284 | 3.001 | 5.365 | 4.742 | 3.190 | 3.147 | 3.171 |
| 1 net weight 1/(d-1) [mk] | invdeg_mk | 1.203 | 1.199 | 0.555 | 2.289 | 1.278 | 1.307 | 1.432 | 1.486 | 1.472 | 1.186 | 1.423 | 1.286 |
| 1 net weight 1/(d-1) [fp] | invdeg_fp | 1.150 | 1.147 | 0.490 | 15.817 | 1.107 | 1.278 | 1.373 | 1.431 | 1.335 | 1.625 | 1.353 | 1.498 |
| 3 hpwl vectors above level 2 [tp] | hpwl_l2_tp | 1.119 | 0.936 | 0.413 | 1.151 | 0.909 | 1.181 | 1.216 | 1.227 | 1.214 | 0.854 | 1.209 | 0.997 |
| 3 hpwl vectors above level 4 [tp] | hpwl_l4_tp | 1.160 | 0.949 | 0.425 | 1.165 | 0.936 | 1.202 | 1.212 | 1.236 | 1.238 | 0.874 | 1.222 | 1.014 |
| 2 probabilistic HPWL vectors [tp] | phpwl_tp | 1.255 | 1.062 | 0.504 | 1.303 | 1.002 | 1.262 | 1.271 | 1.381 | 1.531 | 0.974 | 1.357 | 1.129 |
| 4 ratio cut (balance sweep) [tp] | ratio_tp | 0.979 | 0.918 | 0.418 | 1.051 | 0.845 | 1.132 | 1.190 | 1.219 | 1.175 | 0.803 | 1.179 | 0.952 |
| 4 normalized cut (balance sweep) [tp] | ncut_tp | 0.978 | 0.920 | 0.418 | 1.055 | 0.839 | 1.127 | 1.201 | 1.213 | 1.169 | 0.803 | 1.177 | 0.952 |
| 5 geometric sparsest cut [tp] | sparsest_tp | 0.994 | 1.533 | 0.666 | 1.088 | 0.911 | 2.165 | 1.600 | 2.208 | 2.079 | 1.001 | 1.997 | 1.361 |
| 6 timing net weights [tp] | timing_tp | 1.063 | 1.025 | 0.447 | 1.143 | 0.985 | - | - | - | - | 0.887 | - | 0.887 |

## Degree-dependent net weights, per engine (geomean over floorplans of weighted / unit-weight HPWL, same engine)

| engine | exphpwl / cutnet | invdeg / cutnet | floorplans |
|---|---|---|---|
| tp | 0.997 | 1.221 | 9 |
| hm | 1.011 | 3.314 | 9 |
| mk | 1.119 | 1.249 | 9 |
| fp | 0.768 | 0.906 | 9 |

## Every tp arm against the cutnet_tp control (geomean of arm / control HPWL)

| variant | k-row | base | all | n |
|---|---|---|---|---|
| exphpwl_tp | 0.995 | 1.000 | 0.997 | 9 |
| invdeg_tp | 1.272 | 1.159 | 1.221 | 9 |
| hpwl_l2_tp | 1.040 | 1.033 | 1.037 | 9 |
| hpwl_l4_tp | 1.065 | 1.043 | 1.055 | 9 |
| phpwl_tp | 1.187 | 1.159 | 1.175 | 9 |
| ratio_tp | 0.978 | 1.007 | 0.991 | 9 |
| ncut_tp | 0.978 | 1.005 | 0.990 | 9 |
| sparsest_tp | 1.220 | 1.705 | 1.416 | 9 |
| timing_tp | 1.081 | - | 1.081 | 5 |

## Placer seconds (mean over seeds) and objective counters (sum over levels)

| floorplan | variant | t_place_s | cut_nets | balance_sweeps | balance_offcenter | direction_choices | direction_flips |
|---|---|---|---|---|---|---|---|
| aes_u70_r2/ng45 | cutnet_tp | 62 | 53094 | 0 | 0 | 0 | 0 |
| aes_u70_r2/ng45 | cutnet_hm | 25 | 51550 | 0 | 0 | 0 | 0 |
| aes_u70_r2/ng45 | cutnet_mk | 85 | 54464 | 0 | 0 | 0 | 0 |
| aes_u70_r2/ng45 | cutnet_fp | 70 | 52727 | 0 | 0 | 0 | 0 |
| aes_u70_r2/ng45 | exphpwl_tp | 64 | 52113 | 0 | 0 | 0 | 0 |
| aes_u70_r2/ng45 | exphpwl_hm | 30 | 50575 | 0 | 0 | 0 | 0 |
| aes_u70_r2/ng45 | exphpwl_mk | 92 | 54011 | 0 | 0 | 0 | 0 |
| aes_u70_r2/ng45 | exphpwl_fp | 78 | 52399 | 0 | 0 | 0 | 0 |
| aes_u70_r2/ng45 | invdeg_tp | 56 | 58077 | 0 | 0 | 0 | 0 |
| aes_u70_r2/ng45 | invdeg_hm | 27 | 56580 | 0 | 0 | 0 | 0 |
| aes_u70_r2/ng45 | invdeg_mk | 98 | 58796 | 0 | 0 | 0 | 0 |
| aes_u70_r2/ng45 | invdeg_fp | 72 | 57831 | 0 | 0 | 0 | 0 |
| aes_u70_r2/ng45 | hpwl_l2_tp | 87 | 52512 | 0 | 0 | 0 | 0 |
| aes_u70_r2/ng45 | hpwl_l4_tp | 90 | 51167 | 0 | 0 | 0 | 0 |
| aes_u70_r2/ng45 | phpwl_tp | 99 | 24964 | 0 | 0 | 0 | 0 |
| aes_u70_r2/ng45 | ratio_tp | 262 | 50670 | 1560 | 928 | 0 | 0 |
| aes_u70_r2/ng45 | ncut_tp | 248 | 51032 | 1536 | 929 | 0 | 0 |
| aes_u70_r2/ng45 | sparsest_tp | 71 | 53094 | 0 | 0 | 0 | 0 |
| aes_u70_r2/ng45 | timing_tp | 70 | 52181 | 0 | 0 | 0 | 0 |
| ibex_u70_r6/ng45 | cutnet_tp | 60 | 50505 | 0 | 0 | 0 | 0 |
| ibex_u70_r6/ng45 | cutnet_hm | 25 | 49403 | 0 | 0 | 0 | 0 |
| ibex_u70_r6/ng45 | cutnet_mk | 80 | 51336 | 0 | 0 | 0 | 0 |
| ibex_u70_r6/ng45 | cutnet_fp | 72 | 49011 | 0 | 0 | 0 | 0 |
| ibex_u70_r6/ng45 | exphpwl_tp | 58 | 49846 | 0 | 0 | 0 | 0 |
| ibex_u70_r6/ng45 | exphpwl_hm | 27 | 48977 | 0 | 0 | 0 | 0 |
| ibex_u70_r6/ng45 | exphpwl_mk | 82 | 52180 | 0 | 0 | 0 | 0 |
| ibex_u70_r6/ng45 | exphpwl_fp | 78 | 49207 | 0 | 0 | 0 | 0 |
| ibex_u70_r6/ng45 | invdeg_tp | 56 | 57512 | 0 | 0 | 0 | 0 |
| ibex_u70_r6/ng45 | invdeg_hm | 23 | 56426 | 0 | 0 | 0 | 0 |
| ibex_u70_r6/ng45 | invdeg_mk | 81 | 58468 | 0 | 0 | 0 | 0 |
| ibex_u70_r6/ng45 | invdeg_fp | 78 | 56267 | 0 | 0 | 0 | 0 |
| ibex_u70_r6/ng45 | hpwl_l2_tp | 84 | 50359 | 0 | 0 | 0 | 0 |
| ibex_u70_r6/ng45 | hpwl_l4_tp | 92 | 49289 | 0 | 0 | 0 | 0 |
| ibex_u70_r6/ng45 | phpwl_tp | 122 | 21978 | 0 | 0 | 0 | 0 |
| ibex_u70_r6/ng45 | ratio_tp | 252 | 51133 | 1722 | 921 | 0 | 0 |
| ibex_u70_r6/ng45 | ncut_tp | 250 | 49666 | 1613 | 848 | 0 | 0 |
| ibex_u70_r6/ng45 | sparsest_tp | 70 | 51418 | 0 | 0 | 1 | 1 |
| ibex_u70_r6/ng45 | timing_tp | 57 | 51827 | 0 | 0 | 0 | 0 |
| jpeg_u70_r8/asap7 | cutnet_tp | 133 | 115615 | 0 | 0 | 0 | 0 |
| jpeg_u70_r8/asap7 | cutnet_hm | 74 | 111136 | 0 | 0 | 0 | 0 |
| jpeg_u70_r8/asap7 | cutnet_mk | 210 | 113361 | 0 | 0 | 0 | 0 |
| jpeg_u70_r8/asap7 | cutnet_fp | 189 | 112083 | 0 | 0 | 0 | 0 |
| jpeg_u70_r8/asap7 | exphpwl_tp | 123 | 111192 | 0 | 0 | 0 | 0 |
| jpeg_u70_r8/asap7 | exphpwl_hm | 74 | 109183 | 0 | 0 | 0 | 0 |
| jpeg_u70_r8/asap7 | exphpwl_mk | 199 | 112368 | 0 | 0 | 0 | 0 |
| jpeg_u70_r8/asap7 | exphpwl_fp | 210 | 108749 | 0 | 0 | 0 | 0 |
| jpeg_u70_r8/asap7 | invdeg_tp | 136 | 121599 | 0 | 0 | 0 | 0 |
| jpeg_u70_r8/asap7 | invdeg_hm | 835 | 186838 | 0 | 0 | 0 | 0 |
| jpeg_u70_r8/asap7 | invdeg_mk | 195 | 123345 | 0 | 0 | 0 | 0 |
| jpeg_u70_r8/asap7 | invdeg_fp | 217 | 119365 | 0 | 0 | 0 | 0 |
| jpeg_u70_r8/asap7 | hpwl_l2_tp | 391 | 115116 | 0 | 0 | 0 | 0 |
| jpeg_u70_r8/asap7 | hpwl_l4_tp | 414 | 114136 | 0 | 0 | 0 | 0 |
| jpeg_u70_r8/asap7 | phpwl_tp | 493 | 56799 | 0 | 0 | 0 | 0 |
| jpeg_u70_r8/asap7 | ratio_tp | 540 | 117175 | 5690 | 3149 | 0 | 0 |
| jpeg_u70_r8/asap7 | ncut_tp | 552 | 116024 | 5499 | 2978 | 0 | 0 |
| jpeg_u70_r8/asap7 | sparsest_tp | 188 | 114387 | 0 | 0 | 3 | 3 |
| jpeg_u70_r8/asap7 | timing_tp | 146 | 119610 | 0 | 0 | 0 | 0 |
| swerv_u60_r1/ng45 | cutnet_tp | 752 | 428862 | 0 | 0 | 0 | 0 |
| swerv_u60_r1/ng45 | cutnet_hm | 585 | 412467 | 0 | 0 | 0 | 0 |
| swerv_u60_r1/ng45 | cutnet_mk | 793 | 429932 | 0 | 0 | 0 | 0 |
| swerv_u60_r1/ng45 | cutnet_fp | 2449 | 1019343 | 0 | 0 | 0 | 0 |
| swerv_u60_r1/ng45 | exphpwl_tp | 822 | 421860 | 0 | 0 | 0 | 0 |
| swerv_u60_r1/ng45 | exphpwl_hm | 600 | 412093 | 0 | 0 | 0 | 0 |
| swerv_u60_r1/ng45 | exphpwl_mk | 809 | 426239 | 0 | 0 | 0 | 0 |
| swerv_u60_r1/ng45 | exphpwl_fp | 2300 | 953355 | 0 | 0 | 0 | 0 |
| swerv_u60_r1/ng45 | invdeg_tp | 711 | 481624 | 0 | 0 | 0 | 0 |
| swerv_u60_r1/ng45 | invdeg_hm | 1303 | 982923 | 0 | 0 | 0 | 0 |
| swerv_u60_r1/ng45 | invdeg_mk | 749 | 505976 | 0 | 0 | 0 | 0 |
| swerv_u60_r1/ng45 | invdeg_fp | 2338 | 1244599 | 0 | 0 | 0 | 0 |
| swerv_u60_r1/ng45 | hpwl_l2_tp | 1117 | 427156 | 0 | 0 | 0 | 0 |
| swerv_u60_r1/ng45 | hpwl_l4_tp | 1266 | 425613 | 0 | 0 | 0 | 0 |
| swerv_u60_r1/ng45 | phpwl_tp | 1518 | 150677 | 0 | 0 | 0 | 0 |
| swerv_u60_r1/ng45 | ratio_tp | 1632 | 432875 | 9706 | 4519 | 0 | 0 |
| swerv_u60_r1/ng45 | ncut_tp | 1610 | 426344 | 9410 | 5236 | 0 | 0 |
| swerv_u60_r1/ng45 | sparsest_tp | 743 | 428862 | 0 | 0 | 0 | 0 |
| swerv_u60_r1/ng45 | timing_tp | 759 | 422291 | 0 | 0 | 0 | 0 |
| cva6_u70_r2/ng45 | cutnet_tp | 405 | 391860 | 0 | 0 | 0 | 0 |
| cva6_u70_r2/ng45 | cutnet_hm | 200 | 382686 | 0 | 0 | 0 | 0 |
| cva6_u70_r2/ng45 | cutnet_mk | 379 | 390388 | 0 | 0 | 0 | 0 |
| cva6_u70_r2/ng45 | cutnet_fp | 2725 | 997800 | 0 | 0 | 0 | 0 |
| cva6_u70_r2/ng45 | exphpwl_tp | 390 | 388459 | 0 | 0 | 0 | 0 |
| cva6_u70_r2/ng45 | exphpwl_hm | 226 | 374370 | 0 | 0 | 0 | 0 |
| cva6_u70_r2/ng45 | exphpwl_mk | 349 | 375473 | 0 | 0 | 0 | 0 |
| cva6_u70_r2/ng45 | exphpwl_fp | 439 | 367934 | 0 | 0 | 0 | 0 |
| cva6_u70_r2/ng45 | invdeg_tp | 342 | 444411 | 0 | 0 | 0 | 0 |
| cva6_u70_r2/ng45 | invdeg_hm | 1153 | 903100 | 0 | 0 | 0 | 0 |
| cva6_u70_r2/ng45 | invdeg_mk | 423 | 455259 | 0 | 0 | 0 | 0 |
| cva6_u70_r2/ng45 | invdeg_fp | 423 | 435365 | 0 | 0 | 0 | 0 |
| cva6_u70_r2/ng45 | hpwl_l2_tp | 676 | 389270 | 0 | 0 | 0 | 0 |
| cva6_u70_r2/ng45 | hpwl_l4_tp | 767 | 388327 | 0 | 0 | 0 | 0 |
| cva6_u70_r2/ng45 | phpwl_tp | 884 | 141787 | 0 | 0 | 0 | 0 |
| cva6_u70_r2/ng45 | ratio_tp | 1172 | 391340 | 10049 | 4864 | 0 | 0 |
| cva6_u70_r2/ng45 | ncut_tp | 1167 | 388328 | 9848 | 5359 | 0 | 0 |
| cva6_u70_r2/ng45 | sparsest_tp | 379 | 391860 | 0 | 0 | 0 | 0 |
| cva6_u70_r2/ng45 | timing_tp | 388 | 394599 | 0 | 0 | 0 | 0 |
| aes/ng45 | cutnet_tp | 55 | 51477 | 0 | 0 | 0 | 0 |
| aes/ng45 | cutnet_hm | 21 | 50571 | 0 | 0 | 0 | 0 |
| aes/ng45 | cutnet_mk | 78 | 51933 | 0 | 0 | 0 | 0 |
| aes/ng45 | cutnet_fp | 63 | 51413 | 0 | 0 | 0 | 0 |
| aes/ng45 | exphpwl_tp | 58 | 50776 | 0 | 0 | 0 | 0 |
| aes/ng45 | exphpwl_hm | 25 | 50126 | 0 | 0 | 0 | 0 |
| aes/ng45 | exphpwl_mk | 82 | 52004 | 0 | 0 | 0 | 0 |
| aes/ng45 | exphpwl_fp | 67 | 50432 | 0 | 0 | 0 | 0 |
| aes/ng45 | invdeg_tp | 51 | 57585 | 0 | 0 | 0 | 0 |
| aes/ng45 | invdeg_hm | 19 | 56847 | 0 | 0 | 0 | 0 |
| aes/ng45 | invdeg_mk | 75 | 58143 | 0 | 0 | 0 | 0 |
| aes/ng45 | invdeg_fp | 64 | 57489 | 0 | 0 | 0 | 0 |
| aes/ng45 | hpwl_l2_tp | 76 | 50976 | 0 | 0 | 0 | 0 |
| aes/ng45 | hpwl_l4_tp | 83 | 49914 | 0 | 0 | 0 | 0 |
| aes/ng45 | phpwl_tp | 111 | 24474 | 0 | 0 | 0 | 0 |
| aes/ng45 | ratio_tp | 237 | 51482 | 1596 | 1050 | 0 | 0 |
| aes/ng45 | ncut_tp | 230 | 51232 | 1546 | 990 | 0 | 0 |
| aes/ng45 | sparsest_tp | 100 | 53357 | 0 | 0 | 50 | 50 |
| ibex/asap7 | cutnet_tp | 65 | 69201 | 0 | 0 | 0 | 0 |
| ibex/asap7 | cutnet_hm | 29 | 67008 | 0 | 0 | 0 | 0 |
| ibex/asap7 | cutnet_mk | 109 | 68417 | 0 | 0 | 0 | 0 |
| ibex/asap7 | cutnet_fp | 86 | 67298 | 0 | 0 | 0 | 0 |
| ibex/asap7 | exphpwl_tp | 69 | 68421 | 0 | 0 | 0 | 0 |
| ibex/asap7 | exphpwl_hm | 31 | 67216 | 0 | 0 | 0 | 0 |
| ibex/asap7 | exphpwl_mk | 110 | 69208 | 0 | 0 | 0 | 0 |
| ibex/asap7 | exphpwl_fp | 90 | 66442 | 0 | 0 | 0 | 0 |
| ibex/asap7 | invdeg_tp | 62 | 77479 | 0 | 0 | 0 | 0 |
| ibex/asap7 | invdeg_hm | 524 | 113355 | 0 | 0 | 0 | 0 |
| ibex/asap7 | invdeg_mk | 95 | 78703 | 0 | 0 | 0 | 0 |
| ibex/asap7 | invdeg_fp | 88 | 75852 | 0 | 0 | 0 | 0 |
| ibex/asap7 | hpwl_l2_tp | 111 | 69447 | 0 | 0 | 0 | 0 |
| ibex/asap7 | hpwl_l4_tp | 129 | 66893 | 0 | 0 | 0 | 0 |
| ibex/asap7 | phpwl_tp | 180 | 28299 | 0 | 0 | 0 | 0 |
| ibex/asap7 | ratio_tp | 277 | 68488 | 2070 | 1175 | 0 | 0 |
| ibex/asap7 | ncut_tp | 279 | 67890 | 2035 | 1161 | 0 | 0 |
| ibex/asap7 | sparsest_tp | 116 | 69208 | 0 | 0 | 142 | 132 |
| cva6/asap7 | cutnet_tp | 397 | 502094 | 0 | 0 | 0 | 0 |
| cva6/asap7 | cutnet_hm | 231 | 485187 | 0 | 0 | 0 | 0 |
| cva6/asap7 | cutnet_mk | 502 | 481136 | 0 | 0 | 0 | 0 |
| cva6/asap7 | cutnet_fp | 449 | 458016 | 0 | 0 | 0 | 0 |
| cva6/asap7 | exphpwl_tp | 365 | 499177 | 0 | 0 | 0 | 0 |
| cva6/asap7 | exphpwl_hm | 223 | 478958 | 0 | 0 | 0 | 0 |
| cva6/asap7 | exphpwl_mk | 529 | 474628 | 0 | 0 | 0 | 0 |
| cva6/asap7 | exphpwl_fp | 491 | 459718 | 0 | 0 | 0 | 0 |
| cva6/asap7 | invdeg_tp | 364 | 567738 | 0 | 0 | 0 | 0 |
| cva6/asap7 | invdeg_hm | 1104 | 1101951 | 0 | 0 | 0 | 0 |
| cva6/asap7 | invdeg_mk | 471 | 577432 | 0 | 0 | 0 | 0 |
| cva6/asap7 | invdeg_fp | 457 | 559039 | 0 | 0 | 0 | 0 |
| cva6/asap7 | hpwl_l2_tp | 784 | 501003 | 0 | 0 | 0 | 0 |
| cva6/asap7 | hpwl_l4_tp | 936 | 501362 | 0 | 0 | 0 | 0 |
| cva6/asap7 | phpwl_tp | 1176 | 172209 | 0 | 0 | 0 | 0 |
| cva6/asap7 | ratio_tp | 1235 | 498902 | 11666 | 7008 | 0 | 0 |
| cva6/asap7 | ncut_tp | 1210 | 496441 | 11507 | 6985 | 0 | 0 |
| cva6/asap7 | sparsest_tp | 594 | 477776 | 0 | 0 | 1391 | 1344 |
| mempool_tile/ng45 | cutnet_tp | 430 | 509200 | 0 | 0 | 0 | 0 |
| mempool_tile/ng45 | cutnet_hm | 249 | 485243 | 0 | 0 | 0 | 0 |
| mempool_tile/ng45 | cutnet_mk | 597 | 510979 | 0 | 0 | 0 | 0 |
| mempool_tile/ng45 | cutnet_fp | 551 | 486641 | 0 | 0 | 0 | 0 |
| mempool_tile/ng45 | exphpwl_tp | 426 | 497357 | 0 | 0 | 0 | 0 |
| mempool_tile/ng45 | exphpwl_hm | 247 | 475770 | 0 | 0 | 0 | 0 |
| mempool_tile/ng45 | exphpwl_mk | 599 | 499948 | 0 | 0 | 0 | 0 |
| mempool_tile/ng45 | exphpwl_fp | 567 | 485274 | 0 | 0 | 0 | 0 |
| mempool_tile/ng45 | invdeg_tp | 372 | 610383 | 0 | 0 | 0 | 0 |
| mempool_tile/ng45 | invdeg_hm | 1007 | 1137229 | 0 | 0 | 0 | 0 |
| mempool_tile/ng45 | invdeg_mk | 596 | 620903 | 0 | 0 | 0 | 0 |
| mempool_tile/ng45 | invdeg_fp | 545 | 591736 | 0 | 0 | 0 | 0 |
| mempool_tile/ng45 | hpwl_l2_tp | 814 | 516804 | 0 | 0 | 0 | 0 |
| mempool_tile/ng45 | hpwl_l4_tp | 983 | 508182 | 0 | 0 | 0 | 0 |
| mempool_tile/ng45 | phpwl_tp | 1338 | 186414 | 0 | 0 | 0 | 0 |
| mempool_tile/ng45 | ratio_tp | 1516 | 520077 | 15389 | 9299 | 0 | 0 |
| mempool_tile/ng45 | ncut_tp | 1565 | 518483 | 15150 | 9380 | 0 | 0 |
| mempool_tile/ng45 | sparsest_tp | 664 | 519600 | 0 | 0 | 230 | 227 |
