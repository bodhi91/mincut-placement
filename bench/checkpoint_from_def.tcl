# Rebuilds a benchmark checkpoint (bench/designs/<floorplan>/floorplan.db)
# from any placement solution of that floorplan: the DEF carries the die,
# rows, tracks, blockages, fixed instances, I/O pins and the netlist; the
# movable standard cells are unplaced again so that every placer starts
# from the same input, as in the paper.
#
#   MCP_DESIGN=<floorplan> MCP_DEF=<solutions/.../*.def.gz> \
#     openroad -exit bench/checkpoint_from_def.tcl
#
# Needs bench/designs/<floorplan>/vars.tcl (shipped) and the public PDK
# files under OpenROAD/test.  Afterwards the harness runs as usual:
#   exp/run.sh exp/<cfg>.cfg -j 4

set bench_dir [file dirname [file normalize [info script]]]
source [file join $bench_dir common.tcl]

proc env_or { name default } {
  if { [info exists ::env($name)] } { return $::env($name) }
  return $default
}

set design [env_or MCP_DESIGN ""]
set def [env_or MCP_DEF ""]
if { $design == "" || $def == "" } {
  puts "usage: MCP_DESIGN=<floorplan> MCP_DEF=<def> openroad -exit checkpoint_from_def.tcl"
  exit 1
}
set dir [mcp_design_dir $design]
uplevel #0 [list source [file join $dir vars.tcl]]

set test_dir $::mcp_or_test_dir
switch -- $::platform {
  nangate45 {
    set lefs [list Nangate45/Nangate45_tech.lef Nangate45/Nangate45_stdcell.lef]
    foreach f [glob -nocomplain -directory [file join $test_dir Nangate45] fakeram45_*.lef] {
      lappend lefs [file join Nangate45 [file tail $f]]
    }
  }
  asap7 {
    set lefs [list asap7/asap7_tech_1x_201209.lef \
      asap7/asap7sc7p5t_28_L_1x_220121a.lef \
      asap7/asap7sc7p5t_28_R_1x_220121a.lef \
      asap7/asap7sc7p5t_28_SL_1x_220121a.lef]
    foreach f [glob -nocomplain -directory [file join $test_dir asap7] fakeram*.lef] {
      lappend lefs [file join asap7 [file tail $f]]
    }
  }
  default { puts "unknown platform $::platform"; exit 1 }
}
foreach lef $lefs {
  read_lef [file join $test_dir $lef]
}
foreach lib [concat $::liberty_file $::extra_liberty] {
  read_liberty $lib
}

if { [string match *.gz $def] } {
  set plain [file join $dir [file rootname [file tail $def]]]
  exec gzip -dc $def > $plain
  read_def $plain
  file delete $plain
} else {
  read_def $def
}

set block [ord::get_db_block]
set unplaced 0
foreach inst [$block getInsts] {
  if { [$inst isFixed] } { continue }
  $inst setPlacementStatus NONE
  incr unplaced
}
puts "$design: $unplaced movable instances unplaced"
write_db [file join $dir floorplan.db]
puts "checkpoint written to [file join $dir floorplan.db]"
