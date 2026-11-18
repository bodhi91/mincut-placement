// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

// Level-by-level decomposition: every region of the current level is
// partitioned (in parallel), the children form the next level, and end
// cases are written to the database.

#pragma once

#include <algorithm>
#include <memory>
#include <vector>

#include "Assignment.h"
#include "Capacity.h"
#include "EndCase.h"
#include "Engine.h"
#include "Feedback.h"
#include "Ironing.h"
#include "Region.h"
#include "SubproblemBuilder.h"
#include "mcp/MinCutPlacer.h"
#include "odb/geom.h"

namespace utl {
class Logger;
}

namespace mcp {

class NetlistModel;
class PlacementArea;
class RegionTree;
class Observer;
class QpOracle;

struct LevelStats
{
  int level = 0;
  int regions = 0;      // regions partitioned at this level
  int cells = 0;        // cells in those regions
  int end_cases = 0;    // regions that became leaves at this level
  int nets = 0;         // hyperedges handed to the engines
  int cut_nets = 0;     // cut after partitioning (incl. inessential)
  int inessential = 0;  // nets already spanning >= 2 children
  int engine_failures = 0;
  int balance_violations = 0;  // children outside their capacity bounds
  // Seed assignment (M3): cells split by the fractional optimum, cells
  // moved to restore the bounds, regions whose bounds could not be met.
  int assignment_split = 0;
  int assignment_repairs = 0;
  int assignment_infeasible = 0;
  // Linearized HPWL cost (dbu) of the cells put into a child other than
  // their seed's: the side-of-cut decisions of the level.
  double assignment_dhpwl = 0.0;
  // Oracle solve after this level (Options::oracle): HPWL (dbu) of the
  // clamped oracle positions, and its runtime.
  double oracle_hpwl = 0.0;
  double oracle_seconds = 0.0;
  // Seeded runs: cells whose seed lies outside the child they were put
  // in, and the sum of their distances to it (dbu).
  int seed_outside = 0;
  double seed_distance = 0.0;
  // Wall time of the transportation solves of the level (seeded runs).
  double transport_seconds = 0.0;
  // American-map diagnostic (Options::am_diagnose): regions where both
  // partitions were computed; cells and cell area (dbu^2) the min-cut
  // and the assignment put into different children, out of am_cells /
  // am_area; surrogate HPWL (dbu) of the nets touching those regions
  // with the local cells at the child centers of each partition (nets
  // touching two regions of the level count in both); area-weighted
  // displacement (dbu * dbu^2) from the seed of each partition.
  int am_regions = 0;
  int am_cells = 0;
  int am_disagree_cells = 0;
  double am_area = 0.0;
  double am_disagree_area = 0.0;
  double am_hpwl_mincut = 0.0;
  double am_hpwl_transport = 0.0;
  double am_disp_mincut = 0.0;
  double am_disp_transport = 0.0;
  // I1: weighted cut of the two partitions on the same subproblem
  // (same hypergraph, same planned bounds), summed over am_regions.
  double am_cut_mincut = 0.0;
  double am_cut_transport = 0.0;
  // I1 (Options::cut_degrade): bisections degraded, their weighted cut
  // as the engine left it and after the swaps, swaps accepted.
  int degrade_regions = 0;
  double degrade_cut_before = 0.0;
  double degrade_cut_after = 0.0;
  int degrade_swaps = 0;
  // Exact partitioning (M4): regions it ran on, regions where it beat
  // the engine, regions where it hit its node budget.
  int exact_regions = 0;
  int exact_improved = 0;
  int exact_incomplete = 0;
  // E24: regions whose balance was chosen by the objective (ratio /
  // ncut sweep) and how many of them chose a fraction other than the
  // one closest to 1/2; regions where both cut directions were tried
  // (sparsest) and how many chose the shorter-side cut, i.e. not the
  // default.
  int balance_sweeps = 0;
  int balance_offcenter = 0;
  int direction_choices = 0;
  int direction_flips = 0;
  // X1b: k-way refinement of the level's result (Options::
  // kway_refine_levels): regions it ran over, cells moved, HPWL at the
  // region centers (dbu) before / after, whether the result was kept.
  int kway_k = 0;
  int kway_moved = 0;
  double kway_hpwl_before = 0.0;
  double kway_hpwl_after = 0.0;
  bool kway_accepted = false;
  double kway_seconds = 0.0;
  // X1a: the same for the from-scratch construction
  // (Options::kway_construct_levels), which runs before the refinement.
  int kwayc_moved = 0;
  double kwayc_hpwl_before = 0.0;
  double kwayc_hpwl_after = 0.0;
  bool kwayc_accepted = false;
  double kwayc_seconds = 0.0;
  // X2: boundary sweeps over the level's regions (Options::sweep_levels):
  // adjacent pairs, passes run, solver calls, accepted moves, cells
  // moved, HPWL at the centers (dbu) before / after, HPWL after every
  // pass, the solver model's predicted change summed over the accepted
  // moves against the exact one (both dbu; identical when the model is
  // the surrogate), audit mismatches, runtime.
  int sweep_pairs = 0;
  int sweep_passes = 0;
  int sweep_calls = 0;
  int sweep_accepted = 0;
  int sweep_noop = 0;        // the engine kept the assignment
  int sweep_rejected = 0;    // the exact HPWL did not decrease
  int sweep_infeasible = 0;  // the result broke a capacity
  int sweep_moved = 0;
  double sweep_hpwl_before = 0.0;
  double sweep_hpwl_after = 0.0;
  std::vector<double> sweep_curve;
  double sweep_model_delta = 0.0;
  double sweep_exact_delta = 0.0;
  int sweep_audit_errors = 0;
  double sweep_seconds = 0.0;
  double seconds = 0.0;
};

struct RunReport
{
  std::vector<LevelStats> levels;
  EndCaseStats end_case;
  IroningStats ironing;
  int regions = 0;
  int overfull_regions = 0;
  // Utilization of the leaves holding cells (cell area / usable area):
  // how the capacity policy ended up distributing the whitespace.
  int leaves_with_cells = 0;
  double leaf_density_mean = 0.0;
  double leaf_density_max = 0.0;
  // Fraction of the usable area covered by leaves holding cells.
  double occupied_area_fraction = 0.0;
  double decompose_seconds = 0.0;
  double end_case_seconds = 0.0;
  // HPWL (dbu) after the end cases, before row ironing.
  int64_t hpwl_before_ironing = 0;
};

class Decomposer
{
 public:
  Decomposer(const NetlistModel& netlist,
             const PlacementArea& area,
             RegionTree& tree,
             const Options& options,
             Engine* engine,
             Engine* top_engine,
             Engine* fallback,
             Observer* observer,
             utl::Logger* logger);

  // Decomposes from the root and places the end cases.
  void run(int root_id, RunReport& report);

 private:
  struct Job
  {
    int region = -1;
    SplitPlan plan;
    Subproblem sub;
    SubproblemStats sub_stats;
    std::vector<int> parts;
    bool failed = false;
    int cut_nets = 0;
    int assignment_split = 0;
    int assignment_repairs = 0;
    bool assignment_infeasible = false;
    double assignment_dhpwl = 0.0;
    bool exact_run = false;
    bool exact_improved = false;
    bool exact_incomplete = false;
    double transport_seconds = 0.0;
    // American-map diagnostic of this region (LevelStats::am_*).
    bool am_ran = false;
    int am_disagree_cells = 0;
    double am_disagree_area = 0.0;
    double am_hpwl_mincut = 0.0;
    double am_hpwl_transport = 0.0;
    double am_disp_mincut = 0.0;
    double am_disp_transport = 0.0;
    double am_cut_mincut = 0.0;
    double am_cut_transport = 0.0;
    // I1: cut degradation of this bisection (Options::cut_degrade).
    bool degrade_ran = false;
    double degrade_cut_before = 0.0;
    double degrade_cut_after = 0.0;
    int degrade_swaps = 0;
    // E24: the alternative plan (the other cut direction) when the
    // sparsest-cut rule applies, and what was decided.
    bool has_alt_plan = false;
    SplitPlan alt_plan;
    bool balance_swept = false;
    bool balance_offcenter = false;
    bool direction_tried = false;
    bool direction_flipped = false;
    // External pull of the region (minlocal/mixed), computed before the
    // wave so that the jobs can derive the policy's targets in parallel.
    odb::Point pull;
    // acg: mass of the analytical rectangle in every planned child.
    std::vector<double> mass;
  };

  void runJob(Job& job,
              SubproblemBuilder& builder,
              int level,
              int engine_threads);
  // Partition job.sub (already built) with the engines, the exact
  // search and -- when the balance objective applies -- the sweep over
  // the balance fractions; leaves job.parts, job.failed, the exact
  // flags and the balance flags.  Returns the objective value of the
  // result (weighted cut, or the vector cost).
  double solve(Job& job, int level);
  // One engine run on sub within its current bounds.
  bool runEngine(Job& job, int level);
  // Objective value of a partition: the weighted cut for cutnet, the
  // sum of the net vectors otherwise.
  static double partitionCost(const Subproblem& sub,
                              const std::vector<int>& parts);
  // Weighted pin volume of every part (for the normalized cut).
  static void partVolumes(const Subproblem& sub,
                          const std::vector<int>& parts,
                          std::vector<double>& volume);
  void greedySplit(const Subproblem& sub, std::vector<int>& parts) const;
  static int countCut(const Subproblem& sub, const std::vector<int>& parts);
  // I1: worsens the bisection parts of sub by balance-preserving random
  // swaps (one vertex of each side; the sides stay within the bounds
  // they satisfy on entry) until the weighted cut is at least target or
  // the attempt budget is spent.  Returns the number of swaps made.
  static int degradeCut(const Subproblem& sub,
                        std::vector<int>& parts,
                        double target,
                        unsigned seed);
  unsigned regionSeed(int region_id) const;
  // Padding (dbu) the end case keeps between cells (Options::leaf_pad).
  int leafPadLeft() const;
  int leafPadRight() const;
  // Whether the seed placement drives the splits of this level
  // (Options::seed_levels).
  bool seededAt(int level) const
  {
    return options_.seed_placement
           && (options_.seed_levels < 0 || level < options_.seed_levels)
           && std::find(options_.am_mincut_levels.begin(),
                        options_.am_mincut_levels.end(),
                        level)
                  == options_.am_mincut_levels.end();
  }
  // Whether the American-map diagnostic runs at this level and which
  // partition it commits (Options::am_diagnose, am_commit_mincut).
  bool amDiagnoseAt(int level) const
  {
    return options_.am_diagnose && seededAt(level)
           && options_.assignment != AssignmentMode::kNone;
  }
  bool amCommitMincutAt(int level) const
  {
    return amDiagnoseAt(level) && options_.am_commit_mincut;
  }
  // American-map diagnostic: compares the min-cut partition mc_parts
  // with the assignment am_parts of job's region over the planned
  // children (same local ids: region.cells order) and fills job.am_*.
  void amCompare(Job& job,
                 const std::vector<int>& am_parts,
                 const std::vector<int>& mc_parts) const;
  // Whether a movable cell is assumed at its seed (clamped into its
  // region) rather than at its region's center until its leaf is
  // placed (Options::seed_terminals).
  bool seedTerminals() const
  {
    return options_.seed_placement && options_.seed_terminals;
  }
  // External pull of a region: mean location of the pins outside it
  // over the nets that leave it, clamped to the region (sequential: uses
  // the net stamps).
  odb::Point regionPull(const Region& region);
  // Inputs of the adaptive capacity policies for the planned children
  // of a job: distance from the region's external pull and routing
  // demand.  Leaves hints empty for the uniform policy.
  void childHints(const SplitPlan& plan,
                  const odb::Point& pull,
                  const std::vector<double>& mass,
                  const DemandMap* demand,
                  std::vector<ChildHint>& hints) const;
  // acg: the analytical mass rectangle of a region -- its cell area at
  // the target density, the region's aspect ratio, centered at the
  // area-weighted center of mass of its cells' oracle positions (their
  // current locations without an oracle), kept inside the region -- and
  // its area inside every planned child.
  std::vector<double> regionMass(const Region& region,
                                 const SplitPlan& plan) const;
  void leafStatistics(const std::vector<int>& leaves, RunReport& report) const;
  // X1b / X1a: refine (or, from_scratch, construct) the assignment of
  // the cells to the regions of next_level as one k-way mapping and keep
  // it if the HPWL at the region centers improves (moves the cells
  // between the regions of the tree, updates loc_ and region_of_).
  void kwayRefine(int level,
                  const std::vector<int>& next_level,
                  bool from_scratch,
                  LevelStats& stats);
  // X2: reversible boundary sweeps over the regions of a level
  // (src/BoundarySweep.cpp).
  void boundarySweep(int level,
                     const std::vector<int>& regions,
                     SubproblemBuilder& builder,
                     LevelStats& stats);
  // One adjacent pair: the two-bin problem, the engine, the exact
  // acceptance test and the move.  cap[q] is the capacity (dbu area) of
  // the two regions.  Returns true when a move was accepted.
  bool sweepPair(int level,
                 int a,
                 int b,
                 bool x_pair,
                 const int64_t cap[2],
                 SubproblemBuilder& builder,
                 LevelStats& stats,
                 int64_t& exact_delta);
  // HPWL of the model's nets at the current locations (dbu).
  int64_t hpwl() const;
  // HPWL of net e at the current locations (dbu).
  int64_t netHpwl(int e) const;

  const NetlistModel& netlist_;
  const PlacementArea& area_;
  RegionTree& tree_;
  const Options& options_;
  Engine* engine_;
  Engine* top_engine_;
  Engine* fallback_;
  Observer* observer_;
  utl::Logger* logger_;

  // Current location of every vertex: fixed vertices keep their
  // database location; movable cells sit at their region's center (at
  // their seed clamped into the region with seed_terminals) until their
  // leaf is placed.
  std::vector<odb::Point> loc_;
  // Region holding every movable cell (-1 for fixed vertices and pins).
  std::vector<int> region_of_;
  // Unclamped oracle positions (Options::oracle), the position model of
  // the vector objectives' local pins (seed_spread); empty without an
  // oracle.
  std::vector<odb::Point> oracle_pos_;
  // Scratch for childHints: last job that visited a net.
  std::vector<int> net_stamp_;
  int stamp_ = 0;
  // Demand map of the running level (null without congestion policies).
  const DemandMap* demand_ = nullptr;
  // Analytical oracle of the running pass (null without -oracle).
  QpOracle* oracle_ = nullptr;
  bool warned_no_progress_ = false;
};

}  // namespace mcp
