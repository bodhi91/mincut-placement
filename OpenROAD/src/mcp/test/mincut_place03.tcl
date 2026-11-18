# mixed reserve policy (minlocal pull + congestion demand) at a target
# density above the core utilization; the cells must end up legal.
source "helpers.tcl"
read_lef Nangate45/Nangate45.lef
read_def simple01.def
mincut_placement -k 2 -engine tritonpart -objective cutnet \
  -reserve mixed -density 0.8 -seed 1 -threads 1 -report mincut_place03.json
detailed_placement
check_placement -verbose
set report [open mincut_place03.json r]
set json [read $report]
close $report
foreach key {leaf_density_max occupied_area_fraction} {
  regexp "\"$key\": (\[0-9.\]+)" $json -> value
  puts "$key [expr { $value <= 1.0 && $value > 0.0 ? "ok" : "bad" }]"
}
file delete mincut_place03.json
