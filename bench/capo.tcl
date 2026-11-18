# Capo (UMpack MetaPlacer / CapoTest1) on a checkpoint, through Bookshelf.
#
#   MCP_DESIGN=gcd_nangate45 MCP_SEED=1 MCP_CAPO=/path/to/CapoTest1.exe \
#   MCP_CAPO_ARGS="-densityTarget 30" MCP_OUT=runs/gcd_capo \
#     openroad -exit -log runs/gcd_capo.log bench/capo.tcl
#
# Capo does not read LEF/DEF in the public UMpack, so the checkpoint is
# written in Bookshelf (UCLA nodes/nets/pl/scl, DBU units, pin offsets
# relative to the cell center, fixed instances and I/O pins as terminals,
# rows as CoreRow entries), CapoTest1 is run on it, and its .pl is read
# back into the database before the standard evaluation (mcp_eval).
# Writes MCP_OUT.capo/design.{aux,nodes,nets,wts,pl,scl,capo.pl,capo.log}
# (under MCP_SCRATCH when set, with the log and .pl copied to MCP_OUT.capo.*),
# MCP_OUT.db and MCP_OUT.eval.json.

source [file join [file dirname [file normalize [info script]]] common.tcl]

set design [mcp_env MCP_DESIGN ""]
set seed [mcp_env MCP_SEED 1]
set capo [mcp_env MCP_CAPO ""]
# MCP_CAPO_ARGS is subst'ed after the design is loaded, so it may refer to
# the platform variables, e.g. "-densityTarget [expr {int(100*\$::global_place_density)}]".
set capo_args_raw [mcp_env MCP_CAPO_ARGS ""]
# MCP_CAPO_PAD: sites of padding on each side of every movable cell, as
# gpl's -pad_left/-pad_right (subst'ed like the arguments, e.g.
# "\$::global_place_pad").  The exported width grows by 2*pad*site and the
# cell is placed pad*site to the right of Capo's lower-left corner, so
# the pin offsets (relative to the center) stay exact.
set capo_pad_raw [mcp_env MCP_CAPO_PAD 0]
set ::capo_pad_dbu 0
# MCP_CAPO_SOFT_BLOCKAGES=block: soft placement blockages (macro halos)
# are exported as fixed terminal nodes of their size, so Capo carves them
# out of the rows like macros (Bookshelf has no blockage object); gpl
# blocks every site under a soft blockage without MAXDENSITY, so this is
# the setting that gives both placers the same constraints.  Default
# "ignore": blockages are not exported (what dpl does).
set ::capo_soft_blockages [mcp_env MCP_CAPO_SOFT_BLOCKAGES ignore]
set out [mcp_env MCP_OUT ""]
if { $design == "" || $capo == "" || $out == "" } {
  puts "MCP_DESIGN, MCP_CAPO and MCP_OUT must be set"
  exit 1
}

source [file join [file dirname [file normalize [info script]]] capo_bridge.tcl]

# Everything Capo reads and writes lives in its own directory: the .aux
# names the files relative to it, and UMpack's random seeds come from a
# seeds.in in the working directory (line 1: 0, line 2: the external seed).
# Capo flushes seeds.out after every random object; on NFS that makes a
# 0.3 s run take 18 s, so the directory goes to MCP_SCRATCH (local disk)
# when it is set, and the log and placement are copied back.
set scratch [mcp_env MCP_SCRATCH ""]
if { $scratch != "" } {
  set capo_dir [file join $scratch [file tail $out].capo]
} else {
  set capo_dir [file normalize $out.capo]
}
file mkdir $capo_dir
set capo_prefix [file join $capo_dir design]

mcp_run_variant $design capo $seed [subst -nocommands {
  set capo_args [subst \$::capo_args_raw]
  set site_width [[lindex [[ord::get_db_block] getRows] 0] getSite]
  set ::capo_pad_dbu [expr { int([subst \$::capo_pad_raw]) * [\$site_width getWidth] }]
  puts "Capo arguments: \$capo_args (padding \$::capo_pad_dbu dbu per side)"
  capo_export $capo_prefix
  set f [open [file join $capo_dir seeds.in] w]
  puts \$f "0\n$seed"
  close \$f
  set t0 [mcp_seconds]
  set here [pwd]
  cd $capo_dir
  set status [catch {
    exec $capo -f design.aux {*}\$capo_args -savePl design.capo.pl \
      > design.capo.log 2>@1
  } err options]
  cd \$here
  set ::capo_seconds [expr { [mcp_seconds] - \$t0 }]
  if { ![file exists $capo_prefix.capo.pl] } {
    puts "Capo failed: \$err"
    exit 1
  }
  capo_import $capo_prefix.capo.pl
  if { "$scratch" != "" } {
    file copy -force $capo_prefix.capo.log $out.capo.log
    file copy -force $capo_prefix.capo.pl $out.capo.pl
  }
  puts "CAPO_SECONDS [format %.2f \$::capo_seconds]"
}]
