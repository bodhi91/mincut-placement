# Writes a Bookshelf .pl of a placed database of a converted ICCAD04 design
# (bench/bookshelf2lefdef.py: 1 Bookshelf unit = 1 um, node names kept), for
# WLCalc / bench/bookshelf_hpwl.py cross-checks and Bookshelf tools.
#
#   openroad -exit bench/write_pl.tcl   with
#   MCP_DB   placed database          MCP_PL   output .pl
set db [expr { [info exists ::env(MCP_DB)] ? $::env(MCP_DB) : "" }]
set out [expr { [info exists ::env(MCP_PL)] ? $::env(MCP_PL) : "" }]
if { $db == "" || $out == "" } {
  puts "MCP_DB and MCP_PL must be set"
  exit 1
}
read_db $db
set block [ord::get_db_block]
set dbu [$block getDbUnitsPerMicron]
set f [open $out w]
puts $f "UCLA pl 1.0"
puts $f "# written by bench/write_pl.tcl from [file tail $db]"
puts $f ""
foreach inst [$block getInsts] {
  set loc [$inst getLocation]
  set x [expr { double([lindex $loc 0]) / $dbu }]
  set y [expr { double([lindex $loc 1]) / $dbu }]
  set tag [expr { [$inst isFixed] ? " /FIXED" : "" }]
  puts $f [format "%s %.4f %.4f : N%s" [$inst getName] $x $y $tag]
}
# terminals: 1x1 nodes at the pin center -> lower-left = center - 0.5
foreach bterm [$block getBTerms] {
  set name [$bterm getName]
  regsub {_[0-9]+$} $name {} base
  if { [info exists seen($base)] } {
    continue
  }
  set seen($base) 1
  set bbox [$bterm getBBox]
  set x [expr { ([$bbox xMin] + [$bbox xMax]) / 2.0 / $dbu }]
  set y [expr { ([$bbox yMin] + [$bbox yMax]) / 2.0 / $dbu }]
  puts $f [format "%s %.4f %.4f : N /FIXED" $base [expr { $x - 0.5 }] [expr { $y - 0.5 }]]
}
close $f
puts "wrote $out"
