# Placement solutions

This directory receives the placement solutions produced by the harness;
the repository ships the tooling, not the DEFs. Each solution is the
legal placement behind one number in the paper: the placer's output
after OpenROAD `detailed_placement` and before detailed-placement
optimization, which is the primary metric of every table (HPWL after
detailed placement).

```bash
X3_SET=krow_small exp/run.sh exp/solutions_x3.cfg -j 8 --keep-def   # few-row set (krow_small, krow_big) and square set (base_small, base_big)
X4_SET=small      exp/run.sh exp/solutions_x4.cfg -j 8 --keep-def   # sweep set (small, big)
python3 exp/collect_solutions.py solutions
```

```
solutions/
├── manifest.csv                                set, floorplan, platform, placer, seed, HPWL (µm), file, source experiment
├── few_row/<design>_u<util>_r<rows>_<pdk>/     180 floorplans, 1–8 rows
├── sweep/sweep_<design>_ar<AR>_util<util>_<pdk>/ 182 floorplans, aspect ratio × utilization
└── square/<design>_ar1_<pdk>/                  20 floorplans at aspect ratio 1
```

Inside a floorplan directory, `<placer>_s<seed>.def.gz`:

| placer | meaning |
|---|---|
| `replace` | RePlAce (`global_placement -disable_pin_density_adjust -overflow 0.2`) |
| `rb_tp` | RB (TP): recursive bisection with TritonPart |
| `rbk_tp` | RB+*k* (TP): RB with level-wise reassignment |
| `rb_hm`, `rb_mk`, `rb_fp` | RB with hMETIS, Mt-KaHyPar, FastPart |
| `capo` | Capo 10.5 through the Bookshelf bridge |

A DEF is complete: die, rows, tracks, blockages, fixed instances, I/O
pins, the netlist and the placed standard cells. It therefore serves
two purposes.

**Verify a result.** Recompute HPWL and legality from the DEF and the
public PDK LEFs (OpenROAD's `test/Nangate45`, `test/asap7`):

```bash
MCP_DEF=solutions/few_row/aes_u70_r2_nangate45/rbk_tp_s1.def.gz MCP_PLATFORM=nangate45 \
  openroad -exit bench/verify_solution.tcl
```

The printed `hpwl_um` matches the `hpwl_dpl_um` column of `manifest.csv`
and the `hpwl_dpl` column of `results/<experiment>/runs.csv`.

**Rebuild the benchmark input.** Any solution of a floorplan yields the
floorplan's checkpoint (movable cells unplaced again), from which every
placer in the paper can be re-run through `exp/run.sh`:

```bash
MCP_DESIGN=aes_u70_r2_nangate45 \
MCP_DEF=solutions/few_row/aes_u70_r2_nangate45/replace_s1.def.gz \
  openroad -exit bench/checkpoint_from_def.tcl
```

HPWL is the half-perimeter of the bounding box of each net's pin
shapes, summed over signal nets with at least two pins, in micrometres;
it is the sum computed by `bench/common.tcl` in every experiment.
