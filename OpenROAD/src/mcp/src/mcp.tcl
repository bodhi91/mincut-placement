# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

sta::define_cmd_args "mincut_placement" { \
    [-k k] \
    [-objective cutnet|conn|hpwl|phpwl|rsmt] \
    [-fine_objective cutnet|conn|hpwl|phpwl|rsmt] \
    [-fine_level level] \
    [-net_weight uniform|exphpwl|invdeg] \
    [-net_weights_file file] \
    [-balance_objective bounds|ratio|ncut] \
    [-balance_fractions fractions] \
    [-balance_sweep_tolerance tolerance] \
    [-balance_levels levels] \
    [-cut_direction longer|sparsest] \
    [-engine fm|tritonpart|hmetis|mtkahypar|fastpart] \
    [-top_engine engine] \
    [-top_levels levels] \
    [-density density] \
    [-balance_tolerance tolerance] \
    [-reserve uniform|minlocal|congestion|mixed|acg] \
    [-max_tolerance tolerance] \
    [-terminals none|propagate] \
    [-terminal_band fraction] \
    [-waves waves] \
    [-end_case_cells cells] \
    [-exact_cells cells] \
    [-optimal_cells cells] \
    [-optimal_rows 0|1] \
    [-iron_window cells] \
    [-iron_passes passes] \
    [-end_case_passes passes] \
    [-leaf_positions even|optimal|joint] \
    [-leaf_pad sites] \
    [-max_fanout fanout] \
    [-pad_left sites] \
    [-pad_right sites] \
    [-soft_blockages ignore|block] \
    [-seed_placement] \
    [-assignment none|transport|transport_only] \
    [-displacement_weight weight] \
    [-seed_spread alpha] \
    [-assignment_net_weight weight] \
    [-assignment_rounds rounds] \
    [-seed_levels levels] \
    [-am_mincut_levels levels] \
    [-am_diagnose] \
    [-am_commit mincut|transport] \
    [-oracle none|qp] \
    [-oracle_anchor weight] \
    [-oracle_anchor_util exponent] \
    [-oracle_rounds rounds] \
    [-oracle_min_level level] \
    [-no_oracle_terminals] \
    [-kway_refine_levels levels] \
    [-kway_construct_levels levels] \
    [-kway_slack slack] \
    [-kway_vcycles cycles] \
    [-sweep_levels levels] \
    [-sweep_passes passes] \
    [-sweep_slack slack] \
    [-sweep_multilevel] \
    [-sweep_cold] \
    [-sweep_audit] \
    [-seed_terminals] \
    [-feedback_passes passes] \
    [-feedback_weight weight] \
    [-feedback_assignment none|transport|transport_only] \
    [-max_moves moves] \
    [-refiner_iters passes] \
    [-engine_runs runs] \
    [-cut_degrade fraction] \
    [-no_seed_leaf] \
    [-threads threads] \
    [-seed seed] \
    [-report file] \
    [-regions file] \
    [-hypergraph file] \
    [-hmetis_path path] \
    [-mtkahypar_path path] \
    [-fastpart_path path] \
    [-fastpart_time seconds] \
    [-fastpart_decay factor] \
    [-fastpart_min_time seconds] \
    [-fastpart_min_cells cells] \
    [-fastpart_stall runs] \
    [-scratch_dir dir]}

proc mincut_placement { args } {
  sta::parse_key_args "mincut_placement" args \
    keys {-k -objective -fine_objective -fine_level \
          -net_weight -net_weights_file -balance_objective \
          -balance_fractions -balance_sweep_tolerance -balance_levels \
          -cut_direction -engine -top_engine \
          -top_levels -density -balance_tolerance -reserve -max_tolerance \
          -terminals -terminal_band -waves -end_case_cells -exact_cells \
          -optimal_cells -optimal_rows -iron_window -iron_passes -end_case_passes \
          -leaf_positions -leaf_pad -max_fanout \
          -pad_left -pad_right -soft_blockages \
          -assignment -displacement_weight -seed_spread \
          -assignment_net_weight -assignment_rounds -seed_levels \
          -am_mincut_levels -am_commit \
          -oracle -oracle_anchor -oracle_anchor_util -oracle_rounds \
          -oracle_min_level -kway_refine_levels -kway_construct_levels \
          -kway_slack -kway_vcycles -sweep_levels -sweep_passes -sweep_slack \
          -feedback_passes -feedback_weight -feedback_assignment \
          -max_moves -refiner_iters -engine_runs -cut_degrade \
          -threads -seed -report -regions \
          -hypergraph -hmetis_path -mtkahypar_path -fastpart_path -fastpart_time \
          -fastpart_decay -fastpart_min_time -fastpart_min_cells \
          -fastpart_stall -scratch_dir} \
    flags {-seed_placement -seed_terminals -no_oracle_terminals -am_diagnose \
           -sweep_multilevel -sweep_cold -sweep_audit -no_seed_leaf}

  sta::check_argc_eq0 "mincut_placement" $args

  mcp::reset_options_cmd

  if { [info exists keys(-k)] } {
    set k $keys(-k)
    if { $k != 0 && $k != 2 && $k != 4 } {
      utl::error MCP 100 "-k must be 0 (auto), 2 or 4."
    }
    mcp::set_k_cmd $k
  }
  if { [info exists keys(-objective)] } {
    mcp::set_objective_cmd $keys(-objective)
  }
  if { [info exists keys(-fine_objective)] } {
    set level 1
    if { [info exists keys(-fine_level)] } {
      set level $keys(-fine_level)
      sta::check_cardinal "-fine_level" $level
    }
    mcp::set_fine_objective_cmd $keys(-fine_objective) $level
  } elseif { [info exists keys(-fine_level)] } {
    utl::error MCP 104 "-fine_level requires -fine_objective."
  }
  if { [info exists keys(-net_weight)] } {
    mcp::set_net_weight_cmd $keys(-net_weight)
  }
  if { [info exists keys(-net_weights_file)] } {
    mcp::set_net_weights_file_cmd $keys(-net_weights_file)
  }
  if { [info exists keys(-balance_objective)] } {
    mcp::set_balance_objective_cmd $keys(-balance_objective)
  }
  if { [info exists keys(-balance_fractions)] } {
    mcp::clear_balance_fractions_cmd
    foreach f $keys(-balance_fractions) {
      if { ![string is double -strict $f] || $f <= 0.0 || $f >= 1.0 } {
        utl::error MCP 108 "-balance_fractions must be numbers in (0, 1)."
      }
      mcp::add_balance_fraction_cmd $f
    }
  }
  if { [info exists keys(-balance_sweep_tolerance)] } {
    sta::check_positive_float "-balance_sweep_tolerance" \
      $keys(-balance_sweep_tolerance)
    mcp::set_balance_sweep_tolerance_cmd $keys(-balance_sweep_tolerance)
  }
  if { [info exists keys(-balance_levels)] } {
    sta::check_cardinal "-balance_levels" $keys(-balance_levels)
    mcp::set_balance_levels_cmd $keys(-balance_levels)
  }
  if { [info exists keys(-cut_direction)] } {
    mcp::set_cut_direction_cmd $keys(-cut_direction)
  }
  if { [info exists keys(-engine)] } {
    mcp::set_engine_cmd $keys(-engine)
  }
  if { [info exists keys(-top_engine)] } {
    set levels 1
    if { [info exists keys(-top_levels)] } {
      set levels $keys(-top_levels)
      sta::check_positive_integer "-top_levels" $levels
    }
    mcp::set_top_engine_cmd $keys(-top_engine) $levels
  } elseif { [info exists keys(-top_levels)] } {
    utl::error MCP 101 "-top_levels requires -top_engine."
  }
  if { [info exists keys(-density)] } {
    set density $keys(-density)
    sta::check_positive_float "-density" $density
    if { $density > 1.0 } {
      utl::error MCP 102 "-density must be in (0, 1]."
    }
    mcp::set_density_cmd $density
  }
  if { [info exists keys(-balance_tolerance)] } {
    set tol $keys(-balance_tolerance)
    sta::check_positive_float "-balance_tolerance" $tol
    mcp::set_balance_tolerance_cmd $tol
  }
  if { [info exists keys(-reserve)] } {
    mcp::set_reserve_cmd $keys(-reserve)
    if { $keys(-reserve) != "uniform" && ![info exists keys(-density)] } {
      utl::warn MCP 105 "-reserve $keys(-reserve) moves only the whitespace\
        above -density; without -density the whitespace stays uniform."
    }
  }
  if { [info exists keys(-max_tolerance)] } {
    set tol $keys(-max_tolerance)
    sta::check_positive_float "-max_tolerance" $tol
    mcp::set_max_tolerance_cmd $tol
  }
  if { [info exists keys(-terminals)] } {
    mcp::set_terminals_cmd $keys(-terminals)
  }
  if { [info exists keys(-terminal_band)] } {
    sta::check_positive_float "-terminal_band" $keys(-terminal_band)
    mcp::set_terminal_band_cmd $keys(-terminal_band)
  }
  if { [info exists keys(-waves)] } {
    sta::check_positive_integer "-waves" $keys(-waves)
    mcp::set_waves_cmd $keys(-waves)
  }
  if { [info exists keys(-end_case_cells)] } {
    sta::check_positive_integer "-end_case_cells" $keys(-end_case_cells)
    mcp::set_end_case_cells_cmd $keys(-end_case_cells)
  }
  if { [info exists keys(-exact_cells)] } {
    sta::check_positive_integer "-exact_cells" $keys(-exact_cells)
    mcp::set_exact_cells_cmd $keys(-exact_cells)
  }
  if { [info exists keys(-optimal_cells)] } {
    sta::check_positive_integer "-optimal_cells" $keys(-optimal_cells)
    mcp::set_optimal_cells_cmd $keys(-optimal_cells)
  }
  if { [info exists keys(-optimal_rows)] } {
    sta::check_positive_integer "-optimal_rows" $keys(-optimal_rows)
    mcp::set_optimal_rows_cmd [expr { $keys(-optimal_rows) != 0 }]
  }
  set iron_window 7
  set iron_passes 2
  if { [info exists keys(-iron_window)] } {
    sta::check_positive_integer "-iron_window" $keys(-iron_window)
    set iron_window $keys(-iron_window)
  }
  if { [info exists keys(-iron_passes)] } {
    sta::check_positive_integer "-iron_passes" $keys(-iron_passes)
    set iron_passes $keys(-iron_passes)
  }
  mcp::set_ironing_cmd $iron_window $iron_passes
  if { [info exists keys(-end_case_passes)] } {
    sta::check_positive_integer "-end_case_passes" $keys(-end_case_passes)
    mcp::set_end_case_passes_cmd $keys(-end_case_passes)
  }
  if { [info exists keys(-leaf_positions)] } {
    mcp::set_leaf_positions_cmd $keys(-leaf_positions)
  }
  if { [info exists keys(-leaf_pad)] } {
    sta::check_integer "-leaf_pad" $keys(-leaf_pad)
    mcp::set_leaf_pad_cmd $keys(-leaf_pad)
  }
  if { [info exists keys(-max_fanout)] } {
    sta::check_positive_integer "-max_fanout" $keys(-max_fanout)
    mcp::set_max_fanout_cmd $keys(-max_fanout)
  }
  if { [info exists keys(-pad_left)] || [info exists keys(-pad_right)] } {
    set pad_left 0
    set pad_right 0
    if { [info exists keys(-pad_left)] } {
      set pad_left $keys(-pad_left)
      sta::check_positive_integer "-pad_left" $pad_left
    }
    if { [info exists keys(-pad_right)] } {
      set pad_right $keys(-pad_right)
      sta::check_positive_integer "-pad_right" $pad_right
    }
    mcp::set_padding_cmd $pad_left $pad_right
  }
  if { [info exists keys(-soft_blockages)] } {
    switch -- $keys(-soft_blockages) {
      ignore { mcp::set_soft_blockages_cmd 0 }
      block { mcp::set_soft_blockages_cmd 1 }
      default {
        utl::error MCP 49 "-soft_blockages must be ignore or block"
      }
    }
  }
  if { [info exists flags(-seed_placement)] } {
    mcp::set_seed_placement_cmd 1
  } elseif { [info exists keys(-assignment)] \
               || [info exists keys(-displacement_weight)] \
               || [info exists keys(-seed_spread)] \
               || [info exists keys(-assignment_net_weight)] \
               || [info exists keys(-assignment_rounds)] \
               || [info exists keys(-seed_levels)] \
               || [info exists flags(-seed_terminals)] } {
    utl::warn MCP 106 "-assignment, -displacement_weight, -seed_spread,\
      -assignment_net_weight, -assignment_rounds, -seed_levels and\
      -seed_terminals have no effect without -seed_placement."
  }
  if { [info exists flags(-seed_terminals)] } {
    mcp::set_seed_terminals_cmd 1
  }
  if { [info exists keys(-assignment)] } {
    mcp::set_assignment_cmd $keys(-assignment)
  }
  if { [info exists keys(-displacement_weight)] } {
    set w $keys(-displacement_weight)
    sta::check_positive_float "-displacement_weight" $w
    mcp::set_displacement_weight_cmd $w
  }
  if { [info exists keys(-seed_spread)] } {
    set a $keys(-seed_spread)
    sta::check_positive_float "-seed_spread" $a
    if { $a > 1.0 } {
      utl::error MCP 107 "-seed_spread must be in \[0, 1\]."
    }
    mcp::set_seed_spread_cmd $a
  }
  if { [info exists keys(-assignment_net_weight)] } {
    set w $keys(-assignment_net_weight)
    sta::check_positive_float "-assignment_net_weight" $w
    mcp::set_assignment_net_weight_cmd $w
  }
  if { [info exists keys(-assignment_rounds)] } {
    sta::check_positive_integer "-assignment_rounds" $keys(-assignment_rounds)
    mcp::set_assignment_rounds_cmd $keys(-assignment_rounds)
  }
  if { [info exists keys(-am_mincut_levels)] } {
    mcp::clear_am_mincut_levels_cmd
    foreach level $keys(-am_mincut_levels) {
      sta::check_positive_integer "-am_mincut_levels" $level
      mcp::add_am_mincut_level_cmd $level
    }
  }
  mcp::set_am_diagnose_cmd [info exists flags(-am_diagnose)]
  if { [info exists keys(-am_commit)] } {
    switch -- $keys(-am_commit) {
      mincut { mcp::set_am_commit_mincut_cmd 1 }
      transport { mcp::set_am_commit_mincut_cmd 0 }
      default { utl::error MCP 118 "-am_commit must be mincut or transport" }
    }
  }
  if { [info exists keys(-oracle)] } {
    mcp::set_oracle_cmd $keys(-oracle)
  }
  if { [info exists flags(-no_oracle_terminals)] } {
    mcp::set_oracle_terminals_cmd 0
  }
  if { [info exists keys(-oracle_anchor)] } {
    sta::check_positive_float "-oracle_anchor" $keys(-oracle_anchor)
    mcp::set_oracle_anchor_cmd $keys(-oracle_anchor)
  }
  if { [info exists keys(-oracle_anchor_util)] } {
    sta::check_positive_float "-oracle_anchor_util" $keys(-oracle_anchor_util)
    mcp::set_oracle_anchor_util_cmd $keys(-oracle_anchor_util)
  }
  if { [info exists keys(-oracle_rounds)] } {
    sta::check_positive_integer "-oracle_rounds" $keys(-oracle_rounds)
    mcp::set_oracle_rounds_cmd $keys(-oracle_rounds)
  }
  if { [info exists keys(-oracle_min_level)] } {
    sta::check_cardinal "-oracle_min_level" $keys(-oracle_min_level)
    mcp::set_oracle_min_level_cmd $keys(-oracle_min_level)
  }
  if { [info exists keys(-seed_levels)] } {
    sta::check_integer "-seed_levels" $keys(-seed_levels)
    mcp::set_seed_levels_cmd $keys(-seed_levels)
  }
  if { [info exists keys(-kway_refine_levels)] } {
    mcp::clear_kway_refine_levels_cmd
    foreach level $keys(-kway_refine_levels) {
      sta::check_cardinal "-kway_refine_levels" $level
      mcp::add_kway_refine_level_cmd $level
    }
  }
  if { [info exists keys(-kway_construct_levels)] } {
    mcp::clear_kway_construct_levels_cmd
    foreach level $keys(-kway_construct_levels) {
      sta::check_cardinal "-kway_construct_levels" $level
      mcp::add_kway_construct_level_cmd $level
    }
  }
  if { [info exists keys(-kway_slack)] } {
    set s $keys(-kway_slack)
    sta::check_positive_float "-kway_slack" $s
    if { $s < 1.0 } {
      utl::error MCP 59 "-kway_slack must be at least 1."
    }
    mcp::set_kway_slack_cmd $s
  }
  if { [info exists keys(-kway_vcycles)] } {
    sta::check_positive_integer "-kway_vcycles" $keys(-kway_vcycles)
    mcp::set_kway_vcycles_cmd $keys(-kway_vcycles)
  }
  if { [info exists keys(-sweep_levels)] } {
    mcp::clear_sweep_levels_cmd
    foreach level $keys(-sweep_levels) {
      sta::check_cardinal "-sweep_levels" $level
      mcp::add_sweep_level_cmd $level
    }
  }
  if { [info exists keys(-sweep_passes)] } {
    sta::check_positive_integer "-sweep_passes" $keys(-sweep_passes)
    mcp::set_sweep_passes_cmd $keys(-sweep_passes)
  }
  if { [info exists keys(-sweep_slack)] } {
    set s $keys(-sweep_slack)
    sta::check_positive_float "-sweep_slack" $s
    if { $s < 1.0 } {
      utl::error MCP 62 "-sweep_slack must be at least 1."
    }
    mcp::set_sweep_slack_cmd $s
  }
  mcp::set_sweep_multilevel_cmd [info exists flags(-sweep_multilevel)]
  mcp::set_sweep_warm_cmd [expr { ![info exists flags(-sweep_cold)] }]
  mcp::set_sweep_audit_cmd [info exists flags(-sweep_audit)]
  if { [info exists keys(-feedback_passes)] } {
    sta::check_positive_integer "-feedback_passes" $keys(-feedback_passes)
    mcp::set_feedback_passes_cmd $keys(-feedback_passes)
  }
  if { [info exists keys(-feedback_weight)] } {
    set w $keys(-feedback_weight)
    sta::check_positive_float "-feedback_weight" $w
    mcp::set_feedback_weight_cmd $w
  }
  if { [info exists keys(-feedback_assignment)] } {
    mcp::set_feedback_assignment_cmd $keys(-feedback_assignment)
  }
  if { [info exists keys(-max_moves)] } {
    sta::check_positive_integer "-max_moves" $keys(-max_moves)
    mcp::set_max_moves_cmd $keys(-max_moves)
  }
  if { [info exists keys(-refiner_iters)] } {
    sta::check_positive_integer "-refiner_iters" $keys(-refiner_iters)
    mcp::set_refiner_iters_cmd $keys(-refiner_iters)
  }
  if { [info exists keys(-engine_runs)] } {
    sta::check_positive_integer "-engine_runs" $keys(-engine_runs)
    mcp::set_engine_runs_cmd $keys(-engine_runs)
  }
  if { [info exists keys(-cut_degrade)] } {
    sta::check_positive_float "-cut_degrade" $keys(-cut_degrade)
    mcp::set_cut_degrade_cmd $keys(-cut_degrade)
  }
  if { [info exists flags(-no_seed_leaf)] } {
    mcp::set_seed_leaf_cmd 0
  }
  if { [info exists keys(-threads)] } {
    sta::check_positive_integer "-threads" $keys(-threads)
    mcp::set_threads_cmd $keys(-threads)
  }
  if { [info exists keys(-seed)] } {
    sta::check_integer "-seed" $keys(-seed)
    mcp::set_seed_cmd $keys(-seed)
  }
  if { [info exists keys(-report)] } {
    mcp::set_report_file_cmd $keys(-report)
  }
  if { [info exists keys(-regions)] } {
    mcp::set_regions_file_cmd $keys(-regions)
  }
  if { [info exists keys(-hypergraph)] } {
    mcp::set_hypergraph_file_cmd $keys(-hypergraph)
  }
  if { [info exists keys(-hmetis_path)] } {
    mcp::set_engine_path_cmd hmetis $keys(-hmetis_path)
  }
  if { [info exists keys(-mtkahypar_path)] } {
    mcp::set_engine_path_cmd mtkahypar $keys(-mtkahypar_path)
  }
  if { [info exists keys(-fastpart_path)] } {
    mcp::set_engine_path_cmd fastpart $keys(-fastpart_path)
  }
  if { [info exists keys(-fastpart_time)] } {
    sta::check_positive_float "-fastpart_time" $keys(-fastpart_time)
    mcp::set_fastpart_time_cmd $keys(-fastpart_time)
  }
  if { [info exists keys(-fastpart_decay)] } {
    sta::check_positive_float "-fastpart_decay" $keys(-fastpart_decay)
    mcp::set_fastpart_decay_cmd $keys(-fastpart_decay)
  }
  if { [info exists keys(-fastpart_min_time)] } {
    sta::check_positive_float "-fastpart_min_time" $keys(-fastpart_min_time)
    mcp::set_fastpart_min_time_cmd $keys(-fastpart_min_time)
  }
  if { [info exists keys(-fastpart_min_cells)] } {
    sta::check_positive_integer "-fastpart_min_cells" $keys(-fastpart_min_cells)
    mcp::set_fastpart_min_cells_cmd $keys(-fastpart_min_cells)
  }
  if { [info exists keys(-fastpart_stall)] } {
    sta::check_positive_integer "-fastpart_stall" $keys(-fastpart_stall)
    mcp::set_fastpart_stall_cmd $keys(-fastpart_stall)
  }
  if { [info exists keys(-scratch_dir)] } {
    mcp::set_scratch_dir_cmd $keys(-scratch_dir)
  }

  if { [ord::get_db_block] == "NULL" } {
    utl::error MCP 103 "No design block found."
  }

  mcp::mincut_placement_cmd
}

namespace eval mcp {
# Debug graphics (regions after every level) when the GUI is active.
proc set_debug { args } {
  sta::parse_key_args "mcp::set_debug" args keys {} flags {}
  mcp::set_debug_cmd
}
}
