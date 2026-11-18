# 8. Where the advantage over Capo comes from

Capo 10.5 (`CapoTest1 -densityTarget <flow density>`, Bookshelf bridge, legalized by the same `detailed_placement`) is the fixed reference: every row is HPWL after detailed placement / Capo on the same floorplan, geomean over the 180 few-row floorplans of table 1 (6 designs x 2 PDKs x 60/70/80 % x 1/2/4/6/8 rows), with the number of floorplans the arm wins against Capo. Capo and every arm complete all 180, so no floorplan is excluded. TritonPart unless stated, seed 1, 4 threads, no DPO. The last column gives the same arm against RePlAce on the floorplans where RePlAce converged (table 7's rule), for cross-reference with the paper's ablation table.

Sources: Capo, RePlAce and the four RB engines from E17 (`e17_krow`); the cumulative ladder from E26 (`e26_ablation`); the leave-one-out and base-rung partitioner arms from E27 (`e27_capo_attr`); RB+k from X3 (`x3_mega`). Regenerate: `python3 exp/capo_attribution.py`.

Reading. The ladder is an ablation of *our* components measured against Capo, not a decomposition of Capo's internals: Capo has its own whitespace allocator, end-case placer and row ironing, so rung 0 is a bisection core without those pieces (and lands 1.7 % behind Capo), not "Capo with TritonPart". What the table does establish is which of our components carry the 4.6 % (RB) / 6.0 % (RB+k) advantage and that the partitioning engine is not one of them.

## 8.1 Cumulative component ladder against Capo

| configuration | / Capo | wins / 180 | step | 1 row | 2 rows | 4 rows | 6 rows | 8 rows | / RePlAce (n) |
|---|---|---|---|---|---|---|---|---|---|
| 0. basic cut-net bisection: TritonPart, terminal propagation, uniform whitespace, even in-row spreading, no leaf optimization | 1.017 | 67 | -- | 1.030 | 1.017 | 1.010 | 1.009 | 1.019 | 0.878 (137) |
| 1. + two checkerboard waves | 1.017 | 67 | -0.1 % | 1.031 | 1.017 | 1.011 | 1.009 | 1.017 | 0.877 (137) |
| 2. + connectivity-guided whitespace (`-reserve minlocal`, adaptive tolerance) | 0.988 | 100 | -2.9 % | 0.993 | 0.985 | 0.986 | 0.983 | 0.992 | 0.852 (137) |
| 3. + end game: exact bisection <= 16 cells, leaf B&B (rows, then orders) <= 12, row ironing 7 x 2 | 0.964 | 128 | -2.4 % | 0.976 | 0.969 | 0.964 | 0.955 | 0.956 | 0.828 (137) |
| 4. + optimal in-row positions = **RB (TP)** | 0.954 | 142 | -1.1 % | 0.964 | 0.957 | 0.955 | 0.946 | 0.946 | 0.817 (137) |
| 5. + level-wise k-way reassignment = **RB+k (TP)** | 0.940 | 155 | -1.5 % | 0.952 | 0.942 | 0.940 | 0.934 | 0.932 | 0.806 (137) |

By design (geomean / Capo over the 30 floorplans of each design; cells in parentheses in table 1):

| configuration | gcd | aes | ibex | jpeg | swerv_no_macro | cva6_no_macro |
|---|---|---|---|---|---|---|
| 0. basic cut-net bisection | 1.084 | 1.027 | 0.996 | 0.996 | 0.988 | 1.016 |
| 1. + two checkerboard waves | 1.082 | 1.026 | 0.996 | 0.996 | 0.988 | 1.015 |
| 2. + connectivity-guided whitespace (`-reserve minlocal`, adaptive tolerance) | 1.060 | 0.996 | 0.973 | 0.957 | 0.962 | 0.982 |
| 3. + end game | 0.970 | 0.974 | 0.959 | 0.944 | 0.959 | 0.976 |
| 4. + optimal in-row positions | 0.929 | 0.963 | 0.956 | 0.941 | 0.958 | 0.975 |
| 5. + level-wise k-way reassignment | 0.929 | 0.947 | 0.938 | 0.937 | 0.938 | 0.949 |

## 8.2 Leave-one-out on RB (TP)

Each row removes one end-game component from the full RB (TP) configuration (E27). `full` is RB (TP) rerun on the E27 binary (control: it must match rung 4 above). The `/ full` columns are the arm's HPWL over the control's on the same floorplan, so a value above 1 is what the component is worth.

| configuration | n | / Capo | wins vs Capo | / full | floorplans worse than full | / full, 1 row | / full, 2 rows | / full, 4 rows | / full, 6 rows | / full, 8 rows |
|---|---|---|---|---|---|---|---|---|---|---|
| full RB (TP) (control) | 180 | 0.954 | 142 | 1.000 | 0/180 | 1.000 | 1.000 | 1.000 | 1.000 | 1.000 |
| without exact small-region bisection (`-exact_cells 0`) | 180 | 0.954 | 142 | 1.000 | 141/180 | 1.000 | 1.000 | 1.000 | 1.000 | 1.000 |
| without the leaf branch and bound (`-optimal_cells 0`; ironing kept) | 180 | 0.963 | 134 | 1.009 | 179/180 | 1.001 | 1.005 | 1.009 | 1.013 | 1.019 |
| without row ironing (`-iron_window 0`) | 180 | 0.961 | 136 | 1.007 | 180/180 | 1.009 | 1.007 | 1.006 | 1.008 | 1.007 |
| leaf B&B orders only: heuristic cells-to-rows assignment kept (`-optimal_rows 0`) -- the single-row end case | 180 | 0.962 | 134 | 1.009 | 144/180 | 1.000 | 1.004 | 1.009 | 1.013 | 1.019 |

On one-row floorplans the row-assignment search has nothing to decide, so `rows_only` equals `full` there by construction; its effect on 2-8 rows is the value of searching the cells-to-rows assignment over ordering the cells within rows the heuristic assigned.

## 8.3 The partitioning engine

The engine swapped at both ends of the ladder: the full RB configuration (E17 arms) and the base rung (E27 `a0_*`). hMETIS is the class of Capo's own MLPart (multilevel FM); `fm` is TritonPart with coarsening disabled (flat FM from a random initial bisection).

| configuration | engine | / Capo | wins / 180 | 1 row | 2 rows | 4 rows | 6 rows | 8 rows | / RePlAce (n) |
|---|---|---|---|---|---|---|---|---|---|
| full RB | TritonPart | 0.954 | 142 | 0.964 | 0.957 | 0.955 | 0.946 | 0.946 | 0.817 (137) |
| full RB | hMETIS | 0.948 | 143 | 0.956 | 0.958 | 0.957 | 0.936 | 0.934 | 0.815 (137) |
| full RB | Mt-KaHyPar | 1.015 | 84 | 1.037 | 1.008 | 1.025 | 1.002 | 1.001 | 0.863 (137) |
| full RB | FastPart | 0.977 | 148 | 1.017 | 1.007 | 0.942 | 0.939 | 0.981 | 0.831 (137) |
| base rung 0 | TritonPart | 1.017 | 67 | 1.030 | 1.017 | 1.010 | 1.009 | 1.019 | 0.878 (137) |
| base rung 0 | hMETIS | 1.011 | 79 | 1.015 | 1.016 | 1.019 | 0.998 | 1.009 | 0.874 (137) |
| base rung 0 | flat FM | 5.286 | 0 | 5.317 | 5.334 | 5.366 | 5.206 | 5.209 | 3.951 (137) |

