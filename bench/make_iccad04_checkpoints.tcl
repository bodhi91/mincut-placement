# Checkpoints of the ICCAD04 (IBM-MSwPins) Bookshelf benchmarks for the
# harness: bench/designs/ibmXX_iccad04/{floorplan.db,vars.tcl}.
#
#   bench/bookshelf2lefdef.py writes <scratch>/<ibmXX>/{<name>.lef,<name>.def,
#   vars.tcl,stats.json} (1 Bookshelf unit = 1 um, pins at the Bookshelf
#   offsets, macros FIXED at RePlAce's positions); this script reads them and
#   writes the database.  Driven by bench/make_iccad04_checkpoints.sh.
#
#   MCP_ICCAD04_SRC   directory with the LEF/DEF of one design
#   MCP_DESIGN        design name (ibm01_iccad04)
source [file join [file dirname [file normalize [info script]]] common.tcl]

set src [mcp_env MCP_ICCAD04_SRC ""]
set design [mcp_env MCP_DESIGN ""]
if { $src == "" || $design == "" } {
  puts "MCP_ICCAD04_SRC and MCP_DESIGN must be set"
  exit 1
}
set dir [mcp_design_dir $design]
file mkdir $dir

read_lef [file join $src $design.lef]
read_def [file join $src $design.def]

set block [ord::get_db_block]
set n_inst [llength [$block getInsts]]
set n_fixed 0
set n_unplaced 0
foreach inst [$block getInsts] {
  if { [$inst isFixed] } {
    incr n_fixed
  } elseif { ![$inst isPlaced] } {
    incr n_unplaced
  }
}
puts [format "%s: %d instances (%d fixed, %d unplaced), %d pins, %d nets, %d rows" \
  $design $n_inst $n_fixed $n_unplaced [llength [$block getBTerms]] \
  [llength [$block getNets]] [llength [$block getRows]]]

write_db [file join $dir floorplan.db]
file copy -force [file join $src vars.tcl] [file join $dir vars.tcl]
file copy -force [file join $src stats.json] [file join $dir stats.json]
puts "checkpoint written to $dir"
