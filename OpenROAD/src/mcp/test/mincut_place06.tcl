# gcd: in-row position rules of the end case and the ironing windows
# (even / optimal / joint) with the detailed-placement padding.  The
# optimal positions must not cost HPWL against the even ones, and the
# legalizer (whose padding they keep where a row affords it) moves them
# less.
source "helpers.tcl"
read_lef Nangate45/Nangate45.lef
read_def simple01.def
set_placement_padding -global -left 1 -right 1

set hpwl {}
foreach rule {even optimal joint} {
  mincut_placement -k 2 -engine tritonpart -seed 1 -threads 1 \
    -pad_left 1 -pad_right 1 -leaf_positions $rule -leaf_pad 1 \
    -report mincut_place06.json
  set report [open mincut_place06.json r]
  set json [read $report]
  close $report
  regexp "\"leaf_positions\": \"(\[a-z\]+)\"" $json -> value
  regexp "\"hpwl_um\": (\[0-9.\]+)" $json -> wl
  puts "$rule: leaf_positions $value"
  lappend hpwl $wl
  file delete mincut_place06.json
  detailed_placement
  check_placement -verbose
}
lassign $hpwl even optimal joint
puts "optimal <= even: [expr {$optimal <= $even}]"
puts "joint <= even: [expr {$joint <= $even}]"
