# gcd: bisection with TritonPart, then legalize and check.
source "helpers.tcl"
read_lef Nangate45/Nangate45.lef
read_def simple01.def

mincut_placement -k 2 -engine tritonpart -seed 1 -threads 1
detailed_placement
check_placement -verbose
