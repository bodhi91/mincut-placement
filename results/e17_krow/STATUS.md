# E17 — min-cut placers on the k-row floorplans: final status

The grid finished 2026-10-27 07:51. The results are in
`e17_krow_results.csv` (one row per floorplan, baseline and every placer side
by side; regenerate with
`python3 exp/krow_export.py results/e17_krow results/e17_krow/e17_krow_results.csv`).
The per-run JSONs are in this folder; `summary_*.md` are the ratio tables of
`exp/krow_summary.py` (with the author's RePlAce HPWL as the `ref` column),
`summary.csv` the long-format table.

## What was run

180 floorplans from `k-row-floorplans/floorplan`: {gcd, aes, ibex, jpeg,
swerv_no_macro, cva6_no_macro} × {nangate45, asap7} × utilization {60, 70, 80 %}
× {1, 2, 4, 6, 8} rows. Cells unplaced, I/O pins on the bottom edge, no
macros. Six placers per floorplan, one seed each (1 080 runs):

| column | placer |
|---|---|
| `gpl` | OpenROAD `global_placement -disable_pin_density_adjust -overflow 0.2` (the floorplan author's baseline settings; reproduces the author's HPWL within 0.5 %) |
| `capo` | Capo (UMpack) through the Bookshelf bridge, `-densityTarget` = the flow's density |
| `tp` | `mcp` min-cut placer, TritonPart engine |
| `hm` | `mcp`, hMETIS 1.5 engine (fixed driver, see below) |
| `mk` | `mcp`, Mt-KaHyPar 1.6.2 engine |
| `fp` | `mcp`, FastPart portfolio engine |

`mcp` options: `-k 2 -reserve minlocal -density <flow density> -max_tolerance 1.0
-pad_left 0 -pad_right 0 -optimal_cells 12 -feedback_passes 1`, 4 threads. Every
placement then goes through the same evaluation: `detailed_placement`,
`improve_placement` (DPO; **off for swerv / cva6**, where it ran for hours on the
1–2-row floorplans — the primary metric, post-dpl HPWL, is unaffected),
`global_route`, timing. The CSV reports:

* `<p>_hpwl_um` — HPWL after detailed placement (before DPO), µm. This is the
  primary metric; `<p>_vs_gpl` is its ratio to the `gpl` HPWL on the same floorplan.
* `<p>_t_place_s` — placer runtime, seconds (4 threads).
* `<p>_grt_wl_um` — global-route wirelength, µm; empty when routing did not finish.
* `<p>_status` — `ok`; `routing crashed` (placement fine, FastRoute died, see
  below); `failed: <code>` (the placer itself failed).
* `best_placer`, `best_vs_gpl` — the min-cut placer (incl. Capo) with the lowest
  HPWL on that floorplan and its ratio to `gpl`.

## Coverage

| arm | placed (HPWL) | of which routed | placer failed |
|---|---|---|---|
| `gpl` | 149 / 180 | 97 | 31 (GPL-0305 ×5, DPL-0033 ×26) |
| `capo` | 180 / 180 | 120 | 0 |
| `tp` | 180 / 180 | 121 | 0 |
| `hm` | 180 / 180 | 121 | 0 |
| `mk` | 180 / 180 | 124 | 0 |
| `fp` | 180 / 180 | 120 | 0 |

Ratios to `gpl` exist on the 149 floorplans where `gpl` produced a legal
placement; routed-wirelength ratios on the 96 where both sides routed.

## Results (149 floorplans with a `gpl` baseline)

Geometric mean of HPWL (post detailed placement) relative to `gpl`:

| group | n | Capo | mcp-TritonPart | mcp-hMETIS | mcp-Mt-KaHyPar | mcp-FastPart |
|---|---|---|---|---|---|---|
| all | 149 | 0.830 | 0.789 | **0.785** | 0.836 | 0.813 |
| nangate45 | 82 | 0.840 | 0.803 | **0.798** | 0.856 | 0.813 |
| asap7 | 67 | 0.818 | 0.772 | **0.768** | 0.813 | 0.814 |
| util 60 % | 53 | 0.894 | 0.821 | **0.804** | 0.860 | 0.807 |
| util 70 % | 47 | 0.767 | 0.732 | **0.731** | 0.784 | 0.796 |
| util 80 % | 49 | 0.826 | **0.812** | 0.818 | 0.863 | 0.838 |
| 1 row | 30 | 0.959 | 0.931 | **0.914** | 1.003 | 0.993 |
| 2 rows | 35 | 0.872 | **0.834** | 0.836 | 0.877 | 0.878 |
| 4 rows | 18 | 0.949 | 0.893 | 0.899 | 0.924 | **0.872** |
| 6 rows | 31 | 0.836 | 0.786 | **0.783** | 0.839 | 0.784 |
| 8 rows | 35 | 0.648 | 0.610 | **0.604** | 0.646 | 0.633 |
| gcd | 28 | 1.073 | 0.997 | 0.997 | 1.003 | 0.997 |
| aes | 29 | 1.017 | 0.978 | 0.970 | 1.015 | **0.935** |
| ibex | 24 | 0.929 | 0.890 | 0.961 | 0.924 | **0.866** |
| jpeg | 24 | 0.592 | 0.557 | **0.544** | 0.667 | 0.575 |
| swerv_no_macro | 22 | 0.730 | 0.696 | **0.672** | 0.723 | 0.833 |
| cva6_no_macro | 22 | 0.666 | 0.640 | **0.611** | 0.681 | 0.696 |

Routed wirelength (96 floorplans where both routed) follows the same order with
smaller margins: Capo 0.953, TritonPart 0.918, hMETIS 0.926, Mt-KaHyPar 0.947,
FastPart 0.904 (the routed set is biased toward the small designs and the
wider dies, where the HPWL margins are smaller too).

Placer runtime (mean seconds, 4 threads; gpl / Capo / TP / hM / MK / FP):
gcd 0 / 1 / 2 / 1 / 3 / 3; aes 5 / 26 / 66 / 33 / 79 / 71; ibex 7 / 35 / 82 / 44 /
100 / 92; jpeg 43 / 119 / 239 / 187 / 357 / 311; swerv 107 / 264 / 425 / 287 /
521 / 576; cva6 109 / 245 / 419 / 259 / 493 / 508.

What the numbers say:

* On these dense, flat, macro-free floorplans with all pins on one edge the
  top-down placers are ahead of `gpl`, the opposite of the square ORFS
  floorplans of E16 (where every top-down placer was 7–35 % behind). The best
  min-cut arm beats `gpl` on 137 of the 149 floorplans; geomean of the best arm
  0.761. The 12 where `gpl` wins are by at most 4.7 %: nine are gcd at 60 % (498
  cells, every row count), three are swerv 1-row nangate45 (1.2–1.9 %).
* The advantage grows with design size and with the number of rows: gcd parity,
  aes 3–7 %, ibex 11–13 %, jpeg 45 %, swerv 33 %, cva6 39 % for the best arm.
  8-row floorplans: every min-cut arm is 35–40 % below `gpl`; on 2–8-row jpeg
  `gpl` is 1.4–2.2× worse than every min-cut placer. The author's own RePlAce
  run gives the same `gpl` HPWL (`ref` column of `summary_hpwl_dpl.md`), so this
  is RePlAce struggling with the extreme aspect ratios, not a setup problem.
* Engine ranking: hMETIS and FastPart are best about equally often (61 and 60 of
  149 floorplans), hMETIS has the best geomean overall and on the four larger
  designs, FastPart on aes / ibex; TritonPart third (17), Mt-KaHyPar and Capo
  rarely best (5 and 6). Capo beats `gpl` everywhere except gcd / aes.
* The 1-row losses (swerv) and the 1-row jpeg wins have the same mechanism
  (per-net decomposition, see the E17 notes in `IMPLEMENTATION_PLAN.md`): the
  min-cut tree shortens local nets and lengthens the long nets cut at the top
  levels; whether that nets out depends on the share of high-fanout nets in the
  HPWL (swerv: 45 % in nets of fanout ≥ 6 → loss; jpeg: 83 % in 2–5-pin nets →
  win).

## Failures and what was done about them

* **`gpl` fails on 31 floorplans** (`failed:` in `gpl_status`): RePlAce diverges
  (GPL-0305) on five asap7 1-row floorplans (gcd 60/70 %, aes 70 %, ibex 70 %,
  swerv 70 %); `detailed_placement` rejects its result (DPL-0033) on 26,
  almost all 4- and 6-row floorplans of ibex / jpeg / swerv / cva6 (asap7 and
  nangate45). The author's `replace/` baseline is missing on exactly these
  floorplans too. The five other placers place every floorplan; the CSV has
  their absolute HPWL (marked `*` in the summaries), no ratio.
* **Routing crashes on the flat dies** (`routing crashed`: 52–60 runs per arm,
  every placer alike, including `gpl`): all 1- and 2-row floorplans of jpeg,
  swerv and cva6, ibex asap7 1-row, and part of the 4–8-row swerv / cva6 (a
  1-row swerv core is 177 mm × 1.4 µm). FastRoute runs out of memory building
  its grid (up to 146 GB observed), segfaults in the rip-up-and-reroute
  iterations, or stops with GRT-0229. The harness saves the placement metrics
  before routing, so these runs keep HPWL and placer time and only lack routed
  wirelength; memory gates (`MEM_MIN_GB`, `MEM_MAX_GB` in the configs) kept the
  machine alive.
* **DPO on swerv / cva6** (`improve_placement`, single-threaded independent-set
  matching) ran for 3+ hours on the 1–2-row floorplans; the two designs were
  moved to `exp/e17_krow_big.cfg` / `e17_krow_cva6.cfg` with `DPO=0`. Their
  `hpwl` (post-DPO) column equals `hpwl_dpl`.
* **hMETIS driver bug (fixed, all `hm` runs redone).** libhmetis 1.5 corrupts
  its heap when the pre-assignment leaves a block without a fixed vertex (a
  region that sees a single terminal — common here, with all pins on one edge);
  the placer fell back to flat FM for 13–100 regions per run, sometimes the
  level-1 half-chip cut. `tools/hmetis/hmetis_driver` now adds a zero-weight dummy
  fixed vertex per empty block; no region fails any more. (The E16 hMETIS arm was
  rerun as well: geomean 1.166 → 1.158× gpl.)
* **`gpl` arm settings.** Our first `gpl` arm used `global_placement` defaults
  and was 5–20 % worse than the author's baseline on the 1-row floorplans
  (overflow 0.1 and pin-density inflation hurt on a single row); it was switched
  to the author's settings (`-overflow 0.2 -disable_pin_density_adjust`) and
  rerun before any comparison was drawn.
