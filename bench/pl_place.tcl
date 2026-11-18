# "Placer" that loads cell positions from a .pl written by exp/fold.py
# (E27 Phase 4): the folded strip placement is put on the square checkpoint
# and then evaluated exactly like every other variant (bench/common.tcl
# mcp_eval: detailed_placement, HPWL).
#
#   MCP_DESIGN=aes_ar1_nangate45 MCP_PL=<fold.pl> MCP_OUT=<prefix> \
#     openroad -exit bench/pl_place.tcl
#
# The file format and the orientation rule are those of mcp_pl_import
# (bench/common.tcl).

source [file join [file dirname [file normalize [info script]]] common.tcl]

set design [mcp_env MCP_DESIGN ""]
set pl [mcp_env MCP_PL ""]
set seed [mcp_env MCP_SEED 0]
set variant [mcp_env MCP_VARIANT pl]
if { $design == "" || $pl == "" } {
  puts "MCP_DESIGN and MCP_PL must be set"
  exit 1
}
if { ![file exists $pl] } {
  puts "$pl does not exist"
  exit 1
}

mcp_run_variant $design $variant $seed [list mcp_pl_import $pl]
