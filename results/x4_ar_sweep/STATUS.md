# X4 — mcp on the floorplan author's aspect-ratio × utilization DOE: final status

The sweep finished 2026-11-06 08:12 (host-A + host-B, 2026-11-02 → 11-06).
Tables: `SUMMARY.md`; data: `x4_floorplans.csv` (one row per floorplan, seed
means and ratios) and `x4_runs.csv` (one row per run). Regenerate with
`python3 exp/x4_export.py results/x4_ar_sweep`.

## What was run

the floorplan author's macro-free DOE (`(external)/sweep-runs`,
copied to `scratch/ar_sweep/`): {gcd, aes, ibex, jpeg, swerv, cva6,
mempool_tile, xiangshan} × aspect ratio {0.03, 0.06, 0.09, 1} × target
utilization {60, 70, 80 %} × {asap7, nangate45}, cells unplaced, I/O fixed,
`REMOVE_MACROS=1`. 182 floorplans (10 gcd configs are absent from his tree).
megaboom and mempool_group were excluded (they are in E23). Three arms × 3
seeds, 1 638 runs, config `exp/x4_ar_sweep.cfg`, k-way build
`OpenROAD/build/bin/openroad`:

| arm | placer |
|---|---|
| `gpl` | OpenROAD `global_placement -disable_pin_density_adjust -overflow 0.2`, density = util + 0.10 (the k-row baseline settings; deterministic, the seed has no effect) |
| `cutnet_tp` | `mcp -k 2 -reserve minlocal -density <util+0.10> -max_tolerance 1.0 -pad 0 -optimal_cells 12 -feedback_passes 1`, TritonPart |
| `kway_tp` | same, plus `-kway_refine_levels {2 3 4 5}` (the x3_full k-way configuration) |

Each placement goes through `detailed_placement`; `improve_placement` (DPO)
ran on the small set only (off for swerv/cva6/mempool_tile/xiangshan). No
routing (`MCP_NO_ROUTE=1`): the metric is HPWL.

**Primary metric: HPWL after detailed placement, before DPO** (`hpwl_dpl`),
as in E17/E25. Ratios are seed means per floorplan; tables are geometric
means over floorplans.

## Coverage

1 635 / 1 638 runs. The 3 missing are `xiangshan_ar0.09_util80_asap7 gpl`
seeds 1–3: RePlAce converged (158 overlaps left after 2 h) but `detailed_placement`
failed with DPL-0033 on that 287-row, 0.86 mm × 77 µm core. Every mcp run
finished with a legal placement (0 violations).

## Results

### Headline

| set | floorplans | k-way / gpl | wins | cutnet / gpl | k-way / cutnet |
|---|---|---|---|---|---|
| small (gcd aes ibex jpeg) | 86 | 1.019 | 29 | 1.026 | 0.993 (70/86) |
| big (swerv cva6 mempool_tile xiangshan) | 95 | 0.956 | 46 | 0.971 | 0.985 (95/95) |
| big, excluding the 6 gpl outliers below | 89 | 1.013 | 40 | 1.029 | 0.985 |

The headline numbers hide a strong aspect-ratio dependence.

### Aspect ratio is the axis; utilization is not

k-way / gpl, big set, excluding the gpl outliers:

| AR | 0.03 | 0.06 | 0.09 | 1 |
|---|---|---|---|---|
| ratio (wins) | **0.902** (20/21) | 0.987 (12/23) | 1.022 (8/21) | **1.141** (0/24) |

Small set: 0.970 → 1.001 → 1.030 → 1.071 over the same four ARs. Within an
AR, utilization moves the ratio by ≤ 0.02 in either direction (small set is
slightly better at util 80, big set slightly worse) — no trend.

By design, the AR 0.03 advantage grows with netlist size: gcd 0.95, jpeg 0.87,
swerv 0.93, mempool_tile 0.84, xiangshan ng45 0.83 — while aes and ibex
(10–17 k cells, 17–38 rows) lose at every AR (1.02–1.15). At AR 1 every
design loses: 1.05 (gcd, jpeg) to 1.19 (swerv, cva6).

In row terms (`SUMMARY.md`, "Ratio versus number of rows"): mcp wins
decisively at ≤ 8 rows (gcd only here; E17 covers this range properly), is
roughly even at 33–256 rows when the design is ≥ 45 k cells, and loses at
17–32 rows for the 10–17 k-cell designs and at ≥ 300 rows (AR 1) everywhere.
The dependence is on rows × design size, not on AR alone: xiangshan at
AR 0.09 (200–330 rows) is where aes is at AR 1 (100–160 rows).

### k-way versus cutnet

The k-way refinement (`-kway_refine_levels {2 3 4 5}`) is a consistent small
gain: 0.993 on the small set (70/86), 0.985 on the big set (95/95), at
+0–15 % placer time. It never made a floorplan worse on the big set.

### gpl instability on the wide asap7 xiangshan floorplans

Six floorplans have k-way / gpl < 0.7, all xiangshan asap7 at AR ≤ 0.09:

| floorplan | rows | gpl HPWL | k-way HPWL | ratio |
|---|---|---|---|---|
| ar0.03 util 60/70/80 | 191/176/165 | 5.90 / 5.55 / 5.38 M | 2.81 / 2.60 / 2.48 M | 0.47 |
| ar0.06 util 60 | 270 | 3.53 M | 2.21 M | 0.63 |
| ar0.09 util 60/70 | 331/306 | 7.46 / 7.04 M | 2.00 / 1.89 M | 0.27 |

(and ar0.09 util 80 failed legalization). RePlAce produces a legal but
badly spread placement on these 0.9–1.7 mm-wide, 45–90 µm-tall cores; the
nangate45 xiangshan floorplans of the same AR are normal (0.83 → 0.95 →
1.00). These are reported in both forms above; they are gpl's problem, not
evidence for mcp, and the floorplan author's `-routability_driven` RePlAce runs on the same
floorplans will show whether his flow has it too.

### Seeds, legalization, DPO

* Seed spread (max/min − 1 over 3 seeds): median 1.8 % for both mcp arms;
  gpl is deterministic. Differences below ~2 % on a single floorplan are
  noise; the per-AR geomeans over 21–24 floorplans are not.
* Legalization cost (hpwl_dpl / hpwl_global − 1): mcp 0.01 % median (its
  output is row-aligned and overlap-free), gpl 6.7 % median on the big set,
  14.5 % on the small set (up to 47 %). Comparing *pre*-legalization HPWL
  would favour gpl by that much; post-DPL is the fair point.
* DPO (small set only) helps gpl more than mcp: −7.7 % vs −3.1 %. On the
  **post-DPO** HPWL the small-set ratio is 1.072 (15/86 wins) instead of
  1.019. The earlier status reports in chat quoted post-DPO numbers for the
  small set; `x4_floorplans.csv` has both (`*_hpwl_um` = post-DPL,
  `*_hpwl_final_um` = post-DPO where it ran).

### Runtime

mcp is 11× gpl's placer time on the 10–115 k-cell designs (55–480 s vs 5–43 s,
4 threads) and 53× on xiangshan (2.5 h vs 170 s). xiangshan's 144 mcp runs
were 82 % of the sweep's placement CPU time.

### Cross-check against the floorplan author's baseline

His `summary.csv` has RePlAce results for ar0.03_util60 only (9 floorplans).
Our `gpl` arm is 0.82–1.03× his HPWL (his flow is `-routability_driven`,
ours is the k-row baseline with `-overflow 0.2`); k-way is 0.80–1.02× his.
The comparison against his numbers is therefore slightly *less* favourable
to us than against our own gpl on aes (1.02 vs 1.07) and slightly more on
jpeg/swerv — same conclusion. Re-run the cross-check table when his `place`
stage lands for the other configs.

## Reading

* The k-row result (E17: 0.83 at 1–8 rows) extends to the floorplan author's AR 0.03
  (20–190 rows) for designs ≥ 45 k cells: 0.84–0.93, 23/24 wins. It does not
  extend to the 10–17 k-cell designs at 17–50 rows, where gpl is already fine.
* The crossover is around AR 0.06–0.09 (roughly 50–250 rows depending on
  design); at AR 1 mcp loses by 5–19 % with no exception, consistent with
  E23 (megaboom/mempool_group at AR 1: 0.89–1.19 depending on engine/PDK).
* Utilization 60–80 % is not a factor for either placer in this range.

## Files

* `SUMMARY.md` — all tables (AR × util, design × AR per PDK with row counts,
  rows axis, seed spread, runtime, outliers, failures, the floorplan author cross-check).
* `x4_floorplans.csv` — per floorplan: `design pdk ar util rows core_w_um
  core_h_um cells nets set`, then per arm `_n _hpwl_um _hpwl_min _hpwl_max
  _spread _hpwl_final_um _t_place_s`, and `cutnet_tp_vs_gpl kway_tp_vs_gpl
  kway_vs_cutnet`.
* `x4_runs.csv` — per run: HPWL global/dpl/final, t_place, t_dpl, memory,
  violations, dpo flag, host.
* `*.json`, `*.log`, `*.stdout` — per run. `*.eval.json` is the partial
  record written before (skipped) routing.
