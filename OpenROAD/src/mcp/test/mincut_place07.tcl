# gcd: placement feedback.  Passes beyond the first run the placer again
# seeded by its own result, with the not-yet-placed cells assumed at
# their previous positions instead of their region centers; the best
# pass is kept, so the result is never worse than the first pass.  Then
# the same from a gpl seed with -seed_terminals.
source "helpers.tcl"
read_lef Nangate45/Nangate45.lef
read_def simple01.def
set_placement_padding -global -left 1 -right 1

mincut_placement -k 2 -engine tritonpart -seed 1 -threads 1 \
  -pad_left 1 -pad_right 1 -leaf_pad 1 \
  -feedback_passes 3 -report mincut_place07.json
set report [open mincut_place07.json r]
set json [read $report]
close $report
regexp "\"feedback_passes\": (\[0-9\]+)" $json -> passes
regexp "\"feedback_best_pass\": (\[0-9\]+)" $json -> best
regexp "\"feedback_hpwl_um\": \\\[(\[^\\\]\]*)\\\]" $json -> hpwls
regexp "\"hpwl_um\": (\[0-9.\]+)" $json -> hpwl
puts "feedback: $passes passes run, best pass $best"
set first [lindex [split $hpwls ","] 0]
puts "kept <= first pass: [expr {$hpwl <= $first}]"
puts "kept is the minimum: [expr {$hpwl == [lindex [lsort -real [split $hpwls ","]] 0]}]"
file delete mincut_place07.json
detailed_placement
check_placement -verbose

global_placement -density 0.8 -pad_left 1 -pad_right 1
mincut_placement -k 2 -engine tritonpart -seed 1 -threads 1 -density 0.8 \
  -pad_left 1 -pad_right 1 -leaf_pad 1 \
  -seed_placement -assignment transport_only -seed_terminals
detailed_placement
check_placement -verbose
