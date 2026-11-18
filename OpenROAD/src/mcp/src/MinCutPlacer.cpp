// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "mcp/MinCutPlacer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Decomposer.h"
#include "Engine.h"
#include "NetlistModel.h"
#include "PlacementArea.h"
#include "Region.h"
#include "RegionTree.h"
#include "observer.h"
#include "odb/db.h"
#include "utl/Logger.h"

namespace mcp {

namespace {

// The per-pass HPWL of a boundary sweep (dbu) as a JSON list in um.
std::string sweepCurve(const std::vector<double>& curve, double dbu)
{
  std::ostringstream out;
  for (size_t i = 0; i < curve.size(); ++i) {
    out << (i > 0 ? ", " : "") << curve[i] / dbu;
  }
  return out.str();
}

}  // namespace

MinCutPlacer::MinCutPlacer(odb::dbDatabase* db,
                           par::PartitionMgr* partition_mgr,
                           utl::Logger* logger)
    : db_(db), partition_mgr_(partition_mgr), logger_(logger)
{
}

MinCutPlacer::~MinCutPlacer() = default;

odb::dbBlock* MinCutPlacer::getBlock()
{
  odb::dbChip* chip = db_->getChip();
  if (!chip) {
    logger_->error(utl::MCP, 1, "No chip exists.");
  }
  odb::dbBlock* block = chip->getBlock();
  if (!block) {
    logger_->error(utl::MCP, 2, "No block exists.");
  }
  return block;
}

void MinCutPlacer::place(const Options& options)
{
  odb::dbBlock* block = getBlock();

  if (block->getRows().empty()) {
    logger_->error(utl::MCP, 3, "No rows found; run initialize_floorplan.");
  }

  // Padding in dbu: sites of the narrowest row site.
  int site_width = 0;
  for (odb::dbRow* row : block->getRows()) {
    const int spacing = row->getSpacing();
    if (spacing > 0 && (site_width == 0 || spacing < site_width)) {
      site_width = spacing;
    }
  }
  const int padding
      = (std::max(0, options.pad_left) + std::max(0, options.pad_right))
        * site_width;

  // Placement feedback: passes beyond the first are seeded by the best
  // result so far, with every not-yet-placed cell assumed at its
  // previous position instead of its region center.  The database holds
  // the best pass at the end.
  const int passes = std::max(1, options.feedback_passes);
  Options pass_options = options;
  std::unique_ptr<PassState> best;
  std::vector<double> pass_hpwl;
  int best_pass = 0;
  for (int pass = 0; pass < passes; ++pass) {
    if (pass > 0) {
      pass_options.seed_placement = true;
      pass_options.assignment = options.feedback_assignment;
      pass_options.displacement_weight = options.feedback_weight;
      pass_options.seed_terminals = true;
      pass_options.seed_levels = -1;
      logger_->info(utl::MCP,
                    39,
                    "Feedback pass {} of {}: seeded by the result of pass {} "
                    "(displacement weight {:.3g})",
                    pass + 1,
                    passes,
                    best_pass + 1,
                    options.feedback_weight);
    }
    std::unique_ptr<PassState> state
        = runPass(block, pass_options, padding, pass + 1);
    pass_hpwl.push_back(state->hpwl_um);
    if (!best || state->hpwl_um < best->hpwl_um) {
      best = std::move(state);
      best_pass = pass;
      if (passes > 1) {
        savePlacement(*best);
      }
      continue;
    }
    logger_->info(utl::MCP,
                  40,
                  "Feedback pass {} did not improve (HPWL {:.0f} vs {:.0f} "
                  "um); keeping the result of pass {}",
                  pass + 1,
                  state->hpwl_um,
                  best->hpwl_um,
                  best_pass + 1);
    restorePlacement(*best);
    break;
  }
  if (passes > 1) {
    logger_->info(utl::MCP,
                  41,
                  "Placement feedback: {} passes run, pass {} kept, HPWL "
                  "{:.0f} um (pass 1: {:.0f} um)",
                  pass_hpwl.size(),
                  best_pass + 1,
                  best->hpwl_um,
                  pass_hpwl.front());
  }

  if (!options.report_file.empty()) {
    writeReport(options,
                *best->report,
                best->hpwl_um,
                best->netlist->stats(),
                pass_hpwl,
                best_pass + 1);
  }
  if (!options.regions_file.empty()) {
    writeRegions(options, *best->tree, *best->netlist);
  }
  if (!options.hypergraph_file.empty()) {
    writeHypergraph(
        options, *best->netlist, *best->area, block->getDbUnitsPerMicron());
  }
}

void MinCutPlacer::savePlacement(PassState& state) const
{
  state.saved.clear();
  for (int v : state.netlist->movableVertices()) {
    odb::dbInst* inst = state.netlist->vertex(v).inst;
    int x = 0;
    int y = 0;
    inst->getLocation(x, y);
    state.saved.push_back({inst, odb::Point(x, y), inst->getOrient()});
  }
}

void MinCutPlacer::restorePlacement(const PassState& state) const
{
  for (const SavedInst& s : state.saved) {
    s.inst->setOrient(s.orient);
    s.inst->setLocation(s.loc.x(), s.loc.y());
    s.inst->setPlacementStatus(odb::dbPlacementStatus::PLACED);
  }
}

// Net weights of the objective study (E24): the pin-count rule, then
// the per-net factors of the weights file ("name weight" per line, '#'
// comments).  The weights enter every objective (the cut, the vectors)
// and every engine (the external engines receive them scaled to
// integers, see ExternalEngine::writeHypergraph).
void MinCutPlacer::applyNetWeights(const Options& options,
                                   NetlistModel& netlist,
                                   bool verbose) const
{
  const bool rule = options.net_weight != NetWeightRule::kUniform;
  const bool file = !options.net_weights_file.empty();
  if (!rule && !file) {
    return;
  }
  std::unordered_map<std::string, double> factors;
  if (file) {
    std::ifstream in(options.net_weights_file);
    if (!in) {
      logger_->error(utl::MCP,
                     51,
                     "Cannot read the net weights file {}",
                     options.net_weights_file);
    }
    std::string line;
    while (std::getline(in, line)) {
      const auto hash = line.find('#');
      if (hash != std::string::npos) {
        line.erase(hash);
      }
      std::istringstream fields(line);
      std::string name;
      double w = 0.0;
      if (fields >> name >> w && w > 0.0) {
        factors[name] = w;
      }
    }
  }
  int weighted = 0;
  double w_min = 0.0, w_max = 0.0, w_sum = 0.0;
  for (int e = 0; e < netlist.numNets(); ++e) {
    const int pins = static_cast<int>(netlist.netVertices(e).size());
    double w = netWeightFor(options.net_weight, pins);
    if (file) {
      const auto it = factors.find(netlist.net(e)->getName());
      if (it != factors.end()) {
        w *= it->second;
        ++weighted;
      }
    }
    netlist.setNetWeight(e, static_cast<float>(w));
    w_min = e == 0 ? w : std::min(w_min, w);
    w_max = e == 0 ? w : std::max(w_max, w);
    w_sum += w;
  }
  if (verbose && netlist.numNets() > 0) {
    logger_->info(utl::MCP,
                  52,
                  "Net weights: rule {}, {} nets from {}; min {:.3f} mean "
                  "{:.3f} max {:.3f}",
                  netWeightRuleToString(options.net_weight),
                  weighted,
                  file ? options.net_weights_file : "no file",
                  w_min,
                  w_sum / netlist.numNets(),
                  w_max);
  }
}

std::unique_ptr<MinCutPlacer::PassState> MinCutPlacer::runPass(
    odb::dbBlock* block,
    const Options& options,
    int padding,
    int pass)
{
  auto state = std::make_unique<PassState>();
  state->netlist = std::make_unique<NetlistModel>(
      block, options.max_fanout, padding, logger_);
  NetlistModel& netlist = *state->netlist;
  const NetlistStats& st = netlist.stats();
  applyNetWeights(options, netlist, pass == 1);

  if (st.unplaced_non_core > 0) {
    logger_->error(utl::MCP,
                   4,
                   "{} non-core instances (macros, pads) are not placed; "
                   "place them before min-cut placement.",
                   st.unplaced_non_core);
  }
  if (st.movable == 0) {
    logger_->error(utl::MCP, 5, "No movable instances.");
  }

  const double dbu = block->getDbUnitsPerMicron();
  const double core_area_um2 = block->getCoreArea().area() / (dbu * dbu);
  const double movable_area_um2 = st.movable_area / (dbu * dbu);

  // The netlist and core statistics do not change between feedback
  // passes; they are logged once.
  const bool verbose = pass == 1;
  if (verbose) {
    logger_->info(utl::MCP,
                  10,
                  "Netlist: {} movable instances, {} fixed instances, {} I/O "
                  "pins, {} nets",
                  st.movable,
                  st.fixed_insts,
                  st.io_pins,
                  st.nets_kept);
    logger_->info(utl::MCP,
                  11,
                  "Skipped nets: {} special/supply, {} degree<2, {} fanout>{}, "
                  "{} without movable pins",
                  st.nets_skipped_special,
                  st.nets_skipped_degree,
                  st.nets_skipped_fanout,
                  options.max_fanout,
                  st.nets_skipped_no_movable);
    logger_->info(
        utl::MCP,
        12,
        "Movable area {:.1f} um^2{}, core area {:.1f} um^2, "
        "utilization {:.1f}%",
        movable_area_um2,
        padding > 0
            ? fmt::format(
                  " (padding {}+{} sites)", options.pad_left, options.pad_right)
            : "",
        core_area_um2,
        core_area_um2 > 0 ? 100.0 * movable_area_um2 / core_area_um2 : 0.0);
  }

  if (options.seed_placement) {
    int unplaced = 0;
    for (int v : netlist.movableVertices()) {
      if (!netlist.vertex(v).placed) {
        ++unplaced;
      }
    }
    if (unplaced > 0) {
      logger_->error(utl::MCP,
                     26,
                     "-seed_placement: {} of {} movable instances have no "
                     "location; place them first (e.g. global_placement "
                     "-skip_nesterov_place).",
                     unplaced,
                     st.movable);
    }
    logger_->info(utl::MCP,
                  27,
                  "Seed placement: assignment {} (net weight {:.3g}, {} "
                  "rounds), displacement weight {:.3g}, seed spread {:.3g}, "
                  "terminals at {}",
                  assignmentModeToString(options.assignment),
                  options.assignment_net_weight,
                  options.assignment_rounds,
                  options.displacement_weight,
                  options.seed_spread,
                  options.seed_terminals ? "seed" : "region centers");
  }

  // Placement area: rows minus fixed instances and blockages.  Soft
  // blockages (macro halos) count only with -soft_blockages block.
  std::vector<odb::Rect> obstacles;
  for (int v = 0; v < netlist.numVertices(); ++v) {
    const Vertex& vert = netlist.vertex(v);
    if (vert.fixed && vert.inst) {
      obstacles.push_back(vert.inst->getBBox()->getBox());
    }
  }
  int hard_blockages = 0;
  int soft_blockages = 0;
  for (odb::dbBlockage* blockage : block->getBlockages()) {
    if (blockage->isSoft()) {
      ++soft_blockages;
      if (!options.block_soft_blockages) {
        continue;
      }
    } else {
      ++hard_blockages;
    }
    obstacles.push_back(blockage->getBBox()->getBox());
  }
  state->area = std::make_unique<PlacementArea>(block, obstacles);
  PlacementArea& area = *state->area;
  if (area.totalUsableArea() <= 0) {
    logger_->error(utl::MCP, 6, "No free placement area in the rows.");
  }
  if (verbose && (hard_blockages > 0 || soft_blockages > 0)) {
    logger_->info(utl::MCP,
                  50,
                  "Blockages: {} hard, {} soft ({}); {} obstacles in all",
                  hard_blockages,
                  soft_blockages,
                  options.block_soft_blockages ? "blocked" : "ignored",
                  obstacles.size());
  }

  CapacityPolicy policy;
  policy.reserve = options.reserve;
  policy.density = options.density;
  policy.tolerance = options.balance_tolerance;
  policy.max_tolerance = options.max_tolerance;
  state->tree = std::make_unique<RegionTree>(
      area, netlist, policy, options.end_case_cells, logger_);
  RegionTree& tree = *state->tree;
  const int root_id = tree.makeRoot();
  const Region& root = tree.region(root_id);

  if (verbose) {
    logger_->info(
        utl::MCP,
        13,
        "Core {} x {} um, {} rows, site width {} um, usable area "
        "{:.1f} um^2, root utilization {:.1f}%",
        area.core().dx() / dbu,
        area.core().dy() / dbu,
        area.rowsIn(area.core()),
        area.siteWidth() / dbu,
        root.usable_area / (dbu * dbu),
        root.usable_area > 0 ? 100.0 * root.cell_area / root.usable_area : 0.0);
  }

  // Engines: the main one, an optional one for the top levels, and the
  // flat FM engine as fallback when an engine fails.
  std::unique_ptr<Engine> engine
      = makeEngine(options.engine, options, partition_mgr_, logger_);
  std::unique_ptr<Engine> top_engine;
  if (options.top_levels > 0 && options.top_engine != options.engine) {
    top_engine
        = makeEngine(options.top_engine, options, partition_mgr_, logger_);
  }
  std::unique_ptr<Engine> fallback
      = makeEngine(EngineKind::kFm, options, partition_mgr_, logger_);
  const std::string objective_name
      = options.fine_level >= 0
            ? objectiveToString(options.objective) + " then "
                  + objectiveToString(options.fine_objective) + " from level "
                  + std::to_string(options.fine_level)
            : objectiveToString(options.objective);
  logger_->info(
      utl::MCP,
      14,
      "Engine {}{}{}, objective {}, k = {}, terminals {}, end case <= {} "
      "cells, tolerance {:.3f}, reserve {} at density {}, seed {}, threads {}",
      engine->name(),
      top_engine ? " after " + top_engine->name() + " for levels < " : "",
      top_engine ? std::to_string(options.top_levels) : "",
      objective_name,
      options.k == 0 ? "auto" : std::to_string(options.k),
      options.terminals == TerminalMode::kPropagate ? "propagate" : "none",
      options.end_case_cells,
      options.balance_tolerance,
      reservePolicyToString(options.reserve),
      options.density > 0.0 ? fmt::format("{:.2f}", options.density)
                            : std::string("utilization"),
      options.seed,
      options.threads);
  logger_->info(utl::MCP,
                37,
                "End game: exact partitioning <= {} cells, optimal end case "
                "<= {} cells, row ironing window {} x {} passes",
                options.exact_cells,
                options.optimal_cells,
                options.iron_window,
                options.iron_passes);
  const bool geometric = options.objective != Objective::kCutNet
                         || (options.fine_level >= 0
                             && options.fine_objective != Objective::kCutNet);
  if (geometric
      && (!engine->supportsVectors()
          || (top_engine && !top_engine->supportsVectors()))) {
    logger_->warn(utl::MCP,
                  24,
                  "Objectives other than cutnet need the fm or tritonpart "
                  "engine; the fm engine is used for those levels.");
  }

  state->report = std::make_unique<RunReport>();
  RunReport& report = *state->report;
  Decomposer decomposer(netlist,
                        area,
                        tree,
                        options,
                        engine.get(),
                        top_engine.get(),
                        fallback.get(),
                        debug_observer_.get(),
                        logger_);
  decomposer.run(root_id, report);

  const double hpwl_um = computeHpwl(block) / dbu;
  state->hpwl_um = hpwl_um;
  logger_->info(utl::MCP,
                16,
                "Placed {} cells in {} end-case regions ({} overflow cells, "
                "{} overfull regions), {} regions total",
                report.end_case.cells,
                report.end_case.regions,
                report.end_case.overflow_cells,
                report.overfull_regions,
                report.regions);
  int exact_regions = 0;
  int exact_improved = 0;
  int exact_incomplete = 0;
  for (const LevelStats& l : report.levels) {
    exact_regions += l.exact_regions;
    exact_improved += l.exact_improved;
    exact_incomplete += l.exact_incomplete;
  }
  if (exact_regions > 0 || report.end_case.optimal_regions > 0) {
    logger_->info(utl::MCP,
                  35,
                  "Exact partitioning: {} regions, {} improved, {} incomplete; "
                  "optimal end case: {} leaves, {} improved, {} incomplete, "
                  "HPWL -{:.0f} um",
                  exact_regions,
                  exact_improved,
                  exact_incomplete,
                  report.end_case.optimal_regions,
                  report.end_case.optimal_improved,
                  report.end_case.optimal_incomplete,
                  report.end_case.optimal_gain / dbu);
  }
  if (report.end_case.refine_passes > 0) {
    logger_->info(utl::MCP,
                  38,
                  "End-case feedback: {} passes, {} leaves revisited, {} "
                  "rewritten, HPWL -{:.0f} um",
                  report.end_case.refine_passes,
                  report.end_case.refine_regions,
                  report.end_case.refine_improved,
                  report.end_case.refine_gain / dbu);
  }
  if (report.ironing.passes > 0) {
    logger_->info(utl::MCP,
                  36,
                  "Row ironing: {} passes, {} windows, {} improved, {} "
                  "incomplete, HPWL -{:.0f} um",
                  report.ironing.passes,
                  report.ironing.windows,
                  report.ironing.improved,
                  report.ironing.incomplete,
                  report.ironing.gain / dbu);
  }
  logger_->info(utl::MCP, 17, "HPWL {:.0f} um", hpwl_um);
  debugPrint(logger_,
             utl::MCP,
             "timing",
             1,
             "Decomposition {:.2f} s, end cases {:.2f} s, ironing {:.2f} s",
             report.decompose_seconds,
             report.end_case_seconds,
             report.ironing.seconds);
  return state;
}

void MinCutPlacer::writeRegions(const Options& options,
                                const RegionTree& tree,
                                const NetlistModel& netlist) const
{
  std::ofstream out(options.regions_file);
  if (!out) {
    logger_->warn(
        utl::MCP, 29, "Cannot write regions file {}", options.regions_file);
    return;
  }
  out << "# id parent level xmin ymin xmax ymax cells cell_area usable_area "
         "cut_x cut_y_left cut_y_right\n";
  std::vector<int> leaf_of(netlist.numVertices(), -1);
  for (int id = 0; id < tree.numRegions(); ++id) {
    const Region& r = tree.region(id);
    out << id << ' ' << r.parent << ' ' << r.level << ' ' << r.box.xMin() << ' '
        << r.box.yMin() << ' ' << r.box.xMax() << ' ' << r.box.yMax() << ' '
        << r.cells.size() << ' ' << r.cell_area << ' ' << r.usable_area << ' '
        << r.cut_x << ' ' << r.cut_y_left << ' ' << r.cut_y_right << '\n';
    if (r.isLeaf()) {
      for (int v : r.cells) {
        leaf_of[v] = id;
      }
    }
  }
  out << "# C inst leaf seed_x seed_y final_x final_y width height\n";
  for (int v = 0; v < netlist.numVertices(); ++v) {
    const Vertex& vert = netlist.vertex(v);
    if (vert.inst == nullptr || vert.fixed) {
      continue;
    }
    const odb::Point now = vert.inst->getBBox()->getBox().center();
    out << "C " << vert.inst->getName() << ' ' << leaf_of[v] << ' '
        << vert.loc.x() << ' ' << vert.loc.y() << ' ' << now.x() << ' '
        << now.y() << ' ' << vert.inst->getMaster()->getWidth() << ' '
        << vert.inst->getMaster()->getHeight() << '\n';
  }
  logger_->info(utl::MCP, 34, "Wrote regions {}", options.regions_file);
}

void MinCutPlacer::writeHypergraph(const Options& options,
                                   const NetlistModel& netlist,
                                   const PlacementArea& area,
                                   int dbu) const
{
  std::ofstream out(options.hypergraph_file);
  if (!out) {
    logger_->warn(utl::MCP,
                  53,
                  "Cannot write hypergraph file {}",
                  options.hypergraph_file);
    return;
  }
  const odb::Rect& core = area.core();
  out << "# H vertices nets dbu site_width row_height core_xmin core_ymin "
         "core_xmax core_ymax\n";
  out << "H " << netlist.numVertices() << ' ' << netlist.numNets() << ' '
      << dbu << ' ' << area.siteWidth() << ' ' << area.rowHeight() << ' '
      << core.xMin() << ' ' << core.yMin() << ' ' << core.xMax() << ' '
      << core.yMax() << '\n';
  out << "# V id kind(M|F|P) name x y width height area\n";
  for (int v = 0; v < netlist.numVertices(); ++v) {
    const Vertex& vert = netlist.vertex(v);
    const char kind = vert.bterm != nullptr ? 'P' : (vert.fixed ? 'F' : 'M');
    const std::string name = vert.bterm != nullptr
                                 ? vert.bterm->getName()
                                 : vert.inst->getName();
    out << "V " << v << ' ' << kind << ' ' << name << ' ' << vert.loc.x()
        << ' ' << vert.loc.y() << ' ' << vert.width << ' ' << vert.height
        << ' ' << vert.area << '\n';
  }
  out << "# N id weight name v1 ox1 oy1 v2 ox2 oy2 ...\n";
  for (int e = 0; e < netlist.numNets(); ++e) {
    const auto verts = netlist.netVertices(e);
    const auto offsets = netlist.netPinOffsets(e);
    out << "N " << e << ' ' << netlist.netWeight(e) << ' '
        << netlist.net(e)->getName();
    for (size_t j = 0; j < verts.size(); ++j) {
      out << ' ' << verts[j] << ' ' << offsets[j].x() << ' ' << offsets[j].y();
    }
    out << '\n';
  }
  logger_->info(utl::MCP, 54, "Wrote hypergraph {}", options.hypergraph_file);
}

// HPWL of all signal nets with at least two terminals, from the
// database (so unplaced instances count at their current location).
int64_t MinCutPlacer::computeHpwl(odb::dbBlock* block) const
{
  int64_t hpwl = 0;
  for (odb::dbNet* net : block->getNets()) {
    if (net->getSigType().isSupply() || net->isSpecial()) {
      continue;
    }
    if (net->getITerms().size() + net->getBTerms().size() < 2) {
      continue;
    }
    const odb::Rect bbox = net->getTermBBox();
    hpwl += static_cast<int64_t>(bbox.dx()) + bbox.dy();
  }
  return hpwl;
}

void MinCutPlacer::writeReport(const Options& options,
                               const RunReport& report,
                               double hpwl_um,
                               const NetlistStats& st,
                               const std::vector<double>& pass_hpwl,
                               int best_pass) const
{
  std::ofstream out(options.report_file);
  if (!out) {
    logger_->warn(
        utl::MCP, 23, "Cannot write report file {}", options.report_file);
    return;
  }
  const double dbu = db_->getChip()->getBlock()->getDbUnitsPerMicron();
  auto engine_name = [](EngineKind kind) {
    switch (kind) {
      case EngineKind::kFm:
        return "fm";
      case EngineKind::kTritonPart:
        return "tritonpart";
      case EngineKind::kHmetis:
        return "hmetis";
      case EngineKind::kMtKaHyPar:
        return "mtkahypar";
      case EngineKind::kFastPart:
        return "fastpart";
    }
    return "unknown";
  };
  out << "{\n";
  out << "  \"schema\": 1,\n";
  out << "  \"options\": {\n";
  out << "    \"k\": " << options.k << ",\n";
  out << "    \"objective\": \"" << objectiveToString(options.objective)
      << "\",\n";
  out << "    \"fine_objective\": \""
      << objectiveToString(options.fine_objective) << "\",\n";
  out << "    \"fine_level\": " << options.fine_level << ",\n";
  out << "    \"net_weight\": \"" << netWeightRuleToString(options.net_weight)
      << "\",\n";
  out << "    \"net_weights_file\": \"" << options.net_weights_file << "\",\n";
  out << "    \"balance_objective\": \""
      << balanceObjectiveToString(options.balance_objective) << "\",\n";
  out << "    \"balance_fractions\": [";
  for (size_t i = 0; i < options.balance_fractions.size(); ++i) {
    out << (i ? ", " : "") << options.balance_fractions[i];
  }
  out << "],\n";
  out << "    \"balance_sweep_tolerance\": " << options.balance_sweep_tolerance
      << ",\n";
  out << "    \"balance_levels\": " << options.balance_levels << ",\n";
  out << "    \"cut_direction\": \""
      << cutDirectionToString(options.cut_direction) << "\",\n";
  out << "    \"engine\": \"" << engine_name(options.engine) << "\",\n";
  out << "    \"top_engine\": \"" << engine_name(options.top_engine) << "\",\n";
  out << "    \"top_levels\": " << options.top_levels << ",\n";
  out << "    \"fastpart_time\": " << options.fastpart_time << ",\n";
  out << "    \"fastpart_decay\": " << options.fastpart_decay << ",\n";
  out << "    \"fastpart_min_time\": " << options.fastpart_min_time << ",\n";
  out << "    \"fastpart_min_cells\": " << options.fastpart_min_cells << ",\n";
  out << "    \"fastpart_stall\": " << options.fastpart_stall << ",\n";
  out << "    \"density\": " << options.density << ",\n";
  out << "    \"balance_tolerance\": " << options.balance_tolerance << ",\n";
  out << "    \"reserve\": \"" << reservePolicyToString(options.reserve)
      << "\",\n";
  out << "    \"max_tolerance\": " << options.max_tolerance << ",\n";
  out << "    \"reserve_blend\": " << CapacityPolicy().blend << ",\n";
  out << "    \"terminals\": \""
      << (options.terminals == TerminalMode::kPropagate ? "propagate" : "none")
      << "\",\n";
  out << "    \"terminal_band\": " << options.terminal_band << ",\n";
  out << "    \"soft_blockages\": \""
      << (options.block_soft_blockages ? "block" : "ignore") << "\",\n";
  out << "    \"waves\": " << options.waves << ",\n";
  out << "    \"end_case_cells\": " << options.end_case_cells << ",\n";
  out << "    \"exact_cells\": " << options.exact_cells << ",\n";
  out << "    \"optimal_cells\": " << options.optimal_cells << ",\n";
  out << "    \"optimal_rows\": " << (options.optimal_rows ? "true" : "false")
      << ",\n";
  out << "    \"iron_window\": " << options.iron_window << ",\n";
  out << "    \"iron_passes\": " << options.iron_passes << ",\n";
  out << "    \"end_case_passes_requested\": " << options.end_case_passes
      << ",\n";
  out << "    \"leaf_positions\": \""
      << positionRuleToString(options.leaf_positions) << "\",\n";
  out << "    \"leaf_pad\": " << options.leaf_pad << ",\n";
  out << "    \"max_fanout\": " << options.max_fanout << ",\n";
  out << "    \"pad_left\": " << options.pad_left << ",\n";
  out << "    \"pad_right\": " << options.pad_right << ",\n";
  out << "    \"seed_placement\": " << (options.seed_placement ? 1 : 0)
      << ",\n";
  out << "    \"assignment\": \"" << assignmentModeToString(options.assignment)
      << "\",\n";
  out << "    \"seed_levels\": " << options.seed_levels << ",\n";
  out << "    \"am_mincut_levels\": [";
  for (size_t i = 0; i < options.am_mincut_levels.size(); ++i) {
    out << (i > 0 ? ", " : "") << options.am_mincut_levels[i];
  }
  out << "],\n";
  out << "    \"am_diagnose\": " << (options.am_diagnose ? 1 : 0) << ",\n";
  out << "    \"am_commit\": \""
      << (options.am_commit_mincut ? "mincut" : "transport") << "\",\n";
  out << "    \"displacement_weight\": " << options.displacement_weight
      << ",\n";
  out << "    \"seed_spread\": " << options.seed_spread << ",\n";
  out << "    \"assignment_net_weight\": " << options.assignment_net_weight
      << ",\n";
  out << "    \"assignment_rounds\": " << options.assignment_rounds << ",\n";
  out << "    \"seed_terminals\": " << (options.seed_terminals ? 1 : 0)
      << ",\n";
  out << "    \"oracle\": \"" << oracleKindToString(options.oracle) << "\",\n";
  out << "    \"oracle_anchor\": " << options.oracle_anchor << ",\n";
  out << "    \"oracle_anchor_util\": " << options.oracle_anchor_util
      << ",\n";
  out << "    \"oracle_rounds\": " << options.oracle_rounds << ",\n";
  out << "    \"oracle_min_level\": " << options.oracle_min_level << ",\n";
  out << "    \"oracle_terminals\": " << (options.oracle_terminals ? 1 : 0)
      << ",\n";
  out << "    \"kway_refine_levels\": [";
  for (size_t i = 0; i < options.kway_refine_levels.size(); ++i) {
    out << (i > 0 ? ", " : "") << options.kway_refine_levels[i];
  }
  out << "],\n";
  out << "    \"kway_construct_levels\": [";
  for (size_t i = 0; i < options.kway_construct_levels.size(); ++i) {
    out << (i > 0 ? ", " : "") << options.kway_construct_levels[i];
  }
  out << "],\n";
  out << "    \"kway_slack\": " << options.kway_slack << ",\n";
  out << "    \"kway_vcycles\": " << options.kway_vcycles << ",\n";
  out << "    \"sweep_levels\": [";
  for (size_t i = 0; i < options.sweep_levels.size(); ++i) {
    out << (i > 0 ? ", " : "") << options.sweep_levels[i];
  }
  out << "],\n";
  out << "    \"sweep_passes\": " << options.sweep_passes << ",\n";
  out << "    \"sweep_slack\": " << options.sweep_slack << ",\n";
  out << "    \"sweep_multilevel\": "
      << (options.sweep_multilevel ? "true" : "false") << ",\n";
  out << "    \"sweep_warm\": " << (options.sweep_warm ? "true" : "false")
      << ",\n";
  out << "    \"feedback_passes_requested\": " << options.feedback_passes
      << ",\n";
  out << "    \"feedback_weight\": " << options.feedback_weight << ",\n";
  out << "    \"feedback_assignment\": \""
      << assignmentModeToString(options.feedback_assignment) << "\",\n";
  out << "    \"max_moves\": " << options.max_moves << ",\n";
  out << "    \"refiner_iters\": " << options.refiner_iters << ",\n";
  out << "    \"engine_runs\": " << options.engine_runs << ",\n";
  out << "    \"cut_degrade\": " << options.cut_degrade << ",\n";
  out << "    \"seed_leaf\": " << (options.seed_leaf ? 1 : 0) << ",\n";
  out << "    \"seed\": " << options.seed << ",\n";
  out << "    \"threads\": " << options.threads << "\n";
  out << "  },\n";
  out << "  \"netlist\": {\n";
  out << "    \"movable\": " << st.movable << ",\n";
  out << "    \"fixed\": " << st.fixed_insts << ",\n";
  out << "    \"io_pins\": " << st.io_pins << ",\n";
  out << "    \"nets\": " << st.nets_kept << "\n";
  out << "  },\n";
  out << "  \"levels\": [\n";
  for (size_t i = 0; i < report.levels.size(); ++i) {
    const LevelStats& l = report.levels[i];
    out << "    {\"level\": " << l.level << ", \"regions\": " << l.regions
        << ", \"cells\": " << l.cells << ", \"end_cases\": " << l.end_cases
        << ", \"nets\": " << l.nets << ", \"cut_nets\": " << l.cut_nets
        << ", \"inessential\": " << l.inessential
        << ", \"engine_failures\": " << l.engine_failures
        << ", \"balance_violations\": " << l.balance_violations
        << ", \"assignment_split\": " << l.assignment_split
        << ", \"assignment_repairs\": " << l.assignment_repairs
        << ", \"assignment_infeasible\": " << l.assignment_infeasible
        << ", \"assignment_dhpwl_um\": " << l.assignment_dhpwl / dbu
        << ", \"oracle_hpwl_um\": " << l.oracle_hpwl / dbu
        << ", \"oracle_seconds\": " << l.oracle_seconds
        << ", \"exact_regions\": " << l.exact_regions
        << ", \"exact_improved\": " << l.exact_improved
        << ", \"exact_incomplete\": " << l.exact_incomplete
        << ", \"balance_sweeps\": " << l.balance_sweeps
        << ", \"balance_offcenter\": " << l.balance_offcenter
        << ", \"direction_choices\": " << l.direction_choices
        << ", \"direction_flips\": " << l.direction_flips
        << ", \"kway_k\": " << l.kway_k << ", \"kway_moved\": " << l.kway_moved
        << ", \"kway_hpwl_before_um\": " << l.kway_hpwl_before / dbu
        << ", \"kway_hpwl_after_um\": " << l.kway_hpwl_after / dbu
        << ", \"kway_accepted\": " << (l.kway_accepted ? "true" : "false")
        << ", \"kway_seconds\": " << l.kway_seconds
        << ", \"kwayc_moved\": " << l.kwayc_moved
        << ", \"kwayc_hpwl_before_um\": " << l.kwayc_hpwl_before / dbu
        << ", \"kwayc_hpwl_after_um\": " << l.kwayc_hpwl_after / dbu
        << ", \"kwayc_accepted\": " << (l.kwayc_accepted ? "true" : "false")
        << ", \"kwayc_seconds\": " << l.kwayc_seconds
        << ", \"sweep_pairs\": " << l.sweep_pairs
        << ", \"sweep_passes\": " << l.sweep_passes
        << ", \"sweep_calls\": " << l.sweep_calls
        << ", \"sweep_accepted\": " << l.sweep_accepted
        << ", \"sweep_noop\": " << l.sweep_noop
        << ", \"sweep_rejected\": " << l.sweep_rejected
        << ", \"sweep_infeasible\": " << l.sweep_infeasible
        << ", \"sweep_moved\": " << l.sweep_moved
        << ", \"sweep_hpwl_before_um\": " << l.sweep_hpwl_before / dbu
        << ", \"sweep_hpwl_after_um\": " << l.sweep_hpwl_after / dbu
        << ", \"sweep_curve_um\": [" << sweepCurve(l.sweep_curve, dbu) << "]"
        << ", \"sweep_model_delta_um\": " << l.sweep_model_delta / dbu
        << ", \"sweep_exact_delta_um\": " << l.sweep_exact_delta / dbu
        << ", \"sweep_audit_errors\": " << l.sweep_audit_errors
        << ", \"sweep_seconds\": " << l.sweep_seconds
        << ", \"seed_outside\": " << l.seed_outside
        << ", \"seed_distance_um\": "
        << (l.seed_outside > 0 ? l.seed_distance / l.seed_outside / dbu : 0.0)
        << ", \"transport_seconds\": " << l.transport_seconds
        << ", \"am_regions\": " << l.am_regions
        << ", \"am_cells\": " << l.am_cells
        << ", \"am_disagree_cells\": " << l.am_disagree_cells
        << ", \"am_disagree_area_frac\": "
        << (l.am_area > 0.0 ? l.am_disagree_area / l.am_area : 0.0)
        << ", \"am_hpwl_mincut_um\": " << l.am_hpwl_mincut / dbu
        << ", \"am_hpwl_transport_um\": " << l.am_hpwl_transport / dbu
        << ", \"am_disp_mincut_um\": "
        << (l.am_area > 0.0 ? l.am_disp_mincut / l.am_area / dbu : 0.0)
        << ", \"am_disp_transport_um\": "
        << (l.am_area > 0.0 ? l.am_disp_transport / l.am_area / dbu : 0.0)
        << ", \"am_cut_mincut\": " << l.am_cut_mincut
        << ", \"am_cut_transport\": " << l.am_cut_transport
        << ", \"degrade_regions\": " << l.degrade_regions
        << ", \"degrade_cut_before\": " << l.degrade_cut_before
        << ", \"degrade_cut_after\": " << l.degrade_cut_after
        << ", \"degrade_swaps\": " << l.degrade_swaps
        << ", \"seconds\": " << l.seconds << "}"
        << (i + 1 < report.levels.size() ? "," : "") << "\n";
  }
  out << "  ],\n";
  out << "  \"result\": {\n";
  out << "    \"hpwl_um\": " << std::fixed << std::setprecision(1) << hpwl_um
      << ",\n";
  out << "    \"regions\": " << report.regions << ",\n";
  out << "    \"end_case_regions\": " << report.end_case.regions << ",\n";
  out << "    \"overflow_cells\": " << report.end_case.overflow_cells << ",\n";
  out << "    \"overfull_regions\": " << report.overfull_regions << ",\n";
  out << "    \"leaves_with_cells\": " << report.leaves_with_cells << ",\n";
  out << "    \"leaf_density_mean\": " << std::setprecision(4)
      << report.leaf_density_mean << ",\n";
  out << "    \"leaf_density_max\": " << report.leaf_density_max << ",\n";
  out << "    \"occupied_area_fraction\": " << report.occupied_area_fraction
      << ",\n";
  out << "    \"optimal_regions\": " << report.end_case.optimal_regions
      << ",\n";
  out << "    \"optimal_improved\": " << report.end_case.optimal_improved
      << ",\n";
  out << "    \"optimal_incomplete\": " << report.end_case.optimal_incomplete
      << ",\n";
  out << "    \"optimal_nodes\": " << report.end_case.optimal_nodes << ",\n";
  out << "    \"optimal_gain_um\": " << std::setprecision(1)
      << report.end_case.optimal_gain / dbu << ",\n";
  out << "    \"end_case_passes\": " << report.end_case.refine_passes + 1
      << ",\n";
  out << "    \"refine_regions\": " << report.end_case.refine_regions << ",\n";
  out << "    \"refine_improved\": " << report.end_case.refine_improved
      << ",\n";
  out << "    \"refine_gain_um\": " << report.end_case.refine_gain / dbu
      << ",\n";
  out << "    \"leaf_rows_available\": " << report.end_case.rows_available
      << ",\n";
  out << "    \"leaf_rows_used\": " << report.end_case.rows_used << ",\n";
  out << "    \"leaf_seed_rows_used\": " << report.end_case.seed_rows_used
      << ",\n";
  out << "    \"leaf_final_cost_um\": " << report.end_case.final_cost / dbu
      << ",\n";
  out << "    \"leaf_seed_cost_um\": " << report.end_case.seed_cost / dbu
      << ",\n";
  out << "    \"ironing_passes\": " << report.ironing.passes << ",\n";
  out << "    \"ironing_windows\": " << report.ironing.windows << ",\n";
  out << "    \"ironing_improved\": " << report.ironing.improved << ",\n";
  out << "    \"ironing_incomplete\": " << report.ironing.incomplete << ",\n";
  out << "    \"ironing_nodes\": " << report.ironing.nodes << ",\n";
  out << "    \"ironing_gain_um\": " << report.ironing.gain / dbu << ",\n";
  out << "    \"hpwl_before_ironing_um\": " << report.hpwl_before_ironing / dbu
      << ",\n";
  out << "    \"feedback_passes\": " << pass_hpwl.size() << ",\n";
  out << "    \"feedback_best_pass\": " << best_pass << ",\n";
  out << "    \"feedback_hpwl_um\": [";
  for (size_t i = 0; i < pass_hpwl.size(); ++i) {
    out << (i > 0 ? ", " : "") << pass_hpwl[i];
  }
  out << "],\n";
  out << "    \"decompose_seconds\": " << std::setprecision(3)
      << report.decompose_seconds << ",\n";
  out << "    \"end_case_seconds\": " << report.end_case_seconds << ",\n";
  out << "    \"ironing_seconds\": " << report.ironing.seconds << "\n";
  out << "  }\n";
  out << "}\n";
  logger_->info(utl::MCP, 18, "Wrote report {}", options.report_file);
}

void MinCutPlacer::setDebug(std::unique_ptr<Observer>& observer)
{
  debug_observer_ = std::move(observer);
}

EngineKind engineKindFromString(const std::string& name)
{
  if (name == "fm") {
    return EngineKind::kFm;
  }
  if (name == "tritonpart") {
    return EngineKind::kTritonPart;
  }
  if (name == "hmetis") {
    return EngineKind::kHmetis;
  }
  if (name == "mtkahypar") {
    return EngineKind::kMtKaHyPar;
  }
  if (name == "fastpart") {
    return EngineKind::kFastPart;
  }
  throw std::invalid_argument("unknown engine: " + name);
}

Objective objectiveFromString(const std::string& name)
{
  if (name == "cutnet") {
    return Objective::kCutNet;
  }
  if (name == "conn") {
    return Objective::kConn;
  }
  if (name == "hpwl") {
    return Objective::kHpwl;
  }
  if (name == "phpwl") {
    return Objective::kPhpwl;
  }
  if (name == "rsmt") {
    return Objective::kRsmt;
  }
  throw std::invalid_argument("unknown objective: " + name);
}

std::string objectiveToString(Objective objective)
{
  switch (objective) {
    case Objective::kCutNet:
      return "cutnet";
    case Objective::kConn:
      return "conn";
    case Objective::kHpwl:
      return "hpwl";
    case Objective::kPhpwl:
      return "phpwl";
    case Objective::kRsmt:
      return "rsmt";
  }
  return "unknown";
}

NetWeightRule netWeightRuleFromString(const std::string& name)
{
  if (name == "uniform") {
    return NetWeightRule::kUniform;
  }
  if (name == "exphpwl") {
    return NetWeightRule::kExpHpwl;
  }
  if (name == "invdeg") {
    return NetWeightRule::kInvDegree;
  }
  throw std::invalid_argument("unknown net weight rule: " + name);
}

const char* netWeightRuleToString(NetWeightRule rule)
{
  switch (rule) {
    case NetWeightRule::kUniform:
      return "uniform";
    case NetWeightRule::kExpHpwl:
      return "exphpwl";
    case NetWeightRule::kInvDegree:
      return "invdeg";
  }
  return "uniform";
}

double netWeightFor(NetWeightRule rule, int pins)
{
  const int d = std::max(2, pins);
  switch (rule) {
    case NetWeightRule::kUniform:
      return 1.0;
    case NetWeightRule::kExpHpwl: {
      // d pins uniform on [0, 1], a cut at 1/2.  Expected span when
      // all pins are in one half: (d - 1) / (d + 1) / 2; when the net
      // is cut (pins in both halves): (d - 1) / (d + 1) * (1 - 2^-d) /
      // (1 - 2^(1-d)).  The difference is the HPWL the partitioner
      // gains by un-cutting the net; 1/3 for d = 2, 1/2 for d -> inf.
      const double span = static_cast<double>(d - 1) / (d + 1);
      const double cut
          = (1.0 - std::ldexp(1.0, -d)) / (1.0 - std::ldexp(1.0, 1 - d));
      const double gain = span * (cut - 0.5);
      return gain / (1.0 / 3.0);
    }
    case NetWeightRule::kInvDegree:
      return 1.0 / (d - 1);
  }
  return 1.0;
}

BalanceObjective balanceObjectiveFromString(const std::string& name)
{
  if (name == "bounds") {
    return BalanceObjective::kBounds;
  }
  if (name == "ratio") {
    return BalanceObjective::kRatio;
  }
  if (name == "ncut") {
    return BalanceObjective::kNcut;
  }
  throw std::invalid_argument("unknown balance objective: " + name);
}

const char* balanceObjectiveToString(BalanceObjective objective)
{
  switch (objective) {
    case BalanceObjective::kBounds:
      return "bounds";
    case BalanceObjective::kRatio:
      return "ratio";
    case BalanceObjective::kNcut:
      return "ncut";
  }
  return "bounds";
}

CutDirection cutDirectionFromString(const std::string& name)
{
  if (name == "longer") {
    return CutDirection::kLonger;
  }
  if (name == "sparsest") {
    return CutDirection::kSparsest;
  }
  throw std::invalid_argument("unknown cut direction: " + name);
}

const char* cutDirectionToString(CutDirection direction)
{
  switch (direction) {
    case CutDirection::kLonger:
      return "longer";
    case CutDirection::kSparsest:
      return "sparsest";
  }
  return "longer";
}

TerminalMode terminalModeFromString(const std::string& name)
{
  if (name == "none") {
    return TerminalMode::kNone;
  }
  if (name == "propagate") {
    return TerminalMode::kPropagate;
  }
  throw std::invalid_argument("unknown terminal mode: " + name);
}

PositionRule positionRuleFromString(const std::string& name)
{
  if (name == "even") {
    return PositionRule::kEven;
  }
  if (name == "optimal") {
    return PositionRule::kOptimal;
  }
  if (name == "joint") {
    return PositionRule::kJoint;
  }
  throw std::invalid_argument("unknown position rule: " + name);
}

const char* positionRuleToString(PositionRule rule)
{
  switch (rule) {
    case PositionRule::kEven:
      return "even";
    case PositionRule::kOptimal:
      return "optimal";
    case PositionRule::kJoint:
      return "joint";
  }
  return "optimal";
}

OracleKind oracleKindFromString(const std::string& name)
{
  if (name == "none") {
    return OracleKind::kNone;
  }
  if (name == "qp") {
    return OracleKind::kQp;
  }
  throw std::invalid_argument("unknown oracle: " + name);
}

const char* oracleKindToString(OracleKind kind)
{
  switch (kind) {
    case OracleKind::kNone:
      return "none";
    case OracleKind::kQp:
      return "qp";
  }
  return "none";
}

AssignmentMode assignmentModeFromString(const std::string& name)
{
  if (name == "none") {
    return AssignmentMode::kNone;
  }
  if (name == "transport") {
    return AssignmentMode::kTransport;
  }
  if (name == "transport_only") {
    return AssignmentMode::kTransportOnly;
  }
  throw std::invalid_argument("unknown assignment mode: " + name);
}

const char* assignmentModeToString(AssignmentMode mode)
{
  switch (mode) {
    case AssignmentMode::kNone:
      return "none";
    case AssignmentMode::kTransport:
      return "transport";
    case AssignmentMode::kTransportOnly:
      return "transport_only";
  }
  return "unknown";
}

}  // namespace mcp
