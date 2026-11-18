# gcd: the Capo-style end game switched off (heuristic end case only),
# then on with limits above the defaults.
source "helpers.tcl"
read_lef Nangate45/Nangate45.lef
read_def simple01.def

mincut_placement -k 2 -engine tritonpart -seed 1 -threads 1 \
  -exact_cells 0 -optimal_cells 0 -iron_window 0
mincut_placement -k 2 -engine tritonpart -seed 1 -threads 1 \
  -exact_cells 24 -optimal_cells 12 -iron_window 9 -iron_passes 3
detailed_placement
check_placement -verbose
