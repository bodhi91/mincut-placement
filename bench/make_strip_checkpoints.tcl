# Builds the E27 strip floorplan of a square checkpoint (STRIP_FOLD_PLAN.md,
# Section 1): the same netlist on a core of aspect ratio 1/m^2 that folds
# back onto the square by an m x m snake of chunks.
#
#   MCP_STRIP_SRC=aes_ar1_nangate45 MCP_STRIP_M=7 \
#     openroad -exit bench/make_strip_checkpoints.tcl
#
# Square: core origin (cx0, cy0), S sites per row, R rows, site width s,
# row pitch p.  With T = floor(S/m) sites (tile width) and R' = m*floor(R/m)
# rows, the strip core is m^2 T sites wide and R'/m rows tall; chunk i is
# the strip column [i T, (i+1) T), tile (r, c) of the square is
# [c T, (c+1) T) x [r R'/m, (r+1) R'/m) rows, and the snake maps chunk i to
# r = i div m, c = i mod m on even r and m-1-(i mod m) on odd r.  The die
# keeps the square's margins (the top margin absorbs the R-R' unused rows).
#
# Every I/O pin is moved by the inverse snake: a pin box at the square's
# tile (r, c) = chunk i is translated by ((i - c) T, -r R'/m rows), so
# bottom- and top-edge pins land on the strip's bottom and top edges in
# chunks 0..m-1 and m^2-m..m^2-1, and side pins land on interior chunk
# boundaries.  A side pin sits in the die margin just outside its chunk,
# so geometrically it lies in the neighbouring chunk: the forward map must
# treat a pin inside the core's y range that is just left of chunk
# boundary k as belonging to chunk k and one just right of it as chunk
# k-1 (unambiguous: at every boundary only one neighbour has that side on
# the square's edge).  "Inside the core's y range" is [cy0, cy0 + H'/m +
# (R - R') p): side pins beside the unused top rows keep their y and sit
# just above the strip core, below where the top-edge pins start.  strip.json records each pin's chunk and both boxes
# so exp/fold.py can restore pins without geometric inference and assert
# the round trip.
#
# Writes bench/designs/<design>_strip<m>_<platform>/{floorplan.db, vars.tcl,
# design.sdc, strip.json}; vars.tcl is the square's with strip_m and
# strip_src added.

source [file join [file dirname [file normalize [info script]]] common.tcl]

set src [mcp_env MCP_STRIP_SRC ""]
set m [mcp_env MCP_STRIP_M 7]
if { $src == "" } {
  puts "MCP_STRIP_SRC (square checkpoint name) must be set"
  exit 1
}
if { ![regexp {^(.*)_ar1_([a-z0-9]+)$} $src -> design platform] } {
  puts "MCP_STRIP_SRC must be <design>_ar1_<platform>"
  exit 1
}

set src_dir [mcp_design_dir $src]
uplevel #0 [list source [file join $src_dir vars.tcl]]
read_db [file join $src_dir floorplan.db]
set block [ord::get_db_block]
set dbu [$block getDbUnitsPerMicron]

foreach inst [$block getInsts] {
  if { [$inst isFixed] } {
    puts "$src has fixed instances (macros); E27 is for macro-free designs"
    exit 1
  }
}

# ---- square geometry ------------------------------------------------------
set die [$block getDieArea]
set core [$block getCoreArea]
set cx0 [$core xMin]
set cy0 [$core yMin]
set cx1 [$core xMax]
set cy1 [$core yMax]
set rows [$block getRows]
set R [llength $rows]
set row0 ""
foreach row $rows {
  if { $row0 == "" || [[$row getBBox] yMin] < [[$row0 getBBox] yMin] } {
    set row0 $row
  }
}
set site_obj [$row0 getSite]
set site_name [$site_obj getName]
set s [$site_obj getWidth]
set p [$site_obj getHeight]
set S [$row0 getSiteCount]
set row0_orient [$row0 getOrient]
foreach row $rows {
  if { [$row getSiteCount] != $S || [[$row getSite] getName] != $site_name } {
    puts "rows of $src are not uniform; unsupported"
    exit 1
  }
}
if { [expr { $cx1 - $cx0 }] != $S * $s || [expr { $cy1 - $cy0 }] != $R * $p } {
  puts "core of $src is not an exact row/site grid; unsupported"
  exit 1
}

set T [expr { $S / $m }]                ;# tile width in sites
set Rs [expr { $R / $m }]               ;# strip rows (= rows per tile)
set Rp [expr { $Rs * $m }]              ;# R'
set tile_w [expr { $T * $s }]
set tile_h [expr { $Rs * $p }]
set strip_w [expr { $m * $m * $tile_w }]
set strip_h $tile_h
set sx1 [expr { $cx0 + $strip_w }]
set sy1 [expr { $cy0 + $strip_h }]
# die margins: those of the square measured from the used m T sites and
# R' rows, so edge pins beyond the unused sites/rows stay inside the die
set die_x1 [expr { $sx1 + ([$die xMax] - ($cx0 + $m * $tile_w)) }]
set die_y1 [expr { $sy1 + ([$die yMax] - ($cy0 + $Rp * $p)) }]

puts "$src: core $S sites x $R rows (site $s x $p dbu), die\
  [$die xMin] [$die yMin] [$die xMax] [$die yMax]"
puts "strip m=$m: tile $T sites x $Rs rows, strip [expr { $m * $m * $T }]\
  sites x $Rs rows = [expr { $strip_w / double($dbu) }] x\
  [expr { $strip_h / double($dbu) }] um, unused [expr { $S - $m * $T }]\
  sites and [expr { $R - $Rp }] rows of the square"

# ---- pins: record boxes, compute inverse-snake translations ---------------
proc snake_chunk { r c m } {
  if { $r % 2 == 0 } { return [expr { $r * $m + $c }] }
  return [expr { $r * $m + ($m - 1 - $c) }]
}
proc clamp { v lo hi } {
  if { $v < $lo } { return $lo }
  if { $v > $hi } { return $hi }
  return $v
}
set pins {}          ;# list of {bterm_name layer_name x0 y0 x1 y1 status}
set n_boxes 0
set side_count [dict create B 0 T 0 L 0 R 0 I 0]
foreach bt [$block getBTerms] {
  foreach bp [$bt getBPins] {
    set status [$bp getPlacementStatus]
    foreach box [$bp getBoxes] {
      set x0 [$box xMin]; set y0 [$box yMin]; set x1 [$box xMax]; set y1 [$box yMax]
      set px [expr { ($x0 + $x1) / 2 }]
      set py [expr { ($y0 + $y1) / 2 }]
      set c [clamp [expr { int(floor(double($px - $cx0) / $tile_w)) }] 0 [expr { $m - 1 }]]
      set r [clamp [expr { int(floor(double($py - $cy0) / $tile_h)) }] 0 [expr { $m - 1 }]]
      set i [snake_chunk $r $c $m]
      set dx [expr { ($i - $c) * $tile_w }]
      set dy [expr { -$r * $tile_h }]
      lappend pins [list [$bt getName] [[$box getTechLayer] getName] \
        [expr { $x0 + $dx }] [expr { $y0 + $dy }] [expr { $x1 + $dx }] [expr { $y1 + $dy }] \
        $status $r $c $i $x0 $y0 $x1 $y1]
      incr n_boxes
      if { $x1 <= $cx0 } { dict incr side_count L } \
      elseif { $x0 >= $cx1 } { dict incr side_count R } \
      elseif { $y1 <= $cy0 } { dict incr side_count B } \
      elseif { $y0 >= $cy1 } { dict incr side_count T } \
      else { dict incr side_count I }
    }
  }
}
puts "pins: $n_boxes boxes, by square side $side_count"

# ---- rebuild the floorplan -------------------------------------------------
set um [expr { double($dbu) }]
initialize_floorplan \
  -die_area [list [expr { [$die xMin] / $um }] [expr { [$die yMin] / $um }] \
                  [expr { $die_x1 / $um }] [expr { $die_y1 / $um }]] \
  -core_area [list [expr { $cx0 / $um }] [expr { $cy0 / $um }] \
                   [expr { $sx1 / $um }] [expr { $sy1 / $um }]] \
  -site $site_name

set new_rows [$block getRows]
if { [llength $new_rows] != $Rs } {
  puts "expected $Rs strip rows, got [llength $new_rows]"
  exit 1
}
foreach row $new_rows {
  if { [$row getSiteCount] != $m * $m * $T } {
    puts "strip row [$row getName] has [$row getSiteCount] sites, expected [expr { $m * $m * $T }]"
    exit 1
  }
}
# the bottom row of the strip must match the square's bottom row (same
# orientation, same y) so a folded cell in strip row j lands in a square
# row of the same orientation whenever (R'/m) is even, and the fold knows
# which rows to flip otherwise.
proc bottom_row { rows } {
  set best ""
  foreach row $rows {
    if { $best == "" || [[$row getBBox] yMin] < [[$best getBBox] yMin] } {
      set best $row
    }
  }
  return $best
}
set r0 [bottom_row $new_rows]
if { [$r0 getOrient] != $row0_orient || [[$r0 getBBox] yMin] != $cy0 } {
  puts "strip bottom row differs from the square's: [$r0 getOrient] y\
    [[$r0 getBBox] yMin] vs $row0_orient y $cy0"
  exit 1
}

# pins: drop the old bpins, create the translated boxes
set tech [ord::get_db_tech]
set moved 0
foreach bt [$block getBTerms] {
  foreach bp [$bt getBPins] {
    odb::dbBPin_destroy $bp
  }
}
foreach rec $pins {
  lassign $rec name layer x0 y0 x1 y1 status
  set bt [$block findBTerm $name]
  set bp [odb::dbBPin_create $bt]
  odb::dbBox_create $bp [$tech findLayer $layer] $x0 $y0 $x1 $y1
  $bp setPlacementStatus $status
  if { $x0 < [$die xMin] || $y0 < [$die yMin] || $x1 > $die_x1 || $y1 > $die_y1 } {
    puts "pin $name box $x0 $y0 $x1 $y1 is outside the strip die"
    exit 1
  }
  incr moved
}
puts "pins: $moved boxes re-created"

# ---- write the checkpoint ----------------------------------------------------
set name "${design}_strip${m}_${platform}"
set dir [file join $::mcp_bench_dir designs $name]
file mkdir $dir
write_db [file join $dir floorplan.db]
file copy -force [file join $src_dir design.sdc] [file join $dir design.sdc]

set f [open [file join $dir vars.tcl] w]
puts $f "# generated by bench/make_strip_checkpoints.tcl from $src, m = $m"
set in [open [file join $src_dir vars.tcl] r]
while { [gets $in line] >= 0 } {
  if { [string match "#*" $line] } { continue }
  if { [string match "set sdc_file *" $line] } {
    puts $f "set sdc_file [list [file join $dir design.sdc]]"
  } else {
    puts $f $line
  }
}
close $in
puts $f "set strip_m $m"
puts $f "set strip_src $src"
close $f

set f [open [file join $dir strip.json] w]
puts $f "{"
puts $f "  \"src\": \"$src\", \"m\": $m, \"dbu\": $dbu,"
puts $f "  \"site_width\": $s, \"row_pitch\": $p,"
puts $f "  \"square\": {\"cx0\": $cx0, \"cy0\": $cy0, \"cx1\": $cx1, \"cy1\": $cy1,\
  \"sites\": $S, \"rows\": $R,\
  \"die\": \[[$die xMin], [$die yMin], [$die xMax], [$die yMax]\]},"
puts $f "  \"tile_sites\": $T, \"tile_rows\": $Rs, \"tile_w\": $tile_w, \"tile_h\": $tile_h,"
puts $f "  \"used_sites\": [expr { $m * $T }], \"used_rows\": $Rp,"
puts $f "  \"strip\": {\"cx0\": $cx0, \"cy0\": $cy0, \"cx1\": $sx1, \"cy1\": $sy1,\
  \"die\": \[[$die xMin], [$die yMin], $die_x1, $die_y1\]},"
puts $f "  \"pins\": \["
set first 1
foreach rec $pins {
  lassign $rec name layer x0 y0 x1 y1 status r c i qx0 qy0 qx1 qy1
  if { !$first } { puts $f "," }
  set first 0
  puts -nonewline $f "    {\"name\": \"$name\", \"tile\": \[$r, $c\], \"chunk\": $i,\
    \"square\": \[$qx0, $qy0, $qx1, $qy1\], \"strip\": \[$x0, $y0, $x1, $y1\]}"
}
puts $f ""
puts $f "  \]"
puts $f "}"
close $f
puts "wrote $dir"
exit
