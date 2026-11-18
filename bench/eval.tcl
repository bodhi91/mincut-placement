# Evaluates an already placed database (any placer) against a checkpoint's
# libraries and constraints.
#
#   MCP_DESIGN=gcd_nangate45 MCP_DB=some/placed.db MCP_OUT=runs/x \
#     openroad -exit -log runs/x.log -metrics runs/x.metrics.json bench/eval.tcl
#
# Writes MCP_OUT.eval.json.  The placed database must come from the same
# checkpoint (same netlist, floorplan and pins).

source [file join [file dirname [file normalize [info script]]] common.tcl]

set design [mcp_env MCP_DESIGN ""]
set db [mcp_env MCP_DB ""]
set out [mcp_env MCP_OUT ""]
if { $design == "" || $db == "" || $out == "" } {
  puts "MCP_DESIGN, MCP_DB and MCP_OUT must be set"
  exit 1
}

set dir [mcp_design_dir $design]
source [file join $dir vars.tcl]
foreach lib [concat $::liberty_file $::extra_liberty] {
  read_liberty $lib
}
read_db $db
read_sdc [file join $dir design.sdc]
source $::layer_rc_file
set_wire_rc -signal -layer $::wire_rc_layer
set_wire_rc -clock -layer $::wire_rc_layer_clk
set_thread_count [mcp_env MCP_THREADS 1]

set result [mcp_eval]
dict set result design $design
dict set result variant [mcp_env MCP_VARIANT external]
dict set result seed [mcp_env MCP_SEED 0]
dict set result db $db
dict set result git_sha [mcp_git_sha]
mcp_write_json ${out}.eval.json $result
puts "evaluation written to ${out}.eval.json"
