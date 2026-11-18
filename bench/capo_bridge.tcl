# Capo (UMpack CapoTest1) bridge through Bookshelf: export of the current
# block, import of Capo's .pl.  Shared by bench/capo.tcl (the Capo arm)
# and bench/mincut.tcl (MCP_PRE=capo: Capo as the seed of a seeded
# mincut_placement).  Globals: ::capo_pad_dbu (padding per side, dbu),
# ::capo_soft_blockages (ignore|block).

# Bookshelf export.  Nodes: every instance (movable: width height;
# fixed: width height terminal) and every I/O pin (1x1 terminal).  Nets:
# signal nets with >= 2 pins; pin offsets from the master geometry for
# orientation N, relative to the cell center (Capo's convention).
proc capo_skip_net { net } {
  if { [$net isSpecial] } { return 1 }
  set sig [$net getSigType]
  if { $sig == "POWER" || $sig == "GROUND" } { return 1 }
  if { [llength [$net getITerms]] + [llength [$net getBTerms]] < 2 } { return 1 }
  return 0
}

proc capo_export { prefix } {
  set block [ord::get_db_block]
  set pad $::capo_pad_dbu
  set base [file tail $prefix]
  set insts [$block getInsts]
  set bterms [$block getBTerms]

  set n_fixed 0
  set fixed_lines {}
  set mov_lines {}
  set pl_lines {}
  foreach inst $insts {
    set master [$inst getMaster]
    set w [$master getWidth]
    set h [$master getHeight]
    set name [$inst getName]
    if { [$inst isFixed] } {
      incr n_fixed
      lappend fixed_lines [format "\t%s\t%d\t%d\tterminal" $name $w $h]
      set bbox [$inst getBBox]
      lappend pl_lines [format "%s\t%d\t%d\t: %s /FIXED" $name \
        [$bbox xMin] [$bbox yMin] [$inst getOrient]]
    } else {
      lappend mov_lines [format "\t%s\t%d\t%d" $name [expr { $w + 2 * $pad }] $h]
      # unplaced cells still need a line; Capo ignores the location
      lappend pl_lines [format "%s\t0\t0\t: N" $name]
    }
  }
  foreach bterm $bterms {
    set name [$bterm getName]
    set bbox [$bterm getBBox]
    set cx [expr { ([$bbox xMin] + [$bbox xMax]) / 2 }]
    set cy [expr { ([$bbox yMin] + [$bbox yMax]) / 2 }]
    incr n_fixed
    lappend fixed_lines [format "\t%s\t1\t1\tterminal" $name]
    lappend pl_lines [format "%s\t%d\t%d\t: N /FIXED" $name $cx $cy]
  }
  # Blockages as terminals: hard ones always, soft ones on request.  The
  # names cannot clash with instances (the importer looks nodes up by
  # instance name and skips unknown terminals).
  set n_blockages 0
  foreach blockage [$block getBlockages] {
    if { [$blockage isSoft] && $::capo_soft_blockages != "block" } { continue }
    set bbox [$blockage getBBox]
    set name "__blockage_${n_blockages}__"
    incr n_blockages
    incr n_fixed
    lappend fixed_lines [format "\t%s\t%d\t%d\tterminal" $name \
      [expr { [$bbox xMax] - [$bbox xMin] }] [expr { [$bbox yMax] - [$bbox yMin] }]]
    lappend pl_lines [format "%s\t%d\t%d\t: N /FIXED" $name [$bbox xMin] [$bbox yMin]]
  }
  if { $n_blockages > 0 } {
    puts "Blockages exported as terminals: $n_blockages (soft blockages: $::capo_soft_blockages)"
  }

  set f [open $prefix.nodes w]
  puts $f "UCLA nodes 1.0"
  puts $f "NumNodes : [expr { [llength $insts] + [llength $bterms] + $n_blockages }]"
  puts $f "NumTerminals : $n_fixed"
  foreach l $mov_lines { puts $f $l }
  foreach l $fixed_lines { puts $f $l }
  close $f

  set f [open $prefix.pl w]
  puts $f "UCLA pl 1.0"
  foreach l $pl_lines { puts $f $l }
  close $f

  # nets
  set net_lines {}
  set n_nets 0
  set n_pins 0
  foreach net [$block getNets] {
    if { [capo_skip_net $net] } { continue }
    set pins {}
    foreach iterm [$net getITerms] {
      set inst [$iterm getInst]
      set master [$inst getMaster]
      set mterm [$iterm getMTerm]
      set mb [$mterm getBBox]
      # offset of the pin center from the cell center, orientation N
      set ox [expr { ([$mb xMin] + [$mb xMax]) / 2.0 - [$master getWidth] / 2.0 }]
      set oy [expr { ([$mb yMin] + [$mb yMax]) / 2.0 - [$master getHeight] / 2.0 }]
      switch -- [$mterm getIoType] {
        OUTPUT { set dir O }
        INPUT { set dir I }
        default { set dir B }
      }
      lappend pins [format "\t%s\t%s : %.1f %.1f" [$inst getName] $dir $ox $oy]
    }
    foreach bterm [$net getBTerms] {
      switch -- [$bterm getIoType] {
        INPUT { set dir O }
        OUTPUT { set dir I }
        default { set dir B }
      }
      lappend pins [format "\t%s\t%s : 0.0 0.0" [$bterm getName] $dir]
    }
    if { [llength $pins] < 2 } { continue }
    incr n_nets
    incr n_pins [llength $pins]
    lappend net_lines "NetDegree : [llength $pins]\t[$net getName]"
    foreach p $pins { lappend net_lines $p }
  }
  set f [open $prefix.nets w]
  puts $f "UCLA nets 1.0"
  puts $f "NumNets : $n_nets"
  puts $f "NumPins : $n_pins"
  foreach l $net_lines { puts $f $l }
  close $f

  set f [open $prefix.wts w]
  puts $f "UCLA wts 1.0"
  close $f

  # rows.  Around macros OpenROAD cuts a row into several dbRows with the
  # same y; Capo refuses two CoreRows at one coordinate, so the pieces of
  # a row become subrows of one CoreRow (sorted by x).
  set rows [$block getRows]
  set by_y [dict create]
  foreach row $rows {
    set origin [$row getOrigin]
    dict lappend by_y [lindex $origin 1] $row
  }
  set f [open $prefix.scl w]
  puts $f "UCLA scl 1.0"
  puts $f "NumRows : [dict size $by_y]"
  foreach y [lsort -integer [dict keys $by_y]] {
    set pieces [dict get $by_y $y]
    set row [lindex $pieces 0]
    set site [$row getSite]
    set spacing [$row getSpacing]
    if { $spacing <= 0 } { set spacing [$site getWidth] }
    puts $f "CoreRow Horizontal"
    puts $f " Coordinate   : $y"
    puts $f " Height       : [$site getHeight]"
    puts $f " Sitewidth    : [$site getWidth]"
    puts $f " Sitespacing  : $spacing"
    puts $f " Siteorient   : [$row getOrient]"
    puts $f " Sitesymmetry : Y"
    set subrows {}
    foreach piece $pieces {
      lappend subrows [list [lindex [$piece getOrigin] 0] [$piece getSiteCount]]
    }
    foreach sub [lsort -integer -index 0 $subrows] {
      puts $f " SubrowOrigin : [lindex $sub 0]\tNumsites : [lindex $sub 1]"
    }
    puts $f "End"
  }
  close $f

  set f [open $prefix.aux w]
  puts $f "RowBasedPlacement : $base.nodes $base.nets $base.wts $base.pl $base.scl"
  close $f
  puts "Bookshelf written: [llength $mov_lines] movable, $n_fixed terminals, $n_nets nets, $n_pins pins, [dict size $by_y] rows ([llength $rows] subrows)"
}

# Reads Capo's .pl: lower-left corner and orientation of every movable cell.
proc capo_import { pl_file } {
  set block [ord::get_db_block]
  set f [open $pl_file r]
  set n 0
  while { [gets $f line] >= 0 } {
    set line [string trim $line]
    if { $line == "" || [string index $line 0] == "#" || [string match "UCLA*" $line] } {
      continue
    }
    if { [string first "FIXED" $line] >= 0 } { continue }
    set tokens [split [regsub -all {\s+} $line " "] " "]
    set name [lindex $tokens 0]
    set x [expr { int(round([lindex $tokens 1])) + $::capo_pad_dbu }]
    set y [expr { int(round([lindex $tokens 2])) }]
    set orient N
    set idx [lsearch -exact $tokens ":"]
    if { $idx >= 0 && $idx + 1 < [llength $tokens] } {
      set orient [lindex $tokens [expr { $idx + 1 }]]
    }
    set inst [$block findInst $name]
    if { $inst == "NULL" || [$inst isFixed] } { continue }
    # Bookshelf (DEF) orientation names -> odb names
    switch -- $orient {
      N { set orient R0 } S { set orient R180 } E { set orient R90 } W { set orient R270 }
      FN { set orient MY } FS { set orient MX } FE { set orient MYR90 } FW { set orient MXR90 }
    }
    $inst setOrient $orient
    $inst setLocation $x $y
    $inst setPlacementStatus PLACED
    incr n
  }
  close $f
  puts "Capo placement read: $n cells"
}
