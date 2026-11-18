// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "Decomposer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ExactPartition.h"
#include "KwayRefine.h"
#include "NetlistModel.h"
#include "Oracle.h"
#include "PlacementArea.h"
#include "Region.h"
#include "RegionTree.h"
#include "observer.h"
#include "utl/Logger.h"

namespace mcp {

namespace {

double secondsSince(std::chrono::steady_clock::time_point start)
{
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
      .count();
}

// Node budget of the exact partitioner per region; with k = 2 the
// default exact_cells (16) is always searched completely.
constexpr int64_t kExactBudget = 200000;

}  // namespace

Decomposer::Decomposer(const NetlistModel& netlist,
                       const PlacementArea& area,
                       RegionTree& tree,
                       const Options& options,
                       Engine* engine,
                       Engine* top_engine,
                       Engine* fallback,
                       Observer* observer,
                       utl::Logger* logger)
    : netlist_(netlist),
      area_(area),
      tree_(tree),
      options_(options),
      engine_(engine),
      top_engine_(top_engine),
      fallback_(fallback),
      observer_(observer),
      logger_(logger)
{
}

unsigned Decomposer::regionSeed(int region_id) const
{
  // Deterministic per-region seed independent of thread scheduling.
  uint64_t h = options_.seed;
  h ^= static_cast<uint64_t>(region_id + 1) * 0x9e3779b97f4a7c15ULL;
  h ^= h >> 29;
  h *= 0xbf58476d1ce4e5b9ULL;
  h ^= h >> 32;
  return static_cast<unsigned>(h & 0x7fffffffu);
}

int Decomposer::countCut(const Subproblem& sub, const std::vector<int>& parts)
{
  int cut = 0;
  for (const auto& edge : sub.hyperedges) {
    int first = -1;
    for (int v : edge) {
      const int p = v < sub.num_movable ? parts[v] : sub.fixed_part[v];
      if (first < 0) {
        first = p;
      } else if (p != first) {
        ++cut;
        break;
      }
    }
  }
  return cut;
}

// Last resort when every engine failed: fill the children in order of
// decreasing cell weight, largest first, never exceeding an upper bound
// unless all children are full.
void Decomposer::greedySplit(const Subproblem& sub,
                             std::vector<int>& parts) const
{
  parts.assign(sub.num_movable, 0);
  std::vector<int> order(sub.num_movable);
  for (int i = 0; i < sub.num_movable; ++i) {
    order[i] = i;
  }
  std::sort(order.begin(), order.end(), [&](int a, int b) {
    if (sub.vertex_weights[a] != sub.vertex_weights[b]) {
      return sub.vertex_weights[a] > sub.vertex_weights[b];
    }
    return a < b;
  });
  std::vector<double> load(sub.k, 0.0);
  for (int v : order) {
    int best = -1;
    double best_slack = -1.0;
    for (int q = 0; q < sub.k; ++q) {
      const double slack = sub.upper[q] - load[q] - sub.vertex_weights[v];
      if (slack > best_slack) {
        best_slack = slack;
        best = q;
      }
    }
    parts[v] = best;
    load[best] += sub.vertex_weights[v];
  }
}

int Decomposer::leafPadLeft() const
{
  const int sites
      = options_.leaf_pad >= 0 ? options_.leaf_pad : options_.pad_left;
  return std::max(0, sites) * area_.siteWidth();
}

int Decomposer::leafPadRight() const
{
  const int sites
      = options_.leaf_pad >= 0 ? options_.leaf_pad : options_.pad_right;
  return std::max(0, sites) * area_.siteWidth();
}

void Decomposer::runJob(Job& job,
                        SubproblemBuilder& builder,
                        int level,
                        int engine_threads)
{
  const Region& region = tree_.region(job.region);
  Objective objective
      = (options_.fine_level >= 0 && level >= options_.fine_level)
            ? options_.fine_objective
            : options_.objective;
  const bool seeded = seededAt(level);
  builder.setDisplacement(seeded ? options_.displacement_weight : 0.0);
  // Position model of the local pins: the oracle's positions when there
  // are any, else the seed.
  const bool oracle_model = !oracle_pos_.empty();
  builder.setModel(oracle_model ? &oracle_pos_ : nullptr);
  builder.setSeedSpread((seeded || oracle_model) ? options_.seed_spread : 1.0);
  if (seeded && options_.displacement_weight > 0.0
      && objective == Objective::kCutNet) {
    // The displacement cost is a vertex vector and needs the vector
    // machinery; conn is the cut indicator for k = 2.
    objective = Objective::kConn;
  }
  // Geometric assignment of the seed placement against the planned
  // boxes.  commit() then shifts the lines only within the gap between
  // the two sides' seeds (keep_seeds), so the children contain the
  // seeds of their cells; the whitespace follows the seed instead of
  // the capacity policy.
  AssignmentResult result;
  if (seeded && options_.assignment != AssignmentMode::kNone) {
    // With a netlist term the costs depend on where the other cells
    // are: Gauss-Seidel rounds, the first with every local cell at its
    // seed in the region, the following at their assigned children.
    const double net_weight = options_.assignment_net_weight;
    const int rounds = net_weight > 0.0 ? std::max(1, options_.assignment_rounds) : 1;
    AssignmentProblem problem;
    for (int round = 0; round < rounds; ++round) {
      const std::vector<int>* previous = round > 0 ? &result.part : nullptr;
      builder.buildAssignment(region, job.plan, loc_, net_weight, previous, problem);
      AssignmentResult next;
      const auto transport_start = std::chrono::steady_clock::now();
      solveTransport(problem, next);
      job.transport_seconds
          += std::chrono::duration<double>(std::chrono::steady_clock::now()
                                           - transport_start)
                 .count();
      if (round > 0 && next.part == result.part) {
        break;
      }
      result = std::move(next);
    }
    // American-map diagnostic: the min-cut partition of the same region
    // under the planned balance, decided without the assignment, and
    // the disagreement between the two.
    if (amDiagnoseAt(level)) {
      Job mc;
      mc.region = job.region;
      mc.plan = job.plan;
      builder.build(region,
                    mc.plan,
                    loc_,
                    objective,
                    regionSeed(job.region),
                    engine_threads,
                    mc.sub,
                    mc.sub_stats);
      mc.sub.level = level;
      solve(mc, level);
      amCompare(job, result.part, mc.parts);
      if (job.am_ran) {
        // Weighted cut (not the objective: the vector objectives would
        // report their own cost) of both partitions on one subproblem.
        Subproblem& cut_sub = mc.sub;
        auto cut_of = [&](const std::vector<int>& parts) {
          const auto vectors = cut_sub.vectors;
          cut_sub.vectors = nullptr;
          const double c = partitionCost(cut_sub, parts);
          cut_sub.vectors = vectors;
          return c;
        };
        job.am_cut_mincut = cut_of(mc.parts);
        job.am_cut_transport = cut_of(result.part);
      }
      if (options_.am_commit_mincut) {
        // "Min-cut decides, American map watched": commit the min-cut
        // with the planned bounds; the seed enters only through the
        // terminals.
        job.sub = std::move(mc.sub);
        job.sub_stats = mc.sub_stats;
        job.parts = std::move(mc.parts);
        job.failed = mc.failed;
        job.exact_run = mc.exact_run;
        job.exact_improved = mc.exact_improved;
        job.exact_incomplete = mc.exact_incomplete;
        job.assignment_split = result.split_cells;
        job.assignment_repairs = result.repair_moves;
        job.assignment_infeasible = !result.feasible;
        job.cut_nets
            = countCut(job.sub, job.parts) + job.sub_stats.nets_inessential;
        job.assignment_dhpwl
            = builder.assignmentDeltaHpwl(region, job.plan, loc_, job.parts);
        return;
      }
    }
    // The refinement starts from the assignment: widen the balance
    // bounds to it so that the seed's distribution is not "repaired".
    std::vector<double> assigned(job.plan.k, 0.0);
    for (size_t i = 0; i < result.part.size(); ++i) {
      assigned[result.part[i]]
          += static_cast<double>(netlist_.vertex(region.cells[i]).area);
    }
    for (int q = 0; q < job.plan.k; ++q) {
      job.plan.upper[q] = std::max(job.plan.upper[q], assigned[q]);
      job.plan.lower[q] = std::min(job.plan.lower[q], assigned[q]);
    }
    job.assignment_split = result.split_cells;
    job.assignment_repairs = result.repair_moves;
    job.assignment_infeasible = !result.feasible;
  }

  builder.build(region,
                job.plan,
                loc_,
                objective,
                regionSeed(job.region),
                engine_threads,
                job.sub,
                job.sub_stats);
  job.sub.level = level;

  const bool seeded_split = !result.part.empty();
  if (seeded_split) {
    if (options_.assignment == AssignmentMode::kTransportOnly) {
      job.parts = std::move(result.part);
      job.cut_nets
          = countCut(job.sub, job.parts) + job.sub_stats.nets_inessential;
      job.assignment_dhpwl
          = builder.assignmentDeltaHpwl(region, job.plan, loc_, job.parts);
      return;
    }
    job.sub.initial_parts = std::move(result.part);
  }

  const double cost = solve(job, level);

  // E24 sparsest cut: partition the other direction too and keep the
  // one with fewer cut nets per unit length of the cut line.  The
  // inessential nets (external pins on both sides of the planned line)
  // cross the line whatever the partition and count with weight 1.
  if (job.has_alt_plan && !seeded_split) {
    Job alt;
    alt.region = job.region;
    alt.plan = job.alt_plan;
    builder.build(region,
                  alt.plan,
                  loc_,
                  objective,
                  regionSeed(job.region),
                  engine_threads,
                  alt.sub,
                  alt.sub_stats);
    alt.sub.level = level;
    const double alt_cost = solve(alt, level);
    auto line_length = [&](const SplitPlan& plan) {
      return static_cast<double>(plan.vertical_cut ? region.box.dy()
                                                    : region.box.dx());
    };
    const double density
        = (cost + job.sub_stats.nets_inessential) / line_length(job.plan);
    const double alt_density = (alt_cost + alt.sub_stats.nets_inessential)
                               / line_length(alt.plan);
    job.direction_tried = true;
    debugPrint(logger_,
               utl::MCP,
               "direction",
               1,
               "Region {}: {} cut {:.1f}+{} over {} vs {} cut {:.1f}+{} over {}",
               job.region,
               job.plan.vertical_cut ? "vertical" : "horizontal",
               cost,
               job.sub_stats.nets_inessential,
               line_length(job.plan),
               alt.plan.vertical_cut ? "vertical" : "horizontal",
               alt_cost,
               alt.sub_stats.nets_inessential,
               line_length(alt.plan));
    if (alt_density < density) {
      job.direction_flipped = true;
      job.plan = std::move(alt.plan);
      job.sub = std::move(alt.sub);
      job.sub_stats = alt.sub_stats;
      job.parts = std::move(alt.parts);
      job.failed = alt.failed;
      job.exact_run = alt.exact_run;
      job.exact_improved = alt.exact_improved;
      job.exact_incomplete = alt.exact_incomplete;
      job.balance_swept = alt.balance_swept;
      job.balance_offcenter = alt.balance_offcenter;
      if (!job.mass.empty()) {
        job.mass = regionMass(region, job.plan);
      }
    }
  }

  job.cut_nets = countCut(job.sub, job.parts) + job.sub_stats.nets_inessential;
  if (seeded_split) {
    job.assignment_dhpwl
        = builder.assignmentDeltaHpwl(region, job.plan, loc_, job.parts);
  }
}

void Decomposer::amCompare(Job& job,
                           const std::vector<int>& am_parts,
                           const std::vector<int>& mc_parts) const
{
  const Region& region = tree_.region(job.region);
  const SplitPlan& plan = job.plan;
  const int n = static_cast<int>(region.cells.size());
  if (static_cast<int>(am_parts.size()) != n
      || static_cast<int>(mc_parts.size()) != n) {
    return;
  }
  job.am_ran = true;
  std::vector<odb::Point> centers;
  centers.reserve(plan.k);
  for (const odb::Rect& box : plan.boxes) {
    centers.push_back(box.center());
  }
  // Disagreement and displacement; local id of every region cell.
  std::unordered_map<int, int> local;
  local.reserve(static_cast<size_t>(n) * 2);
  for (int i = 0; i < n; ++i) {
    const int v = region.cells[i];
    local.emplace(v, i);
    const Vertex& vert = netlist_.vertex(v);
    const double area = static_cast<double>(vert.area);
    if (am_parts[i] != mc_parts[i]) {
      ++job.am_disagree_cells;
      job.am_disagree_area += area;
    }
    job.am_disp_transport
        += area
           * static_cast<double>(SubproblemBuilder::boxDistance(
               plan.boxes[am_parts[i]], vert.loc));
    job.am_disp_mincut
        += area
           * static_cast<double>(SubproblemBuilder::boxDistance(
               plan.boxes[mc_parts[i]], vert.loc));
  }
  // Surrogate HPWL of every net touching the region under each
  // partition: local cells at the center of their child, every other
  // pin at its current location.
  std::unordered_set<int> seen;
  for (int i = 0; i < n; ++i) {
    for (int e : netlist_.vertexNets(region.cells[i])) {
      if (!seen.insert(e).second) {
        continue;
      }
      const auto verts = netlist_.netVertices(e);
      const auto offsets = netlist_.netPinOffsets(e);
      if (verts.size() < 2) {
        continue;
      }
      int ax_lo = std::numeric_limits<int>::max(), ax_hi = std::numeric_limits<int>::min();
      int ay_lo = ax_lo, ay_hi = ax_hi;
      int mx_lo = ax_lo, mx_hi = ax_hi, my_lo = ax_lo, my_hi = ax_hi;
      for (size_t j = 0; j < verts.size(); ++j) {
        const int v = verts[j];
        const auto it = local.find(v);
        odb::Point a = loc_[v];
        odb::Point m = a;
        if (it != local.end()) {
          a = centers[am_parts[it->second]];
          m = centers[mc_parts[it->second]];
        }
        const int ax = a.x() + offsets[j].x(), ay = a.y() + offsets[j].y();
        const int mx = m.x() + offsets[j].x(), my = m.y() + offsets[j].y();
        ax_lo = std::min(ax_lo, ax);
        ax_hi = std::max(ax_hi, ax);
        ay_lo = std::min(ay_lo, ay);
        ay_hi = std::max(ay_hi, ay);
        mx_lo = std::min(mx_lo, mx);
        mx_hi = std::max(mx_hi, mx);
        my_lo = std::min(my_lo, my);
        my_hi = std::max(my_hi, my);
      }
      const double w = netlist_.netWeight(e);
      job.am_hpwl_transport
          += w * (static_cast<double>(ax_hi - ax_lo) + (ay_hi - ay_lo));
      job.am_hpwl_mincut
          += w * (static_cast<double>(mx_hi - mx_lo) + (my_hi - my_lo));
    }
  }
}

bool Decomposer::runEngine(Job& job, int level)
{
  Engine* engine = (top_engine_ != nullptr && level < options_.top_levels)
                       ? top_engine_
                       : engine_;
  if (fallback_ != nullptr
      && ((job.sub.vectors != nullptr && !engine->supportsVectors())
          || (!job.sub.initial_parts.empty()
              && !engine->supportsInitialParts()))) {
    engine = fallback_;
  }
  job.parts.clear();
  bool ok = engine->partition(job.sub, job.parts);
  if (!ok) {
    job.failed = true;
    if (fallback_ != nullptr && fallback_ != engine) {
      ok = fallback_->partition(job.sub, job.parts);
    }
  }
  if (!ok) {
    greedySplit(job.sub, job.parts);
  }
  // I1 best-of-N: the same engine again with other seeds; the
  // cheapest partition is kept.
  if (ok && options_.engine_runs > 1 && job.sub.initial_parts.empty()) {
    const unsigned seed0 = job.sub.seed;
    double best = partitionCost(job.sub, job.parts);
    std::vector<int> parts;
    for (int r = 1; r < options_.engine_runs; ++r) {
      job.sub.seed = seed0 + static_cast<unsigned>(r) * 7919u;
      parts.clear();
      if (engine->partition(job.sub, parts)) {
        const double cost = partitionCost(job.sub, parts);
        if (cost < best) {
          best = cost;
          job.parts.swap(parts);
        }
      }
    }
    job.sub.seed = seed0;
  }
  return ok;
}

int Decomposer::degradeCut(const Subproblem& sub,
                           std::vector<int>& parts,
                           double target,
                           unsigned seed)
{
  const int n = sub.num_movable;
  const size_t total = sub.vertex_weights.size();
  if (sub.k != 2 || n < 2 || static_cast<int>(parts.size()) < n) {
    return 0;
  }
  auto part_of = [&](int v) { return v < n ? parts[v] : sub.fixed_part[v]; };
  // Pins of every net on each side; nets of every movable vertex.
  const size_t m = sub.hyperedges.size();
  std::vector<int> cnt(2 * m, 0);
  std::vector<std::vector<int>> nets_of(n);
  for (size_t e = 0; e < m; ++e) {
    for (int v : sub.hyperedges[e]) {
      if (v < 0 || static_cast<size_t>(v) >= total) {
        continue;
      }
      ++cnt[2 * e + part_of(v)];
      if (v < n) {
        nets_of[v].push_back(static_cast<int>(e));
      }
    }
  }
  double cost = 0.0;
  for (size_t e = 0; e < m; ++e) {
    if (cnt[2 * e] > 0 && cnt[2 * e + 1] > 0) {
      cost += sub.hyperedge_weights[e];
    }
  }
  // Side weights and the bounds they must keep (the planned bounds,
  // widened to the entry partition when the engine left it outside).
  double w[2] = {0.0, 0.0};
  for (int v = 0; v < n; ++v) {
    w[parts[v]] += sub.vertex_weights[v];
  }
  double lo[2], hi[2];
  for (int q = 0; q < 2; ++q) {
    lo[q] = std::min(static_cast<double>(sub.lower[q]), w[q]);
    hi[q] = std::max(static_cast<double>(sub.upper[q]), w[q]);
  }
  // Moving v from its side to the other: change of the weighted cut.
  auto move_delta = [&](int v) {
    const int from = parts[v];
    const int to = 1 - from;
    double delta = 0.0;
    for (int e : nets_of[v]) {
      const int c_from = cnt[2 * e + from];
      const int c_to = cnt[2 * e + to];
      if (c_from == 1 && c_to > 0) {
        delta -= sub.hyperedge_weights[e];  // becomes uncut
      } else if (c_to == 0) {
        delta += sub.hyperedge_weights[e];  // becomes cut
      }
    }
    return delta;
  };
  auto apply_move = [&](int v) {
    const int from = parts[v];
    for (int e : nets_of[v]) {
      --cnt[2 * e + from];
      ++cnt[2 * e + 1 - from];
    }
    w[from] -= sub.vertex_weights[v];
    w[1 - from] += sub.vertex_weights[v];
    parts[v] = 1 - from;
  };
  std::mt19937 rng(seed);
  std::uniform_int_distribution<int> pick(0, n - 1);
  int swaps = 0;
  const int64_t budget = static_cast<int64_t>(n) * 200 + 2000;
  int64_t attempts = 0;
  while (cost < target && attempts < budget) {
    ++attempts;
    const int a = pick(rng);
    const int b = pick(rng);
    if (parts[a] == parts[b]) {
      continue;
    }
    const int pa = parts[a];
    const double wa = sub.vertex_weights[a];
    const double wb = sub.vertex_weights[b];
    const double new_a = w[pa] - wa + wb;        // side of a afterwards
    const double new_b = w[1 - pa] - wb + wa;    // side of b afterwards
    if (new_a < lo[pa] || new_a > hi[pa] || new_b < lo[1 - pa]
        || new_b > hi[1 - pa]) {
      continue;
    }
    const double da = move_delta(a);
    apply_move(a);
    const double db = move_delta(b);
    if (da + db > 0.0) {
      apply_move(b);
      cost += da + db;
      ++swaps;
    } else {
      apply_move(a);  // undo
    }
  }
  return swaps;
}

double Decomposer::partitionCost(const Subproblem& sub,
                                 const std::vector<int>& parts)
{
  double cost = 0.0;
  if (sub.vectors != nullptr) {
    const int num_masks = sub.vectors->num_masks;
    for (size_t e = 0; e < sub.hyperedges.size(); ++e) {
      int mask = 0;
      for (int v : sub.hyperedges[e]) {
        mask |= 1 << (v < sub.num_movable ? parts[v] : sub.fixed_part[v]);
      }
      cost += sub.vectors->net[e * num_masks + mask];
    }
    return cost;
  }
  for (size_t e = 0; e < sub.hyperedges.size(); ++e) {
    int first = -1;
    for (int v : sub.hyperedges[e]) {
      const int p = v < sub.num_movable ? parts[v] : sub.fixed_part[v];
      if (first < 0) {
        first = p;
      } else if (p != first) {
        cost += sub.hyperedge_weights[e];
        break;
      }
    }
  }
  return cost;
}

void Decomposer::partVolumes(const Subproblem& sub,
                             const std::vector<int>& parts,
                             std::vector<double>& volume)
{
  volume.assign(sub.k, 0.0);
  for (size_t e = 0; e < sub.hyperedges.size(); ++e) {
    for (int v : sub.hyperedges[e]) {
      if (v < sub.num_movable) {
        volume[parts[v]] += sub.hyperedge_weights[e];
      }
    }
  }
}

double Decomposer::solve(Job& job, int level)
{
  Subproblem& sub = job.sub;
  const bool sweep
      = options_.balance_objective != BalanceObjective::kBounds && sub.k == 2
        && sub.initial_parts.empty() && sub.num_movable >= 2
        && (options_.balance_levels < 0 || level < options_.balance_levels)
        && !options_.balance_fractions.empty();
  if (sweep) {
    // The objective chooses the balance: partition at every candidate
    // fraction with a tight tolerance and keep the split with the
    // lowest ratio cut / normalized cut.
    double total = 0.0;
    for (int v = 0; v < sub.num_movable; ++v) {
      total += sub.vertex_weights[v];
    }
    const double tol = std::max(0.0, options_.balance_sweep_tolerance);
    // sub.upper is plan.upper in sites: the ratio is the site area.
    const double site_area
        = sub.upper[0] > 0.0f ? job.plan.upper[0] / sub.upper[0] : 0.0;
    std::vector<int> best_parts;
    std::vector<float> best_upper, best_lower;
    double best_score = std::numeric_limits<double>::infinity();
    double best_fraction = 0.5;
    bool best_failed = false;
    std::vector<double> volume;
    for (double f : options_.balance_fractions) {
      if (f <= 0.0 || f >= 1.0) {
        continue;
      }
      const double share[2] = {total * f, total * (1.0 - f)};
      for (int q = 0; q < 2; ++q) {
        sub.upper[q] = static_cast<float>(share[q] * (1.0 + tol));
        sub.lower[q] = static_cast<float>(share[q] * (1.0 - tol));
      }
      job.failed = false;
      runEngine(job, level);
      const double cost = partitionCost(sub, job.parts);
      double score;
      if (options_.balance_objective == BalanceObjective::kRatio) {
        double w0 = 0.0;
        for (int v = 0; v < sub.num_movable; ++v) {
          if (job.parts[v] == 0) {
            w0 += sub.vertex_weights[v];
          }
        }
        const double a = total > 0.0 ? w0 / total : 0.5;
        score = (a <= 0.0 || a >= 1.0) ? std::numeric_limits<double>::infinity()
                                       : cost / (a * (1.0 - a));
      } else {
        partVolumes(sub, job.parts, volume);
        score = (volume[0] <= 0.0 || volume[1] <= 0.0)
                    ? std::numeric_limits<double>::infinity()
                    : cost * (1.0 / volume[0] + 1.0 / volume[1]);
      }
      if (score < best_score) {
        best_score = score;
        best_parts = job.parts;
        best_upper = sub.upper;
        best_lower = sub.lower;
        best_fraction = f;
        best_failed = job.failed;
      }
    }
    if (!best_parts.empty()) {
      job.parts = std::move(best_parts);
      // The plan's bounds (dbu area; the commit's balance check reads
      // them) follow the chosen fraction.
      if (site_area > 0.0) {
        for (int q = 0; q < 2; ++q) {
          job.plan.upper[q] = best_upper[q] * site_area;
          job.plan.lower[q] = best_lower[q] * site_area;
        }
      }
      sub.upper = best_upper;
      sub.lower = best_lower;
      job.failed = best_failed;
      job.balance_swept = true;
      double nearest = 0.5;
      double nearest_gap = 1.0;
      for (double f : options_.balance_fractions) {
        if (std::abs(f - 0.5) < nearest_gap) {
          nearest_gap = std::abs(f - 0.5);
          nearest = f;
        }
      }
      job.balance_offcenter = best_fraction != nearest;
    } else {
      runEngine(job, level);
    }
  } else {
    runEngine(job, level);
  }
  if (options_.exact_cells > 0 && sub.num_movable <= options_.exact_cells
      && sub.num_movable >= 2) {
    ExactStats exact;
    exactPartition(sub, job.parts, kExactBudget, exact);
    job.exact_run = true;
    job.exact_improved = exact.improved;
    job.exact_incomplete = !exact.complete;
  }
  if (options_.cut_degrade > 0.0 && sub.k == 2 && sub.num_movable >= 2
      && sub.initial_parts.empty() && sub.vectors == nullptr) {
    const double before = partitionCost(sub, job.parts);
    const double target = before * (1.0 + options_.cut_degrade);
    job.degrade_ran = true;
    job.degrade_cut_before = before;
    job.degrade_swaps = degradeCut(
        sub, job.parts, target, regionSeed(job.region) ^ 0x5bd1e995u);
    job.degrade_cut_after = partitionCost(sub, job.parts);
  }
  return partitionCost(sub, job.parts);
}

void Decomposer::run(int root_id, RunReport& report)
{
  const auto run_start = std::chrono::steady_clock::now();

  // Initial locations: fixed vertices at their database location,
  // movable cells at the root center (or at their seed, clamped into
  // the root, with seed_terminals).
  loc_.resize(netlist_.numVertices());
  const Region& root_region = tree_.region(root_id);
  const odb::Point root_center = root_region.center();
  for (int v = 0; v < netlist_.numVertices(); ++v) {
    const Vertex& vert = netlist_.vertex(v);
    if (vert.fixed || vert.bterm) {
      loc_[v] = vert.loc;
    } else if (seedTerminals()) {
      loc_[v] = root_region.box.closestPtInside(vert.loc);
    } else {
      loc_[v] = root_center;
    }
  }
  region_of_.assign(netlist_.numVertices(), -1);
  for (int v : tree_.region(root_id).cells) {
    region_of_[v] = root_id;
  }
  net_stamp_.assign(netlist_.numNets(), 0);

  // Routing-demand surrogate for the congestion/mixed policies, rebuilt
  // after every wave from the current locations.
  const Capacity& capacity = tree_.capacity();
  std::unique_ptr<DemandMap> demand;
  if (capacity.needsDemand()) {
    demand = std::make_unique<DemandMap>(netlist_, area_.core(), 128);
  }
  demand_ = demand.get();
  std::unique_ptr<QpOracle> oracle;
  if (options_.oracle == OracleKind::kQp) {
    oracle = std::make_unique<QpOracle>(netlist_, logger_);
  }
  oracle_ = oracle.get();
  oracle_pos_.clear();

  const int num_threads = std::max(1, options_.threads);
  std::vector<SubproblemBuilder> builders;
  builders.reserve(num_threads);
  for (int t = 0; t < num_threads; ++t) {
    builders.emplace_back(
        netlist_,
        area_,
        options_.terminals,
        options_.terminal_band,
        options_.seed_placement ? options_.displacement_weight : 0.0);
  }

  std::vector<int> current{root_id};
  std::vector<int> leaves;
  int level = 0;
  while (!current.empty()) {
    const auto level_start = std::chrono::steady_clock::now();
    LevelStats stats;
    stats.level = level;

    // Plan every region; end cases and unsplittable regions become
    // leaves.
    std::vector<Job> jobs;
    for (int id : current) {
      const Region& region = tree_.region(id);
      if (tree_.isEndCase(region)) {
        leaves.push_back(id);
        ++stats.end_cases;
        continue;
      }
      Job job;
      job.region = id;
      const int k = tree_.chooseK(region, options_.k);
      if (!tree_.planSplit(region, k, job.plan)) {
        leaves.push_back(id);
        ++stats.end_cases;
        continue;
      }
      if (options_.cut_direction == CutDirection::kSparsest && job.plan.k == 2
          && !seededAt(level)) {
        job.has_alt_plan = tree_.planSplit(
            region, 2, job.alt_plan, job.plan.vertical_cut ? 0 : 1);
      }
      jobs.push_back(std::move(job));
    }

    // Partition the regions of the level in waves.  Within a wave jobs
    // are independent (they read the tree and loc_ only); after a wave
    // its results are committed and loc_ is updated, so the regions of
    // the next wave see their neighbors' decisions instead of the
    // neighbors' region centers (which lie on this level's cut lines).
    // Two waves color the regions like a checkerboard.
    const bool keep_seeds = seededAt(level)
                            && options_.assignment != AssignmentMode::kNone
                            && !amCommitMincutAt(level);
    const int num_waves = std::max(1, options_.waves);
    std::vector<int> wave_of(jobs.size(), 0);
    if (num_waves > 1) {
      const odb::Rect& root_box = tree_.region(root_id).box;
      for (size_t i = 0; i < jobs.size(); ++i) {
        const Region& region = tree_.region(jobs[i].region);
        const int64_t col = (region.box.xMin() - root_box.xMin())
                            / std::max(1, region.box.dx());
        const int64_t row = (region.box.yMin() - root_box.yMin())
                            / std::max(1, region.box.dy());
        wave_of[i] = static_cast<int>((col + row) % num_waves);
      }
    }

    std::vector<int> next_level;
    std::vector<odb::Rect> boxes;
    for (int wave = 0; wave < num_waves; ++wave) {
      std::vector<size_t> wave_jobs;
      for (size_t i = 0; i < jobs.size(); ++i) {
        if (wave_of[i] == wave) {
          wave_jobs.push_back(i);
        }
      }
      if (wave_jobs.empty()) {
        continue;
      }
      // Inputs of the adaptive policies, from the locations the wave is
      // partitioned with (loc_ changes only when a wave is committed).
      if (demand) {
        demand->build(loc_);
      }
      if (capacity.needsPull()) {
        for (size_t i : wave_jobs) {
          jobs[i].pull = regionPull(tree_.region(jobs[i].region));
        }
      }
      if (capacity.needsMass()) {
        // The balance of the split from the analytical mass (ACG), in
        // place of the whitespace-derived tolerance.
        for (size_t i : wave_jobs) {
          Job& job = jobs[i];
          const Region& region = tree_.region(job.region);
          job.mass = regionMass(region, job.plan);
          std::vector<ChildHint> hints;
          childHints(job.plan, job.pull, job.mass, nullptr, hints);
          std::vector<double> usable(job.plan.k);
          for (int q = 0; q < job.plan.k; ++q) {
            usable[q] = static_cast<double>(job.plan.usable[q]);
          }
          capacity.massBounds(static_cast<double>(region.cell_area),
                              static_cast<double>(region.usable_area),
                              usable,
                              hints,
                              job.plan.upper,
                              job.plan.lower);
        }
      }
      if (num_threads == 1 || wave_jobs.size() == 1) {
        // A single job may use all threads inside the engine.
        const int engine_threads = wave_jobs.size() == 1 ? num_threads : 1;
        for (size_t i : wave_jobs) {
          runJob(jobs[i], builders[0], level, engine_threads);
        }
      } else {
        std::atomic<size_t> next{0};
        std::vector<std::thread> workers;
        workers.reserve(num_threads);
        for (int t = 0; t < num_threads; ++t) {
          workers.emplace_back([&, t]() {
            for (;;) {
              const size_t i = next.fetch_add(1);
              if (i >= wave_jobs.size()) {
                break;
              }
              runJob(jobs[wave_jobs[i]], builders[t], level, 1);
            }
          });
        }
        for (std::thread& worker : workers) {
          worker.join();
        }
      }

      // Commit sequentially in region order (deterministic ids).
      std::vector<ChildHint> hints;
      for (size_t i : wave_jobs) {
        Job& job = jobs[i];
        childHints(job.plan, job.pull, job.mass, demand.get(), hints);
        const std::vector<int> children
            = tree_.commit(job.region, job.plan, job.parts, hints, keep_seeds);
        for (size_t q = 0; q < children.size(); ++q) {
          const int c = children[q];
          const Region& child = tree_.region(c);
          if (child.cell_area > job.plan.upper[q] * 1.001
              || child.cell_area < job.plan.lower[q] * 0.999) {
            ++stats.balance_violations;
            debugPrint(logger_,
                       utl::MCP,
                       "balance",
                       1,
                       "Region {} child {}: cell area {:.0f} outside "
                       "[{:.0f}, {:.0f}] (usable {})",
                       job.region,
                       q,
                       static_cast<double>(child.cell_area),
                       job.plan.lower[q],
                       job.plan.upper[q],
                       child.usable_area);
          }
          const odb::Point center = child.center();
          const bool seed_loc = seedTerminals();
          for (int v : child.cells) {
            loc_[v] = seed_loc
                          ? child.box.closestPtInside(netlist_.vertex(v).loc)
                          : center;
            region_of_[v] = c;
            if (keep_seeds) {
              const int64_t d = SubproblemBuilder::boxDistance(
                  child.box, netlist_.vertex(v).loc);
              if (d > 0) {
                ++stats.seed_outside;
                stats.seed_distance += static_cast<double>(d);
              }
            }
          }
          if (child.box == tree_.region(job.region).box) {
            // No progress is possible from a child that spans its
            // parent (a cut line at the edge); it is a leaf.
            leaves.push_back(c);
            ++stats.end_cases;
            if (!warned_no_progress_) {
              logger_->warn(utl::MCP,
                            28,
                            "Region {} spans its parent {}; treating it as "
                            "an end case.",
                            c,
                            job.region);
              warned_no_progress_ = true;
            }
            continue;
          }
          next_level.push_back(c);
          boxes.push_back(child.box);
        }
        ++stats.regions;
        stats.cells += static_cast<int>(job.sub.num_movable);
        stats.nets += job.sub_stats.nets;
        stats.cut_nets += job.cut_nets;
        stats.inessential += job.sub_stats.nets_inessential;
        stats.engine_failures += job.failed ? 1 : 0;
        stats.assignment_split += job.assignment_split;
        stats.assignment_repairs += job.assignment_repairs;
        stats.assignment_dhpwl += job.assignment_dhpwl;
        stats.assignment_infeasible += job.assignment_infeasible ? 1 : 0;
        stats.transport_seconds += job.transport_seconds;
        if (job.am_ran) {
          ++stats.am_regions;
          stats.am_cells += static_cast<int>(job.sub.num_movable);
          stats.am_area += static_cast<double>(tree_.region(job.region).cell_area);
          stats.am_disagree_cells += job.am_disagree_cells;
          stats.am_disagree_area += job.am_disagree_area;
          stats.am_hpwl_mincut += job.am_hpwl_mincut;
          stats.am_hpwl_transport += job.am_hpwl_transport;
          stats.am_disp_mincut += job.am_disp_mincut;
          stats.am_disp_transport += job.am_disp_transport;
          stats.am_cut_mincut += job.am_cut_mincut;
          stats.am_cut_transport += job.am_cut_transport;
        }
        if (job.degrade_ran) {
          ++stats.degrade_regions;
          stats.degrade_cut_before += job.degrade_cut_before;
          stats.degrade_cut_after += job.degrade_cut_after;
          stats.degrade_swaps += job.degrade_swaps;
        }
        stats.exact_regions += job.exact_run ? 1 : 0;
        stats.exact_improved += job.exact_improved ? 1 : 0;
        stats.exact_incomplete += job.exact_incomplete ? 1 : 0;
        stats.balance_sweeps += job.balance_swept ? 1 : 0;
        stats.balance_offcenter += job.balance_offcenter ? 1 : 0;
        stats.direction_choices += job.direction_tried ? 1 : 0;
        stats.direction_flips += job.direction_flipped ? 1 : 0;
      }
    }
    // X1b: whole-level k-way refinement of the committed assignment.
    // kway_refine_levels names the level of the regions refined, i.e.
    // the children just created (level + 1): {2 4} refines the 4 and
    // the 16 regions of a bisection tree.
    if (!next_level.empty()) {
      const auto listed = [&](const std::vector<int>& levels) {
        return std::find(levels.begin(), levels.end(), level + 1)
               != levels.end();
      };
      if (listed(options_.kway_construct_levels)) {
        kwayRefine(level + 1, next_level, /* from_scratch */ true, stats);
      }
      if (listed(options_.kway_refine_levels)) {
        kwayRefine(level + 1, next_level, /* from_scratch */ false, stats);
      }
      // X2: reversible boundary sweeps over the committed level.
      if (listed(options_.sweep_levels)) {
        boundarySweep(level + 1, next_level, builders[0], stats);
      }
    }
    // Analytical oracle: positions of every movable cell inside the
    // regions decided so far, for the next level's splits.
    if (oracle_ != nullptr && level >= options_.oracle_min_level
        && !next_level.empty()) {
      OracleStats ostats;
      std::vector<odb::Point> pos, clamped;
      const std::vector<odb::Point>& init
          = oracle_pos_.empty() ? loc_ : oracle_pos_;
      oracle_->solve(init,
                     region_of_,
                     tree_,
                     options_.oracle_anchor,
                     options_.oracle_anchor_util,
                     options_.oracle_rounds,
                     pos,
                     clamped,
                     ostats);
      if (options_.oracle_terminals) {
        for (int v = 0; v < netlist_.numVertices(); ++v) {
          if (region_of_[v] >= 0) {
            loc_[v] = clamped[v];
          }
        }
      }
      oracle_pos_ = std::move(pos);
      stats.oracle_hpwl = static_cast<double>(ostats.hpwl);
      stats.oracle_seconds = ostats.seconds;
      debugPrint(logger_,
                 utl::MCP,
                 "oracle",
                 1,
                 "Level {}: oracle HPWL {:.3g} dbu in {:.2f} s",
                 level,
                 static_cast<double>(ostats.hpwl),
                 ostats.seconds);
    }
    stats.seconds = secondsSince(level_start);

    if (stats.regions > 0) {
      logger_->info(
          utl::MCP,
          15,
          "Level {:2d}: {:6d} regions {:8d} cells {:8d} nets "
          "{:7d} cut ({:5.1f}%) {:5d} end cases",
          level,
          stats.regions,
          stats.cells,
          stats.nets,
          stats.cut_nets,
          stats.nets + stats.inessential > 0
              ? 100.0 * stats.cut_nets / (stats.nets + stats.inessential)
              : 0.0,
          stats.end_cases);
      // Runtimes are kept out of the regular log so that regression
      // logs are reproducible; they are in the JSON report.
      debugPrint(logger_,
                 utl::MCP,
                 "timing",
                 1,
                 "Level {}: {:.2f} s",
                 level,
                 stats.seconds);
      if (stats.balance_violations > 0) {
        debugPrint(logger_,
                   utl::MCP,
                   "balance",
                   1,
                   "Level {}: {} children outside their capacity bounds.",
                   level,
                   stats.balance_violations);
      }
      if (stats.engine_failures > 0) {
        logger_->warn(utl::MCP,
                      22,
                      "Level {}: {} engine failures (fallback used).",
                      level,
                      stats.engine_failures);
      }
      if (stats.assignment_infeasible > 0) {
        debugPrint(logger_,
                   utl::MCP,
                   "balance",
                   1,
                   "Level {}: seed assignment outside the bounds in {} "
                   "regions.",
                   level,
                   stats.assignment_infeasible);
      }
      report.levels.push_back(stats);
    }
    if (observer_ != nullptr && !boxes.empty()) {
      observer_->levelDone(level, boxes);
    }
    current = std::move(next_level);
    ++level;
  }
  report.decompose_seconds = secondsSince(run_start);
  report.regions = tree_.numRegions();

  // End cases: write the cells of every leaf to the database.
  const auto end_start = std::chrono::steady_clock::now();
  EndCase end_case(netlist_,
                   area_,
                   options_.seed_placement,
                   options_.optimal_cells,
                   options_.optimal_rows,
                   options_.leaf_positions,
                   leafPadLeft(),
                   leafPadRight(),
                   logger_);
  const auto leaf_seed = [&](const Region& leaf) {
    return options_.seed_leaf && seededAt(leaf.level) ? 1 : 0;
  };
  for (int id : leaves) {
    const Region& leaf = tree_.region(id);
    end_case.place(leaf, loc_, report.end_case, leaf_seed(leaf));
  }
  // Feedback passes: every leaf again, with the other leaves at their
  // placed positions, until a pass changes nothing.
  for (int pass = 1; pass < options_.end_case_passes; ++pass) {
    ++report.end_case.refine_passes;
    int changed = 0;
    for (int id : leaves) {
      const Region& leaf = tree_.region(id);
      if (end_case.place(leaf,
                         loc_,
                         report.end_case,
                         leaf_seed(leaf),
                         /* refine */ true)) {
        ++changed;
      }
    }
    debugPrint(logger_,
               utl::MCP,
               "endcase",
               1,
               "End-case pass {}: {} of {} leaves rewritten",
               pass + 1,
               changed,
               leaves.size());
    if (changed == 0) {
      break;
    }
  }
  for (int id : leaves) {
    const Region& leaf = tree_.region(id);
    end_case.diagnose(leaf,
                      loc_,
                      report.end_case,
                      options_.seed_placement && seededAt(leaf.level));
    // (the diagnostic compares the leaf with the seed whenever there is
    // one; seed_leaf only governs what the end case uses)
  }
  report.end_case_seconds = secondsSince(end_start);
  report.overfull_regions = tree_.overfullRegions();
  leafStatistics(leaves, report);

  // Row ironing over the placed cells.
  if (options_.iron_window >= 2 && options_.iron_passes >= 1) {
    report.hpwl_before_ironing = hpwl();
    RowIroning ironing(netlist_,
                       area_,
                       options_.leaf_positions,
                       leafPadLeft(),
                       leafPadRight(),
                       logger_);
    ironing.run(
        options_.iron_window, options_.iron_passes, loc_, report.ironing);
  }
}

int64_t Decomposer::netHpwl(int e) const
{
  const auto verts = netlist_.netVertices(e);
  const auto offsets = netlist_.netPinOffsets(e);
  if (verts.size() < 2) {
    return 0;
  }
  int x_lo = std::numeric_limits<int>::max(),
      x_hi = std::numeric_limits<int>::min();
  int y_lo = x_lo, y_hi = x_hi;
  for (size_t j = 0; j < verts.size(); ++j) {
    const int x = loc_[verts[j]].x() + offsets[j].x();
    const int y = loc_[verts[j]].y() + offsets[j].y();
    x_lo = std::min(x_lo, x);
    x_hi = std::max(x_hi, x);
    y_lo = std::min(y_lo, y);
    y_hi = std::max(y_hi, y);
  }
  return static_cast<int64_t>(x_hi - x_lo) + (y_hi - y_lo);
}

int64_t Decomposer::hpwl() const
{
  // HPWL of the model's nets from the current locations (loc_ holds the
  // centers; pins at their offsets).
  int64_t sum = 0;
  for (int e = 0; e < netlist_.numNets(); ++e) {
    sum += netHpwl(e);
  }
  return sum;
}

void Decomposer::kwayRefine(int level,
                            const std::vector<int>& next_level,
                            bool from_scratch,
                            LevelStats& stats)
{
  KwayRefiner refiner(netlist_, area_, logger_);
  KwayRefineStats kstats;
  kstats.level = level;
  std::vector<int> new_region_of;
  stats.kway_k = static_cast<int>(next_level.size());
  // Where the statistics of this pass go (construction / refinement).
  int& st_moved = from_scratch ? stats.kwayc_moved : stats.kway_moved;
  double& st_before
      = from_scratch ? stats.kwayc_hpwl_before : stats.kway_hpwl_before;
  double& st_after
      = from_scratch ? stats.kwayc_hpwl_after : stats.kway_hpwl_after;
  bool& st_accepted
      = from_scratch ? stats.kwayc_accepted : stats.kway_accepted;
  double& st_seconds
      = from_scratch ? stats.kwayc_seconds : stats.kway_seconds;
  const char* what = from_scratch ? "construction" : "refinement";
  if (!refiner.refine(next_level,
                      tree_,
                      region_of_,
                      loc_,
                      options_.kway_slack,
                      options_.kway_vcycles,
                      options_.threads,
                      options_.seed + 7919u * static_cast<unsigned>(level + 1)
                          + (from_scratch ? 104729u : 0u),
                      from_scratch,
                      new_region_of,
                      kstats)) {
    return;
  }
  st_seconds = kstats.seconds;
  st_moved = kstats.moved;
  const int64_t before = hpwl();
  st_before = static_cast<double>(before);
  st_after = st_before;
  if (kstats.moved == 0) {
    return;
  }

  // Acceptance test with the placer's own estimate: the moved cells at
  // the center of their new region (at their seed clamped into it with
  // seed_terminals), everything else as it is.
  std::vector<std::pair<int, odb::Point>> saved;
  saved.reserve(kstats.moved);
  const bool seed_loc = seedTerminals();
  for (int v = 0; v < netlist_.numVertices(); ++v) {
    if (region_of_[v] < 0 || new_region_of[v] == region_of_[v]) {
      continue;
    }
    saved.emplace_back(v, loc_[v]);
    const Region& to = tree_.region(new_region_of[v]);
    loc_[v] = seed_loc ? to.box.closestPtInside(netlist_.vertex(v).loc)
                       : to.center();
  }
  const int64_t after = hpwl();
  st_after = static_cast<double>(after);
  if (after >= before) {
    for (const auto& [v, p] : saved) {
      loc_[v] = p;
    }
    debugPrint(logger_,
               utl::MCP,
               "kway",
               1,
               "Level {}: k-way {} over {} regions rejected "
               "({} cells, HPWL {:.4g} -> {:.4g}, Steiner {} -> {})",
               level,
               what,
               next_level.size(),
               kstats.moved,
               static_cast<double>(before),
               static_cast<double>(after),
               kstats.steiner_before,
               kstats.steiner_after);
    return;
  }

  // Move the cells in the tree: rebuild the cell lists of the level's
  // regions from the refined assignment.
  std::vector<int> cells;
  for (int id : next_level) {
    Region& region = tree_.region(id);
    cells.insert(cells.end(), region.cells.begin(), region.cells.end());
    region.cells.clear();
    region.cell_area = 0;
    region.max_cell_width = 0;
    region.max_cell_height = 0;
  }
  for (int v : cells) {
    const int r = new_region_of[v];
    Region& region = tree_.region(r);
    const Vertex& vert = netlist_.vertex(v);
    region.cells.push_back(v);
    region.cell_area += vert.area;
    region.max_cell_width = std::max(region.max_cell_width, vert.width);
    region.max_cell_height = std::max(region.max_cell_height, vert.height);
    region_of_[v] = r;
  }
  st_accepted = true;
  logger_->info(utl::MCP,
                58,
                "Level {:2d}: k-way {} over {} regions moved {} "
                "cells, HPWL at centers {:.4g} -> {:.4g} ({:+.2f}%)",
                level,
                what,
                next_level.size(),
                kstats.moved,
                static_cast<double>(before),
                static_cast<double>(after),
                100.0 * (static_cast<double>(after) - before) / before);
}

odb::Point Decomposer::regionPull(const Region& region)
{
  // Mean location of the pins outside the region over the nets that
  // leave it; every net counts once, its external pins share the unit.
  // Pins of movable cells sit at their region centers (loc_), I/O pins
  // and fixed cells at their location.
  ++stamp_;
  double sx = 0.0;
  double sy = 0.0;
  double sw = 0.0;
  for (int v : region.cells) {
    for (int e : netlist_.vertexNets(v)) {
      if (net_stamp_[e] == stamp_) {
        continue;
      }
      net_stamp_[e] = stamp_;
      double ex = 0.0;
      double ey = 0.0;
      int n_ext = 0;
      for (int u : netlist_.netVertices(e)) {
        if (region_of_[u] != region.id) {
          ex += loc_[u].x();
          ey += loc_[u].y();
          ++n_ext;
        }
      }
      if (n_ext > 0) {
        sx += ex / n_ext;
        sy += ey / n_ext;
        sw += 1.0;
      }
    }
  }
  if (sw <= 0.0) {
    return region.center();
  }
  // Clamped to the region: only the direction matters.
  const int px = static_cast<int>(
      std::clamp<double>(sx / sw, region.box.xMin(), region.box.xMax()));
  const int py = static_cast<int>(
      std::clamp<double>(sy / sw, region.box.yMin(), region.box.yMax()));
  return odb::Point(px, py);
}

std::vector<double> Decomposer::regionMass(const Region& region,
                                           const SplitPlan& plan) const
{
  const int k = plan.k;
  std::vector<double> mass(k, 0.0);
  const double S = static_cast<double>(region.cell_area);
  const double U = static_cast<double>(region.usable_area);
  if (S <= 0.0 || U <= 0.0 || region.box.area() <= 0) {
    return mass;
  }
  // Center of mass of the cells.
  double sum_a = 0.0, cx = 0.0, cy = 0.0;
  const bool oracle = !oracle_pos_.empty();
  for (int v : region.cells) {
    const double a = static_cast<double>(netlist_.vertex(v).area);
    const odb::Point p
        = oracle ? region.box.closestPtInside(oracle_pos_[v]) : loc_[v];
    sum_a += a;
    cx += a * p.x();
    cy += a * p.y();
  }
  if (sum_a <= 0.0) {
    return mass;
  }
  cx /= sum_a;
  cy /= sum_a;
  // The rectangle: area S / d scaled from the region box (the usable
  // fraction of the box accounts for obstacles), kept inside the box.
  const double d = tree_.capacity().density(S / U, nullptr);
  const double frac = std::min(1.0, (S / std::max(d, 1e-9)) / U);
  const double scale = std::sqrt(frac);
  const double w = region.box.dx() * scale;
  const double h = region.box.dy() * scale;
  double x0 = std::clamp(cx - w / 2, static_cast<double>(region.box.xMin()),
                         region.box.xMax() - w);
  double y0 = std::clamp(cy - h / 2, static_cast<double>(region.box.yMin()),
                         region.box.yMax() - h);
  for (int q = 0; q < k; ++q) {
    const odb::Rect& b = plan.boxes[q];
    const double ix = std::max(0.0, std::min(x0 + w, static_cast<double>(b.xMax()))
                                        - std::max(x0, static_cast<double>(b.xMin())));
    const double iy = std::max(0.0, std::min(y0 + h, static_cast<double>(b.yMax()))
                                        - std::max(y0, static_cast<double>(b.yMin())));
    mass[q] = ix * iy;
  }
  return mass;
}

void Decomposer::childHints(const SplitPlan& plan,
                            const odb::Point& pull,
                            const std::vector<double>& mass,
                            const DemandMap* demand,
                            std::vector<ChildHint>& hints) const
{
  hints.clear();
  const Capacity& capacity = tree_.capacity();
  if (!capacity.adaptive()) {
    return;
  }
  const int k = plan.k;
  hints.resize(k);

  if (capacity.needsPull()) {
    for (int q = 0; q < k; ++q) {
      const odb::Point c = plan.boxes[q].center();
      const double dx = std::abs(static_cast<double>(c.x() - pull.x()));
      const double dy = std::abs(static_cast<double>(c.y() - pull.y()));
      if (k == 4) {
        hints[q].distance = dx + dy;
      } else {
        hints[q].distance = plan.vertical_cut ? dx : dy;
      }
    }
  }

  if (demand != nullptr) {
    for (int q = 0; q < k; ++q) {
      hints[q].demand = demand->demand(plan.boxes[q]);
    }
  }

  if (capacity.needsMass() && static_cast<int>(mass.size()) == k) {
    for (int q = 0; q < k; ++q) {
      hints[q].mass = mass[q];
      hints[q].box_area = static_cast<double>(plan.boxes[q].area());
    }
  }
}

void Decomposer::leafStatistics(const std::vector<int>& leaves,
                                RunReport& report) const
{
  int64_t occupied = 0;
  int64_t usable_total = 0;
  double sum = 0.0;
  for (int id : leaves) {
    const Region& leaf = tree_.region(id);
    usable_total += leaf.usable_area;
    if (leaf.cells.empty() || leaf.usable_area <= 0) {
      continue;
    }
    const double density = static_cast<double>(leaf.cell_area)
                           / static_cast<double>(leaf.usable_area);
    sum += density;
    report.leaf_density_max = std::max(report.leaf_density_max, density);
    ++report.leaves_with_cells;
    occupied += leaf.usable_area;
  }
  if (report.leaves_with_cells > 0) {
    report.leaf_density_mean = sum / report.leaves_with_cells;
  }
  if (usable_total > 0) {
    report.occupied_area_fraction
        = static_cast<double>(occupied) / static_cast<double>(usable_total);
  }
}

}  // namespace mcp
