# E25: partitioner objectives — status

Launched 2026-10-27 10:49 on host-A (screen `e25_obj`, log `results/e25_objectives.log`,
`exp/e25_launch.sh` → `exp/e25_objectives.cfg`).  Binary `OpenROAD/build/bin/openroad`
(separate from E23's `build-e23`, which keeps running).

## Question

A min-cut placer has one knob that no other placer has: *what the partitioner minimizes* at every
bisection.  It has no placement to start from, so the objective must be computable from the netlist,
the region geometry and the cells already committed above.  Does anything beat the unit-weight cut?

## Arms (variant names in the results)

| arm | variants | what changes |
|---|---|---|
| ref | `gpl` | RePlAce reference, `MCP_MODE=krow` as E17 / E23 |
| 0 | `cutnet_{tp,hm,mk,fp}` | control: unit-weight cut nets, E17 / E23 settings |
| 1 | `exphpwl_{tp,hm,mk,fp}` | `-net_weight exphpwl`: expected HPWL gain of not cutting a d-pin net under a uniform prior, 1 at d = 2 → 1.5 |
| 1 | `invdeg_{tp,hm,mk,fp}` | `-net_weight invdeg`: 1/(d − 1) |
| 2 | `phpwl_tp` | `-objective phpwl`: expected-HPWL vectors, local pins uniform over the occupied child boxes; cutting a d-pin net costs (L/2)(d − 1)/(d + 1) more than keeping it |
| 3 | `hpwl_l2_tp`, `hpwl_l4_tp` | `-objective hpwl -fine_objective cutnet -fine_level 2 \| 4`: geometric HPWL vectors at the top, cut nets below |
| 4 | `ratio_tp`, `ncut_tp` | `-balance_objective ratio \| ncut`: balance fraction swept over {0.3, 0.4, 0.5, 0.6, 0.7} ± 0.05, lowest cost/(a(1 − a)) or cost(1/vol₀ + 1/vol₁) kept |
| 5 | `sparsest_tp` | `-cut_direction sparsest`: both directions partitioned, fewer (cut + inessential) nets per unit cut-line length kept |
| 6 | `timing_tp` | `MCP_PRE=timing`: `-net_weights_file` with 1 + 4 (T − slack)/(T − WNS) from ideal-wire STA (`bench/common.tcl` `mcp_timing_weights`); k-row designs only |

## Floorplans

| group (`E25_SET`) | floorplans | seeds | DPO | notes |
|---|---|---|---|---|
| krow_small | aes_u70_r2_nangate45, ibex_u70_r6_nangate45 | 1 2 3 | on | |
| krow_mid | jpeg_u70_r8_asap7 | 1 | on | |
| krow_big | swerv_no_macro_u60_r1_nangate45, cva6_no_macro_u70_r2_nangate45 | 1 | off (as E17) | `MCP_NO_ROUTE=1`: global_route exhausted 100+ GB in E17 |
| base_small | aes_ar1_nangate45, ibex_ar1_asap7 | 1 2 3 | on | `-soft_blockages block` |
| base_big | cva6_ar1_asap7, mempool_tile_ar1_nangate45 | 1 | on | `-soft_blockages block` |

Order: arm group A (gpl, arms 0 / 1 / 3) over all five groups, then B (arms 2 / 4 / 5 / 6).
332 runs; `-j` 6 / 6 / 4 / 3 / 2 by group, 4 threads each.  From 15:49 arm group B runs on
**host-B** in parallel (`exp/e25_launch_pdn.sh`, screen `e25_pdn`, log `results/e25_objectives_pdn.log`,
big design groups first); `exp/run.sh` got a per-run lock (`<prefix>.lock/`, atomic `mkdir` on NFS)
and records the host in the result (`knobs.host`).  host-B's Xeon E5-2695 v4 @ 2.1 GHz is some 15-25 %
slower than host-A's Gold 6148 @ 2.4 GHz: `t_place` is comparable only within a host (HPWL is
deterministic for a seed and unaffected).

## Regression (defaults must reproduce E17 / E23)

`build-e25` with the default options, same harness inputs, `hpwl_global` after the placer:

| run | E17 / E23 | build-e25 |
|---|---|---|
| gcd_ar1_nangate45 tp | 3085.4 | 3085.4 |
| aes_u70_r2_nangate45 tp | 1749732.5 | 1749732.5 |
| aes_u70_r2_nangate45 hm | 1597461.6 | 1597461.6 |
| aes_ar1_nangate45 tp (soft blockages) | 148558.3 | 148558.3 |

Smoke tests (gcd_ar1_nangate45, tp, HPWL µm after dpl): cutnet 3085.4, exphpwl 2895.6,
invdeg 3076.0, exphpwl/hm 3090.3, phpwl 3681.8, hpwl→cutnet@2 3464.1, ratio 2814.1 (62 sweeps,
41 off-center), ncut 2960.8 (58 / 32), sparsest 3084.5 (8 direction choices, 8 flips), timing weights
on aes_u70_r2 1789165.2 (12 628 nets weighted, period 10 ns, WNS 7.62 ns).

Found and fixed during the smoke tests: `SubproblemBuilder::build` de-duplicated nets with a stamp
equal to the region id, so a second build of the same region (the other direction of the sparsest
cut) saw no nets at all and always "won" with cut 0 (gcd 9261 µm, 3× the control).  Each build now
takes a fresh stamp; the defaults are unaffected (regression above re-run after the fix).  The
balance sweep now also writes the chosen fraction's bounds back into the plan, so the
`balance_violations` counter is against the bounds that were actually used.

Observation to keep in mind for arm 5: cut per unit line length favors the *longer* cut line, i.e.
cutting across the shorter side (the usual rule cuts the longer side); on the square gcd it flipped
every bisection it was allowed to and still landed at the control's HPWL.

## Results

(filled in as the runs land; `exp/e25_export.py results/e25_objectives` →
`e25_objectives.csv`, `e25_objectives_summary.md`)
