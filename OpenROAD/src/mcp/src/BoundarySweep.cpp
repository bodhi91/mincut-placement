// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

// X2: reversible boundary sweeps (hypothesis H1 of the boundary-sweep
// study) inside the min-cut tree.
//
// After a level is committed every pair of adjacent regions is treated as
// one two-bin problem: the movable cells of both regions may be put on
// either side, the objective is the HPWL of the placer's own surrogate
// (local pins at the two region centers, every other pin where it
// currently is -- the hpwl net vectors of SubproblemBuilder), the
// capacities are the regions' current loads times a slack.  The engine's
// answer is accepted only if the exact HPWL at the centers (Decomposer::
// hpwl, pin offsets included) decreases.  Passes over all pairs repeat
// until nothing is accepted.  A cell may move back and forth between
// passes, or across several regions over the passes: the level's
// decisions are no longer irrevocable.
//
// The tree is not a uniform grid, so the spec's boundary-cut identity
// (one boundary's weighted cut = the surrogate's change) does not hold
// literally; the exact per-net evaluation takes its place, and the solver
// model's predicted change is recorded next to the exact one so that the
// gap can be measured (pin offsets of the local cells are the only
// modeled difference).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Decomposer.h"
#include "ExactPartition.h"
#include "NetlistModel.h"
#include "PlacementArea.h"
#include "Region.h"
#include "RegionTree.h"
#include "utl/Logger.h"

namespace mcp {

namespace {

double sweepSeconds(std::chrono::steady_clock::time_point start)
{
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
      .count();
}

// Node budget of the exact search on a small pair (as Decomposer::solve).
constexpr int64_t kSweepExactBudget = 200000;

struct Pair
{
  int a;  // left / lower region
  int b;  // right / upper region
  bool x_pair;
  int boundary;  // coordinate of the shared edge
  int along;     // lower end of the shared edge (ordering)
};

}  // namespace

void Decomposer::boundarySweep(int level,
                               const std::vector<int>& regions,
                               SubproblemBuilder& builder,
                               LevelStats& stats)
{
  const auto start = std::chrono::steady_clock::now();
  if (regions.size() < 2) {
    return;
  }

  // Capacities: the tree's whitespace decision (the current load, with
  // a slack), never more than the region can hold; an overfull region
  // may only lose cells.
  std::vector<int64_t> cap(tree_.numRegions(), 0);
  for (int id : regions) {
    const Region& r = tree_.region(id);
    const int64_t slack
        = static_cast<int64_t>(std::ceil(r.cell_area * options_.sweep_slack));
    cap[id] = std::max(r.cell_area, std::min(slack, r.usable_area));
  }

  // Adjacent pairs: boxes sharing an edge of positive length.
  std::vector<Pair> pairs;
  for (size_t i = 0; i < regions.size(); ++i) {
    const odb::Rect& p = tree_.region(regions[i]).box;
    for (size_t j = i + 1; j < regions.size(); ++j) {
      const odb::Rect& q = tree_.region(regions[j]).box;
      const int y_overlap
          = std::min(p.yMax(), q.yMax()) - std::max(p.yMin(), q.yMin());
      const int x_overlap
          = std::min(p.xMax(), q.xMax()) - std::max(p.xMin(), q.xMin());
      if (y_overlap > 0 && (p.xMax() == q.xMin() || q.xMax() == p.xMin())) {
        const bool p_left = p.xMax() == q.xMin();
        pairs.push_back({p_left ? regions[i] : regions[j],
                         p_left ? regions[j] : regions[i],
                         true,
                         p_left ? p.xMax() : q.xMax(),
                         std::max(p.yMin(), q.yMin())});
      } else if (x_overlap > 0
                 && (p.yMax() == q.yMin() || q.yMax() == p.yMin())) {
        const bool p_lower = p.yMax() == q.yMin();
        pairs.push_back({p_lower ? regions[i] : regions[j],
                         p_lower ? regions[j] : regions[i],
                         false,
                         p_lower ? p.yMax() : q.yMax(),
                         std::max(p.xMin(), q.xMin())});
      }
    }
  }
  // x boundaries first, then y; deterministic order within each.
  std::sort(pairs.begin(), pairs.end(), [](const Pair& l, const Pair& r) {
    if (l.x_pair != r.x_pair) {
      return l.x_pair;
    }
    if (l.boundary != r.boundary) {
      return l.boundary < r.boundary;
    }
    return l.along < r.along;
  });
  stats.sweep_pairs = static_cast<int>(pairs.size());
  if (pairs.empty()) {
    return;
  }

  const int64_t before = hpwl();
  int64_t current = before;
  stats.sweep_hpwl_before = static_cast<double>(before);
  stats.sweep_hpwl_after = stats.sweep_hpwl_before;
  const int max_passes = std::max(1, options_.sweep_passes);
  for (int pass = 0; pass < max_passes; ++pass) {
    int accepted = 0;
    for (const Pair& pr : pairs) {
      const int64_t caps[2] = {cap[pr.a], cap[pr.b]};
      int64_t delta = 0;
      if (sweepPair(level, pr.a, pr.b, pr.x_pair, caps, builder, stats, delta)) {
        ++accepted;
        current += delta;
        if (options_.sweep_audit) {
          const int64_t full = hpwl();
          if (full != current) {
            if (stats.sweep_audit_errors == 0) {
              logger_->warn(utl::MCP,
                            61,
                            "Level {}: boundary sweep audit: incremental HPWL "
                            "{} differs from the recomputed {} after a move "
                            "between regions {} and {}.",
                            level,
                            current,
                            full,
                            pr.a,
                            pr.b);
            }
            ++stats.sweep_audit_errors;
            current = full;
          }
        }
      }
    }
    ++stats.sweep_passes;
    stats.sweep_curve.push_back(static_cast<double>(current));
    debugPrint(logger_,
               utl::MCP,
               "sweep",
               1,
               "Level {} pass {}: {} of {} pairs accepted, HPWL {:.6g}",
               level,
               pass + 1,
               accepted,
               pairs.size(),
               static_cast<double>(current));
    if (accepted == 0) {
      break;
    }
  }
  stats.sweep_hpwl_after = static_cast<double>(current);
  stats.sweep_seconds = sweepSeconds(start);
  logger_->info(utl::MCP,
                60,
                "Level {:2d}: boundary sweeps over {} regions ({} pairs): {} "
                "passes, {} accepted of {} calls ({} unchanged, {} rejected, "
                "{} infeasible), {} cells moved, HPWL at centers {:.4g} -> "
                "{:.4g} ({:+.2f}%), {:.1f} s",
                level,
                regions.size(),
                pairs.size(),
                stats.sweep_passes,
                stats.sweep_accepted,
                stats.sweep_calls,
                stats.sweep_noop,
                stats.sweep_rejected,
                stats.sweep_infeasible,
                stats.sweep_moved,
                static_cast<double>(before),
                static_cast<double>(current),
                before > 0 ? 100.0 * (current - before) / before : 0.0,
                stats.sweep_seconds);
}

bool Decomposer::sweepPair(int level,
                           int a,
                           int b,
                           bool x_pair,
                           const int64_t cap[2],
                           SubproblemBuilder& builder,
                           LevelStats& stats,
                           int64_t& exact_delta)
{
  exact_delta = 0;
  Region* side[2] = {&tree_.region(a), &tree_.region(b)};

  // The two-bin problem as a synthetic region split into the two boxes.
  // A cell that does not fit into the other box stays where it is (it is
  // seen as an external pin at its location) and its area is taken from
  // its side's capacity.
  Region prob;
  prob.id = a;
  prob.level = level;
  prob.box = side[0]->box;
  prob.box.merge(side[1]->box);
  int64_t locked[2] = {0, 0};
  std::vector<int> locked_cells[2];
  int64_t total = 0;
  std::vector<int> origin;  // side of every local cell
  for (int q = 0; q < 2; ++q) {
    const Region& other = *side[1 - q];
    for (int v : side[q]->cells) {
      const Vertex& vert = netlist_.vertex(v);
      if (vert.width > other.box.dx() || vert.height > other.box.dy()) {
        locked[q] += vert.area;
        locked_cells[q].push_back(v);
        continue;
      }
      prob.cells.push_back(v);
      origin.push_back(q);
      total += vert.area;
    }
  }
  if (prob.cells.size() < 2) {
    return false;
  }
  SplitPlan plan;
  plan.k = 2;
  plan.vertical_cut = x_pair;
  plan.boxes = {side[0]->box, side[1]->box};
  plan.usable = {side[0]->usable_area, side[1]->usable_area};
  plan.upper.resize(2);
  plan.lower.resize(2);
  plan.capacity.resize(2);
  for (int q = 0; q < 2; ++q) {
    plan.upper[q] = static_cast<double>(std::max<int64_t>(0, cap[q] - locked[q]));
    plan.capacity[q] = plan.upper[q];
  }
  if (plan.upper[0] + plan.upper[1] < static_cast<double>(total)) {
    ++stats.sweep_infeasible;
    return false;
  }
  for (int q = 0; q < 2; ++q) {
    plan.lower[q] = std::max(0.0, total - plan.upper[1 - q]);
  }

  Job job;
  job.region = a;
  job.plan = plan;
  builder.build(prob,
                plan,
                loc_,
                Objective::kHpwl,
                regionSeed(a) ^ (0x9e3779b9u * static_cast<unsigned>(b)),
                1,
                job.sub,
                job.sub_stats);
  if (job.sub.hyperedges.empty()) {
    return false;
  }
  if (options_.sweep_warm) {
    job.sub.initial_parts = origin;
  }
  // The flat FM engine by default (the main engine with sweep_multilevel
  // when it understands the vectors), then the exact search for small
  // pairs -- Decomposer::solve without its balance sweep, whose
  // fractions would override the capacities.
  Engine* saved_engine = engine_;
  Engine* saved_top = top_engine_;
  if (!options_.sweep_multilevel || !engine_->supportsVectors()) {
    engine_ = fallback_ != nullptr ? fallback_ : engine_;
  }
  top_engine_ = nullptr;
  ++stats.sweep_calls;
  const double model_before = partitionCost(job.sub, origin);
  runEngine(job, level);
  engine_ = saved_engine;
  top_engine_ = saved_top;
  if (job.parts.size() < prob.cells.size()) {
    return false;
  }
  if (options_.exact_cells > 0
      && job.sub.num_movable <= options_.exact_cells) {
    ExactStats exact;
    exactPartition(job.sub, job.parts, kSweepExactBudget, exact);
  }

  // Feasibility (the engines honor the bounds approximately) and the
  // moved cells.
  int64_t area[2] = {0, 0};
  std::vector<int> moved;
  for (size_t i = 0; i < prob.cells.size(); ++i) {
    const int q = job.parts[i];
    if (q < 0 || q > 1) {
      return false;
    }
    area[q] += netlist_.vertex(prob.cells[i]).area;
    if (q != origin[i]) {
      moved.push_back(static_cast<int>(i));
    }
  }
  // No move, or no predicted gain (the engine shuffles cells without
  // nets in the problem for its balance; the smoke run saw every such
  // result fail the exact test): nothing to evaluate.
  const double model_delta
      = (partitionCost(job.sub, job.parts) - model_before) * area_.rowHeight();
  if (moved.empty() || model_delta >= 0.0) {
    ++stats.sweep_noop;
    return false;
  }
  for (int q = 0; q < 2; ++q) {
    if (static_cast<double>(area[q]) > plan.upper[q] * 1.001) {
      ++stats.sweep_infeasible;
      return false;
    }
  }

  // Exact change: the nets touching the moved cells, before and after.
  ++stamp_;
  std::vector<int> nets;
  for (int i : moved) {
    for (int e : netlist_.vertexNets(prob.cells[i])) {
      if (net_stamp_[e] != stamp_) {
        net_stamp_[e] = stamp_;
        nets.push_back(e);
      }
    }
  }
  int64_t h_before = 0;
  for (int e : nets) {
    h_before += netHpwl(e);
  }
  std::vector<odb::Point> saved;
  saved.reserve(moved.size());
  const bool seed_loc = seedTerminals();
  for (int i : moved) {
    const int v = prob.cells[i];
    saved.push_back(loc_[v]);
    const Region& to = *side[job.parts[i]];
    loc_[v] = seed_loc ? to.box.closestPtInside(netlist_.vertex(v).loc)
                       : to.center();
  }
  int64_t h_after = 0;
  for (int e : nets) {
    h_after += netHpwl(e);
  }
  const int64_t delta = h_after - h_before;
  if (delta >= 0) {
    ++stats.sweep_rejected;
    for (size_t j = 0; j < moved.size(); ++j) {
      loc_[prob.cells[moved[j]]] = saved[j];
    }
    debugPrint(logger_,
               utl::MCP,
               "sweep",
               2,
               "Level {}: pair {} / {} rejected ({} cells, exact {:+d}, model "
               "{:+.0f})",
               level,
               a,
               b,
               moved.size(),
               delta,
               model_delta);
    return false;
  }

  // Move the cells in the tree: rebuild both regions from the result.
  for (int q = 0; q < 2; ++q) {
    Region& r = *side[q];
    r.cells = locked_cells[q];
    r.cell_area = locked[q];
    r.max_cell_width = 0;
    r.max_cell_height = 0;
  }
  for (size_t i = 0; i < prob.cells.size(); ++i) {
    const int v = prob.cells[i];
    Region& r = *side[job.parts[i]];
    r.cells.push_back(v);
    r.cell_area += netlist_.vertex(v).area;
    region_of_[v] = r.id;
  }
  for (int q = 0; q < 2; ++q) {
    Region& r = *side[q];
    for (int v : r.cells) {
      const Vertex& vert = netlist_.vertex(v);
      r.max_cell_width = std::max(r.max_cell_width, vert.width);
      r.max_cell_height = std::max(r.max_cell_height, vert.height);
    }
  }
  ++stats.sweep_accepted;
  stats.sweep_moved += static_cast<int>(moved.size());
  stats.sweep_exact_delta += static_cast<double>(delta);
  stats.sweep_model_delta += model_delta;
  exact_delta = delta;
  return true;
}

}  // namespace mcp
