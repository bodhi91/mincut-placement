# Min-cut placement (mcp) on a checkpoint.
#
#   MCP_DESIGN=gcd_nangate45 MCP_SEED=1 MCP_THREADS=8 \
#   MCP_ARGS="-k 2 -engine hmetis -hmetis_path tools/hmetis/hmetis_driver" \
#   MCP_OUT=runs/gcd_mcp openroad -exit -log runs/gcd_mcp.log \
#     -metrics runs/gcd_mcp.metrics.json bench/mincut.tcl
#
# MCP_ARGS holds the mincut_placement options except -seed, -threads and
# -report, which the harness sets.  MCP_PRE selects a seed placement
# before mincut_placement (E4, with -seed_placement in MCP_ARGS): none
# (default), gpl_init (global_placement -skip_nesterov_place: the
# quadratic initial placement), gpl (the full global placement, as a
# seed-quality reference) or gpl_ovf<tau> (global_placement stopped at
# density overflow tau, e.g. gpl_ovf0.3; E19), capo, timing (E25 net
# weights) or pl:<file> (a .pl of exp/fold.py, E27).  MCP_POST selects
# what runs on the min-cut result before evaluation (hybrid flows, E7):
# none (default),
# gpl (global_placement -skip_initial_place: Nesterov from the min-cut
# placement) or incremental (global_placement -incremental; in the
# current OpenROAD this places only *unplaced* instances and is a no-op
# after mincut_placement, see E7).  Writes MCP_OUT.db,
# MCP_OUT.mcp.json (placer report) and MCP_OUT.eval.json.

source [file join [file dirname [file normalize [info script]]] common.tcl]

set design [mcp_env MCP_DESIGN ""]
set seed [mcp_env MCP_SEED 0]
set threads [mcp_env MCP_THREADS 1]
set args [mcp_env MCP_ARGS ""]
set pre [mcp_env MCP_PRE none]
set post [mcp_env MCP_POST none]
set out [mcp_env MCP_OUT ""]
if { $design == "" } {
  puts "MCP_DESIGN is not set"
  exit 1
}

switch -- $pre {
  none { set pre_cmd {} }
  timing {
    # E25 arm 6: ideal-wire STA criticality as per-net weight factors
    # (bench/common.tcl mcp_timing_weights), handed to the placer.
    set pre_cmd [subst -nocommands {mcp_timing_weights ${out}.weights}]
    append args " -net_weights_file ${out}.weights"
  }
  gpl_init {
    set pre_cmd {global_placement -skip_nesterov_place \
      -density $::global_place_density \
      -pad_left $::global_place_pad -pad_right $::global_place_pad}
  }
  gpl {
    # Full gpl as the seed: a reference for how much the seed quality
    # matters (the result is then mostly gpl's).
    set pre_cmd {global_placement \
      -density $::global_place_density \
      -pad_left $::global_place_pad -pad_right $::global_place_pad}
  }
  gpl_krow {
    # Full gpl in the few-row baseline mode (bench/gpl_baseline.tcl krow:
    # -disable_pin_density_adjust -overflow 0.2) as the seed, so that a
    # seeded arm on the few-row floorplans starts from the same RePlAce
    # placement the paper compares against (R1, exp/r1_fewrow_oracle.cfg).
    set pre_cmd {global_placement \
      -density $::global_place_density \
      -pad_left $::global_place_pad -pad_right $::global_place_pad \
      -disable_pin_density_adjust -overflow 0.2}
  }
  capo {
    # Capo as the seed (AMERICAN_MAPS_STUDY.md, arm AMc): the Bookshelf
    # bridge of bench/capo.tcl, same environment (MCP_CAPO, MCP_CAPO_ARGS
    # subst'ed after the design is loaded, MCP_CAPO_PAD sites per side,
    # MCP_CAPO_SOFT_BLOCKAGES, MCP_SCRATCH for Capo's working directory).
    source [file join [file dirname [file normalize [info script]]] capo_bridge.tcl]
    set ::capo_exe [mcp_env MCP_CAPO ""]
    set ::capo_args_raw [mcp_env MCP_CAPO_ARGS ""]
    set ::capo_pad_raw [mcp_env MCP_CAPO_PAD 0]
    set ::capo_pad_dbu 0
    set ::capo_soft_blockages [mcp_env MCP_CAPO_SOFT_BLOCKAGES ignore]
    if { $::capo_exe == "" } {
      puts "MCP_PRE=capo needs MCP_CAPO (path to CapoTest1.exe)"
      exit 1
    }
    set scratch [mcp_env MCP_SCRATCH ""]
    set ::capo_dir [expr { $scratch != "" ? [file join $scratch [file tail $out].capo]
                                           : [file normalize $out.capo] }]
    file mkdir $::capo_dir
    set pre_cmd [subst -nocommands {
      set capo_args [subst \$::capo_args_raw]
      set site_width [[lindex [[ord::get_db_block] getRows] 0] getSite]
      set ::capo_pad_dbu [expr { int([subst \$::capo_pad_raw]) * [\$site_width getWidth] }]
      puts "Capo seed arguments: \$capo_args (padding \$::capo_pad_dbu dbu per side)"
      set capo_prefix [file join \$::capo_dir design]
      capo_export \$capo_prefix
      set f [open [file join \$::capo_dir seeds.in] w]
      puts \$f "0\n$seed"
      close \$f
      set t0 [mcp_seconds]
      set here [pwd]
      cd \$::capo_dir
      catch {
        exec \$::capo_exe -f design.aux {*}\$capo_args -savePl design.capo.pl \
          > design.capo.log 2>@1
      } err
      cd \$here
      if { ![file exists \$capo_prefix.capo.pl] } {
        puts "Capo seed failed: \$err"
        exit 1
      }
      capo_import \$capo_prefix.capo.pl
      file copy -force \$capo_prefix.capo.log ${out}.capo.log
      puts "CAPO_SECONDS [format %.2f [expr { [mcp_seconds] - \$t0 }]]"
    }]
  }
  default {
    # pl:<file>: a placement file as the seed (E27 Phase 5: the folded
    # strip placement, exp/fold.py; @DESIGN@ and @SEED@ in the path are
    # substituted), loaded by mcp_pl_import; mincut_placement then reads
    # the seed from the database as with every other MCP_PRE.
    if { [regexp {^pl:(.+)$} $pre -> plfile] } {
      set plfile [string map [list @DESIGN@ $design @SEED@ $seed] $plfile]
      if { ![file exists $plfile] } {
        puts "MCP_PRE=pl: $plfile does not exist"
        exit 1
      }
      set pre_cmd [list mcp_pl_import $plfile]
    } elseif { [regexp {^gpl_ovf([0-9.]+)$} $pre -> tau] } {
      set pre_cmd [subst -nocommands {global_placement -overflow $tau \
        -density \$::global_place_density \
        -pad_left \$::global_place_pad -pad_right \$::global_place_pad}]
    } else {
      puts "unknown MCP_PRE '$pre'"
      exit 1
    }
  }
}

switch -- $post {
  none { set post_cmd {} }
  gpl {
    set post_cmd {global_placement -skip_initial_place \
      -density $::global_place_density \
      -pad_left $::global_place_pad -pad_right $::global_place_pad}
  }
  incremental {
    set post_cmd {global_placement -incremental \
      -density $::global_place_density \
      -pad_left $::global_place_pad -pad_right $::global_place_pad}
  }
  default {
    puts "unknown MCP_POST '$post'"
    exit 1
  }
}

mcp_run_variant $design mincut $seed [subst -nocommands {
  $pre_cmd
  mincut_placement $args -seed $seed -threads $threads \
    -leaf_pad \$::detail_place_pad -report ${out}.mcp.json
  $post_cmd
}]
