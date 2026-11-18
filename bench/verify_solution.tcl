# Recomputes the HPWL of a placement solution from its DEF and the
# platform LEFs, and checks that it is legal.  Independent of the
# benchmark checkpoints: only the public PDK files under OpenROAD/test
# are needed.
#
#   MCP_DEF=<solution.def[.gz]> MCP_PLATFORM=<nangate45|asap7> \
#     [MCP_LEF_DIR=<OpenROAD/test>] openroad -exit bench/verify_solution.tcl
#
# Prints "hpwl_um <value>" (the same sum as the reported HPWL: all
# signal nets with at least two pins, bounding box of the pin shapes,
# in microns) and the
# result of check_placement.

proc env_or { name default } {
  if { [info exists ::env($name)] } {
    return $::env($name)
  }
  return $default
}

set def [env_or MCP_DEF ""]
set platform [env_or MCP_PLATFORM ""]
set lef_dir [env_or MCP_LEF_DIR \
  [file normalize [file join [file dirname [info script]] .. OpenROAD test]]]
if { $def == "" || $platform == "" } {
  puts "usage: MCP_DEF=<def> MCP_PLATFORM=<nangate45|asap7> openroad -exit verify_solution.tcl"
  exit 1
}

switch -- $platform {
  nangate45 {
    set lefs [list Nangate45/Nangate45_tech.lef Nangate45/Nangate45_stdcell.lef]
    foreach f [glob -nocomplain -directory [file join $lef_dir Nangate45] fakeram45_*.lef] {
      lappend lefs [file join Nangate45 [file tail $f]]
    }
  }
  asap7 {
    set lefs [list asap7/asap7_tech_1x_201209.lef \
      asap7/asap7sc7p5t_28_L_1x_220121a.lef \
      asap7/asap7sc7p5t_28_R_1x_220121a.lef \
      asap7/asap7sc7p5t_28_SL_1x_220121a.lef]
    foreach f [glob -nocomplain -directory [file join $lef_dir asap7] fakeram*.lef] {
      lappend lefs [file join asap7 [file tail $f]]
    }
  }
  default { puts "unknown platform $platform"; exit 1 }
}
foreach lef $lefs {
  read_lef [file join $lef_dir $lef]
}

if { [string match *.gz $def] } {
  set tmp [file rootname $def]
  exec gzip -dkf $def
  read_def $tmp
  file delete $tmp
} else {
  read_def $def
}

proc solution_hpwl { } {
  set block [ord::get_db_block]
  set dbu [$block getDbUnitsPerMicron]
  set total 0
  foreach net [$block getNets] {
    if { [$net isSpecial] } { continue }
    set sig [$net getSigType]
    if { $sig == "POWER" || $sig == "GROUND" } { continue }
    if { [llength [$net getITerms]] + [llength [$net getBTerms]] < 2 } { continue }
    set bbox [$net getTermBBox]
    set total [expr { $total + [$bbox dx] + [$bbox dy] }]
  }
  return [expr { double($total) / $dbu }]
}

set block [ord::get_db_block]
set placed 0; set unplaced 0
foreach inst [$block getInsts] {
  if { [$inst isFixed] } { continue }
  if { [$inst isPlaced] } { incr placed } else { incr unplaced }
}
puts "movable_placed $placed  movable_unplaced $unplaced"
puts [format "hpwl_um %.1f" [solution_hpwl]]
if { [catch { check_placement -verbose } err] } {
  puts "legal 0 ($err)"
} else {
  puts "legal 1"
}
