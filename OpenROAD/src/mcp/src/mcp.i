// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

// Tcl interface.  Options are collected in a process-wide Options
// object by simple setters (SWIG handles scalars and strings well);
// mincut_placement_cmd runs the placer with them.

%{
#include <memory>
#include <string>

#include "graphics.h"
#include "mcp/MinCutPlacer.h"
#include "ord/OpenRoad.hh"

namespace mcp {
static Options& options()
{
  static Options opts;
  return opts;
}
}  // namespace mcp
%}

%include "../../Exception.i"

%inline %{

namespace mcp {

void reset_options_cmd()
{
  options() = Options();
}

void set_k_cmd(int k) { options().k = k; }
void set_objective_cmd(const char* name)
{
  options().objective = objectiveFromString(name);
}
void set_fine_objective_cmd(const char* name, int level)
{
  options().fine_objective = objectiveFromString(name);
  options().fine_level = level;
}
void set_net_weight_cmd(const char* name)
{
  options().net_weight = netWeightRuleFromString(name);
}
void set_net_weights_file_cmd(const char* file)
{
  options().net_weights_file = file;
}
void set_balance_objective_cmd(const char* name)
{
  options().balance_objective = balanceObjectiveFromString(name);
}
void clear_balance_fractions_cmd() { options().balance_fractions.clear(); }
void add_balance_fraction_cmd(double f)
{
  options().balance_fractions.push_back(f);
}
void set_balance_sweep_tolerance_cmd(double tol)
{
  options().balance_sweep_tolerance = tol;
}
void set_balance_levels_cmd(int levels) { options().balance_levels = levels; }
void set_cut_direction_cmd(const char* name)
{
  options().cut_direction = cutDirectionFromString(name);
}
void set_engine_cmd(const char* name)
{
  options().engine = engineKindFromString(name);
}
void set_top_engine_cmd(const char* name, int levels)
{
  options().top_engine = engineKindFromString(name);
  options().top_levels = levels;
}
void set_density_cmd(double density) { options().density = density; }
void set_balance_tolerance_cmd(double tol)
{
  options().balance_tolerance = tol;
}
void set_reserve_cmd(const char* name)
{
  options().reserve = reservePolicyFromString(name);
}
void set_max_tolerance_cmd(double tol) { options().max_tolerance = tol; }
void set_terminals_cmd(const char* name)
{
  options().terminals = terminalModeFromString(name);
}
void set_terminal_band_cmd(double f) { options().terminal_band = f; }
void set_soft_blockages_cmd(bool block)
{
  options().block_soft_blockages = block;
}
void set_waves_cmd(int n) { options().waves = n; }
void set_end_case_cells_cmd(int n) { options().end_case_cells = n; }
void set_exact_cells_cmd(int n) { options().exact_cells = n; }
void set_optimal_cells_cmd(int n) { options().optimal_cells = n; }
void set_optimal_rows_cmd(bool on) { options().optimal_rows = on; }
void set_ironing_cmd(int window, int passes)
{
  options().iron_window = window;
  options().iron_passes = passes;
}
void set_end_case_passes_cmd(int n) { options().end_case_passes = n; }
void set_max_fanout_cmd(int n) { options().max_fanout = n; }
void set_padding_cmd(int left, int right)
{
  options().pad_left = left;
  options().pad_right = right;
}
void set_seed_placement_cmd(bool on) { options().seed_placement = on; }
void set_assignment_cmd(const char* name)
{
  options().assignment = assignmentModeFromString(name);
}
void set_displacement_weight_cmd(double w)
{
  options().displacement_weight = w;
}
void set_seed_spread_cmd(double a) { options().seed_spread = a; }
void set_assignment_net_weight_cmd(double w)
{
  options().assignment_net_weight = w;
}
void set_assignment_rounds_cmd(int n) { options().assignment_rounds = n; }
void set_oracle_cmd(const char* name)
{
  options().oracle = oracleKindFromString(name);
}
void set_oracle_anchor_cmd(double w) { options().oracle_anchor = w; }
void set_oracle_anchor_util_cmd(double g) { options().oracle_anchor_util = g; }
void set_oracle_rounds_cmd(int n) { options().oracle_rounds = n; }
void set_oracle_min_level_cmd(int n) { options().oracle_min_level = n; }
void set_oracle_terminals_cmd(bool on) { options().oracle_terminals = on; }
void clear_kway_refine_levels_cmd() { options().kway_refine_levels.clear(); }
void add_kway_refine_level_cmd(int level)
{
  options().kway_refine_levels.push_back(level);
}
void clear_kway_construct_levels_cmd()
{
  options().kway_construct_levels.clear();
}
void add_kway_construct_level_cmd(int level)
{
  options().kway_construct_levels.push_back(level);
}
void set_kway_slack_cmd(double s) { options().kway_slack = s; }
void set_kway_vcycles_cmd(int n) { options().kway_vcycles = n; }
void clear_sweep_levels_cmd() { options().sweep_levels.clear(); }
void add_sweep_level_cmd(int level) { options().sweep_levels.push_back(level); }
void set_sweep_passes_cmd(int n) { options().sweep_passes = n; }
void set_sweep_slack_cmd(double s) { options().sweep_slack = s; }
void set_sweep_multilevel_cmd(bool on) { options().sweep_multilevel = on; }
void set_sweep_warm_cmd(bool on) { options().sweep_warm = on; }
void set_sweep_audit_cmd(bool on) { options().sweep_audit = on; }
void set_seed_levels_cmd(int n) { options().seed_levels = n; }
void clear_am_mincut_levels_cmd() { options().am_mincut_levels.clear(); }
void add_am_mincut_level_cmd(int level)
{
  options().am_mincut_levels.push_back(level);
}
void set_am_diagnose_cmd(bool on) { options().am_diagnose = on; }
void set_am_commit_mincut_cmd(bool on) { options().am_commit_mincut = on; }
void set_seed_terminals_cmd(bool on) { options().seed_terminals = on; }
void set_feedback_passes_cmd(int n) { options().feedback_passes = n; }
void set_feedback_weight_cmd(double w) { options().feedback_weight = w; }
void set_feedback_assignment_cmd(const char* name)
{
  options().feedback_assignment = assignmentModeFromString(name);
}
void set_leaf_pad_cmd(int sites) { options().leaf_pad = sites; }
void set_leaf_positions_cmd(const char* name)
{
  options().leaf_positions = positionRuleFromString(name);
}
void set_max_moves_cmd(int n) { options().max_moves = n; }
void set_refiner_iters_cmd(int n) { options().refiner_iters = n; }
void set_engine_runs_cmd(int n) { options().engine_runs = n; }
void set_cut_degrade_cmd(double f) { options().cut_degrade = f; }
void set_seed_leaf_cmd(bool on) { options().seed_leaf = on; }
void set_threads_cmd(int n) { options().threads = n; }
void set_seed_cmd(int seed) { options().seed = static_cast<unsigned>(seed); }
void set_report_file_cmd(const char* path) { options().report_file = path; }
void set_regions_file_cmd(const char* path) { options().regions_file = path; }
void set_hypergraph_file_cmd(const char* path) { options().hypergraph_file = path; }
void set_engine_path_cmd(const char* engine, const char* path)
{
  switch (engineKindFromString(engine)) {
    case EngineKind::kHmetis:
      options().hmetis_path = path;
      break;
    case EngineKind::kMtKaHyPar:
      options().mtkahypar_path = path;
      break;
    case EngineKind::kFastPart:
      options().fastpart_path = path;
      break;
    default:
      break;
  }
}
void set_fastpart_time_cmd(double seconds) { options().fastpart_time = seconds; }
void set_fastpart_stall_cmd(int runs) { options().fastpart_stall = runs; }
void set_fastpart_decay_cmd(double f) { options().fastpart_decay = f; }
void set_fastpart_min_time_cmd(double s) { options().fastpart_min_time = s; }
void set_fastpart_min_cells_cmd(int n) { options().fastpart_min_cells = n; }
void set_scratch_dir_cmd(const char* path) { options().scratch_dir = path; }

void mincut_placement_cmd()
{
  MinCutPlacer* placer = ord::OpenRoad::openRoad()->getMinCutPlacer();
  placer->place(options());
}

void set_debug_cmd()
{
  MinCutPlacer* placer = ord::OpenRoad::openRoad()->getMinCutPlacer();
  if (Graphics::guiActive()) {
    std::unique_ptr<Observer> graphics = std::make_unique<Graphics>();
    placer->setDebug(graphics);
  }
}

}  // namespace mcp

%}  // inline
