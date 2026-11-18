# Builds a placement checkpoint for one OpenROAD/test flow design.
#
#   MCP_DESIGN=<design>_<platform> openroad -exit bench/make_checkpoints.tcl
#
# (openroad does not forward script arguments, hence the environment
# variable.)  Runs OpenROAD/test/<design>_<platform>.tcl and flow.tcl up
# to and including pin placement (floorplan, macro placement, tap cells,
# PDN, a first global placement with -skip_io, place_pins), then unplaces
# the standard cells and writes bench/designs/<design>_<platform>/
# {floorplan.db, design.sdc, vars.tcl}.  Every placer under test starts
# from exactly this database.

source [file join [file dirname [file normalize [info script]]] common.tcl]

set bench_name [mcp_env MCP_DESIGN ""]
if { $bench_name == "" } {
  puts "usage: MCP_DESIGN=<design>_<platform> openroad -exit make_checkpoints.tcl"
  exit 1
}
set design_script [file join $::mcp_or_test_dir ${bench_name}.tcl]
if { ![file exists $design_script] } {
  puts "no such design script: $design_script"
  exit 1
}
cd $::mcp_or_test_dir

# Evaluate the design script without its trailing "include flow.tcl".
set fp [open $design_script r]
set script [read $fp]
close $fp
set lines {}
foreach line [split $script "\n"] {
  if { [regexp {^\s*include\s} $line] } {
    continue
  }
  lappend lines $line
}
eval [join $lines "\n"]

# flow.tcl up to and including place_pins.
read_libraries
read_verilog $synth_verilog
link_design $top_module
read_sdc $sdc_file

set_thread_count [cpu_count]
sta::set_thread_count 1

initialize_floorplan -site $site \
  -die_area $die_area \
  -core_area $core_area

source $tracks_file

remove_buffers

if { $pre_placed_macros_file != "" } {
  source $pre_placed_macros_file
}

if { [have_macros] } {
  lassign $macro_place_halo halo_x halo_y
  set_macro_base_halo $halo_x $halo_y
  set report_dir [make_result_file ${design}_${platform}_rtlmp]
  rtl_macro_placer -report_directory $report_dir
}

eval tapcell $tapcell_args ;# tclint-disable command-args

source $pdn_cfg
pdngen

foreach layer_adjustment $global_routing_layer_adjustments {
  lassign $layer_adjustment layer adjustment
  set_global_routing_layer_adjustment $layer $adjustment
}
set_routing_layers -signal $global_routing_layers \
  -clock $global_routing_clock_layers
set_macro_extension 2

global_placement -density $global_place_density \
  -pad_left $global_place_pad -pad_right $global_place_pad -skip_io

place_pins -hor_layers $io_placer_hor_layer -ver_layers $io_placer_ver_layer

# Standard cells start unplaced; macros, taps and pins stay fixed.
set unplaced [mcp_unplace_std_cells]
puts "checkpoint: $unplaced standard cells unplaced"

set out_dir [mcp_design_dir $bench_name]
file mkdir $out_dir
write_db [file join $out_dir floorplan.db]
write_sdc [file join $out_dir design.sdc]
mcp_write_vars [file join $out_dir vars.tcl]
puts "checkpoint written to $out_dir"
