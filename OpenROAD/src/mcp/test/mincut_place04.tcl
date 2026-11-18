# seeded placement: the quadratic initial placement of gpl is assigned
# to the children by the transportation problem and refined with a
# displacement cost; the cells must end up legal.
source "helpers.tcl"
read_lef Nangate45/Nangate45.lef
read_def simple01.def
set_thread_count 1
# -seed_placement without a placed design is an error
catch { mincut_placement -seed_placement } msg
puts $msg
global_placement -skip_nesterov_place -density 0.8
mincut_placement -k 2 -engine tritonpart -density 0.8 \
  -seed_placement -assignment transport -displacement_weight 1 \
  -seed 1 -threads 1 -report mincut_place04.json
detailed_placement
check_placement -verbose
set report [open mincut_place04.json r]
set json [read $report]
close $report
foreach key {seed_placement assignment displacement_weight} {
  regexp "\"$key\": (\[^,\]+)" $json -> value
  puts "$key $value"
}
regexp "\"assignment_infeasible\": (\[0-9\]+)" $json -> value
puts "level 0 assignment_infeasible $value"
file delete mincut_place04.json
