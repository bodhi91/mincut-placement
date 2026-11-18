# Global placement baselines (RePlAce / gpl) on a checkpoint.
#
#   MCP_DESIGN=gcd_nangate45 MCP_MODE=default MCP_OUT=runs/gcd_gpl \
#     openroad -exit -log runs/gcd_gpl.log -metrics runs/gcd_gpl.metrics.json \
#     bench/gpl_baseline.tcl
#
# MCP_MODE: default | timing_driven | routability_driven | skip_nesterov | nopad | krow
#   krow: the baseline of the k-row floorplans (k-row-floorplans/replace):
#   -disable_pin_density_adjust -overflow 0.2 (E17)
# Writes MCP_OUT.db and MCP_OUT.eval.json.

source [file join [file dirname [file normalize [info script]]] common.tcl]

set design [mcp_env MCP_DESIGN ""]
set mode [mcp_env MCP_MODE default]
set seed [mcp_env MCP_SEED 0]
if { $design == "" } {
  puts "MCP_DESIGN is not set"
  exit 1
}

switch -- $mode {
  default { set extra {} }
  timing_driven { set extra {-timing_driven} }
  routability_driven { set extra {-routability_driven} }
  skip_nesterov { set extra {-skip_nesterov_place} }
  nopad { set extra {} }
  krow { set extra {-disable_pin_density_adjust -overflow 0.2} }
  default {
    # ovf<tau>: gpl stopped at density overflow tau -- the seed of the
    # seeded mincut_placement arms (bench/mincut.tcl MCP_PRE=gpl_ovf<tau>)
    # evaluated on its own, after legalization (AMERICAN_MAPS_STUDY.md,
    # arm "seed").
    if { [regexp {^ovf([0-9.]+)$} $mode -> tau] } {
      set extra "-overflow $tau"
    } elseif { [regexp {^krow_ovf([0-9.]+)$} $mode -> tau] } {
      # krow baseline with another stopping overflow (R6: RePlAce at its
      # default 0.1 on the few-row set, to show the 0.2 stop is not what
      # limits it there).
      set extra "-disable_pin_density_adjust -overflow $tau"
    } else {
      puts "unknown MCP_MODE '$mode'"
      exit 1
    }
  }
}

# nopad: the density applies to the unpadded cell area (Capo's reading of
# -densityTarget, E14); otherwise the flow's padding, as ORFS.
if { $mode == "nopad" } {
  set pad 0
} else {
  set pad {$::global_place_pad}
}
mcp_run_variant $design gpl_$mode $seed [subst -nocommands {
  global_placement -density \$::global_place_density \
    -pad_left $pad -pad_right $pad $extra
}]
