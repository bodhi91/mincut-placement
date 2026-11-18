# Dumps a placed database as plain text for exp/fold.py (E27): cells with
# their boxes, I/O pins with their centres, and every signal net with the
# absolute positions of its pins.  All coordinates in dbu.
#
#   MCP_DB=<placed.db> MCP_OUT=<dump.txt> openroad -exit bench/dump_place.tcl
#
# Format (one record per line):
#   dbu <units per micron>
#   cell <name> <x> <y> <w> <h> <orient>         (lower-left, size)
#   pin <bterm> <x> <y>                           (bbox centre)
#   net <name> <nterms> {i <inst> <x> <y> | p <bterm> <x> <y>}...
set db [expr { [info exists ::env(MCP_DB)] ? $::env(MCP_DB) : "" }]
set out [expr { [info exists ::env(MCP_OUT)] ? $::env(MCP_OUT) : "" }]
if { $db == "" || $out == "" } {
  puts "MCP_DB and MCP_OUT must be set"
  exit 1
}
read_db $db
set block [ord::get_db_block]
set f [open $out w]
puts $f "dbu [$block getDbUnitsPerMicron]"
foreach inst [$block getInsts] {
  set bbox [$inst getBBox]
  puts $f "cell [$inst getName] [$bbox xMin] [$bbox yMin] [$bbox getDX] [$bbox getDY] [$inst getOrient]"
}
foreach bt [$block getBTerms] {
  set bbox [$bt getBBox]
  puts $f "pin [$bt getName] [expr { ([$bbox xMin] + [$bbox xMax]) / 2 }] [expr { ([$bbox yMin] + [$bbox yMax]) / 2 }]"
}
set nnets 0
foreach net [$block getNets] {
  if { [$net isSpecial] } { continue }
  set sig [$net getSigType]
  if { $sig == "POWER" || $sig == "GROUND" } { continue }
  set its [$net getITerms]
  set bts [$net getBTerms]
  if { [llength $its] + [llength $bts] < 2 } { continue }
  set rec "net [$net getName] [expr { [llength $its] + [llength $bts] }]"
  foreach it $its {
    lassign [$it getAvgXY] ok x y
    if { !$ok } {
      set bbox [$it getBBox]
      set x [expr { ([$bbox xMin] + [$bbox xMax]) / 2 }]
      set y [expr { ([$bbox yMin] + [$bbox yMax]) / 2 }]
    }
    append rec " i [[$it getInst] getName] $x $y"
  }
  foreach bt $bts {
    set bbox [$bt getBBox]
    append rec " p [$bt getName] [expr { ([$bbox xMin] + [$bbox xMax]) / 2 }] [expr { ([$bbox yMin] + [$bbox yMax]) / 2 }]"
  }
  puts $f $rec
  incr nnets
}
close $f
puts "dumped [llength [$block getInsts]] cells, [llength [$block getBTerms]] pins, $nnets nets to $out"
exit
