# Bookshelf bridge check on a kept Capo placement, part 1: re-export the
# checkpoint through the bridge, import Capo's <run>.capo.pl, and dump the
# OpenDB view (every signal pin center, instance origins and orientations,
# HPWL) for bench/capo_verify.py.
#
#   MCP_DESIGN=<floorplan> MCP_PL=<run>.capo.pl MCP_VDIR=<dir> \
#     openroad -exit bench/capo_verify.tcl > <dir>/verify.log
#
# Prints ODB_HPWL_DBU (HPWL of the imported placement from the real pin
# geometry, I/O pins included) and the number of fixed instances with a
# non-R0 orientation that carry signal pins (where the bridge's R0 pin
# offsets would be the wrong ones; 0 on all our floorplans).
source bench/common.tcl
source bench/capo_bridge.tcl
set design $::env(MCP_DESIGN)
set pl $::env(MCP_PL)
set outdir $::env(MCP_VDIR)
mcp_load_design $design
set block [ord::get_db_block]
set site [[lindex [$block getRows] 0] getSite]
set ::capo_pad_dbu [expr { int($::global_place_pad) * [$site getWidth] }]
set ::capo_soft_blockages ignore
file mkdir $outdir
capo_export $outdir/design
capo_import $pl
# OpenDB view of the imported placement: every signal pin center, and the
# net HPWL from the real pin geometry (orientation applied by OpenDB).
set f [open $outdir/odb_pins.txt w]
foreach inst [$block getInsts] {
  foreach it [$inst getITerms] {
    set net [$it getNet]
    if { $net == "NULL" || [capo_skip_net $net] } continue
    set bb [$it getBBox]
    puts $f [format "%s %s %s %d %d %d" [$inst getName] [[$it getMTerm] getName] [$net getName] \
      [expr {([$bb xMin]+[$bb xMax])/2}] [expr {([$bb yMin]+[$bb yMax])/2}] [$inst isFixed]]
  }
}
# I/O pins, so that the Python side can recompute the complete net HPWL.
foreach bt [$block getBTerms] {
  set net [$bt getNet]
  if { $net == "NULL" || [capo_skip_net $net] } continue
  set bb [$bt getBBox]
  puts $f [format "PIN:%s - %s %d %d 1" [$bt getName] [$net getName] \
    [expr {([$bb xMin]+[$bb xMax])/2}] [expr {([$bb yMin]+[$bb yMax])/2}]]
}
close $f
set f [open $outdir/odb_insts.txt w]
foreach inst [$block getInsts] {
  set bb [$inst getBBox]
  puts $f [format "%s %d %d %s %d" [$inst getName] [$bb xMin] [$bb yMin] [$inst getOrient] [$inst isFixed]]
}
close $f
set dbu [$block getDbUnitsPerMicron]
puts [format "ODB_HPWL_DBU %.0f" [expr {[mcp_hpwl]*$dbu}]]
puts "ODB_NONR0_FIXED_WITH_SIGNAL_PINS [llength [lsort -unique [lmap inst [$block getInsts] {
  if {![$inst isFixed] || [$inst getOrient] eq "R0"} continue
  set has 0
  foreach it [$inst getITerms] { set n [$it getNet]; if {$n ne "NULL" && ![capo_skip_net $n]} {set has 1} }
  if {!$has} continue
  [$inst getName]
}]]]"
