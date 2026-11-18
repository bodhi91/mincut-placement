# hpwl net-vector objective, k = 4, tritonpart engine
source "helpers.tcl"
read_lef Nangate45/Nangate45.lef
read_def simple01.def
mincut_placement -k 4 -engine tritonpart -objective hpwl -seed 1 -threads 1
detailed_placement
check_placement -verbose
