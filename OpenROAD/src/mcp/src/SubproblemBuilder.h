// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

// Builds the partitioning subproblem of one region: its movable cells,
// the nets among them, and the external pins.  With the cut-net
// objective external pins become terminal vertices (Dunlop-Kernighan
// propagation to the nearest child box); with a geometric objective
// (conn, hpwl, rsmt) every net gets a cost vector over the 2^k - 1
// occupancy masks of the children, with the external pins folded in at
// their current locations.  One builder per thread; it keeps scratch
// arrays sized to the netlist.

#pragma once

#include <memory>
#include <vector>

#include "Engine.h"
#include "mcp/MinCutPlacer.h"
#include "odb/geom.h"

namespace stt::flt {
class Flute;
}

namespace mcp {

struct AssignmentProblem;
class NetlistModel;
class PlacementArea;
struct Region;
struct SplitPlan;

struct SubproblemStats
{
  int nets = 0;              // hyperedges handed to the engine
  int nets_internal = 0;     // no external pins
  int nets_terminal = 0;     // with external pins (one terminal, or
                             // folded into the vector)
  int nets_inessential = 0;  // cut-net only: terminals in >= 2 children
                             // (dropped)
  int nets_single = 0;       // one local pin and no external pin (dropped)
};

class SubproblemBuilder
{
 public:
  // terminal_band: see Options::terminal_band.  displacement > 0 adds
  // the seed-displacement vertex vectors (Options::displacement_weight)
  // to the vector objectives; the seed location is Vertex::loc.
  SubproblemBuilder(const NetlistModel& netlist,
                    const PlacementArea& area,
                    TerminalMode terminals,
                    double terminal_band,
                    double displacement);
  SubproblemBuilder(SubproblemBuilder&&) noexcept;
  ~SubproblemBuilder();

  // Changes the displacement weight for the following build() calls.
  void setDisplacement(double displacement) { displacement_ = displacement; }
  // Options::seed_spread for the following build() calls (1: local pins
  // at the child centers).
  void setSeedSpread(double alpha) { seed_spread_ = alpha; }
  // Position model of the local pins for seed_spread < 1: model[v] for
  // every vertex (an oracle's positions), or the seed (Vertex::loc)
  // when null.
  void setModel(const std::vector<odb::Point>* model) { model_ = model; }

  // loc[v] is the current location of every vertex (fixed vertices: the
  // instance/pin center; movable cells: the center of their region).
  void build(const Region& region,
             const SplitPlan& plan,
             const std::vector<odb::Point>& loc,
             Objective objective,
             unsigned seed,
             int threads,
             Subproblem& sub,
             SubproblemStats& stats);

  // Transportation problem of the region's cells (seed locations in
  // Vertex::loc) to the planned children: weights in sites, cost the
  // Manhattan distance to the child box in dbu plus net_weight times the
  // HPWL change of the cell's nets (Options::assignment_net_weight; loc
  // gives the external pins), bounds from the plan.  parts (optional):
  // the previous round's assignment; local pins are then modeled at
  // their seed clamped into the assigned child instead of the region.
  // Per-cell constants are removed from the costs (min over q = 0).
  void buildAssignment(const Region& region,
                       const SplitPlan& plan,
                       const std::vector<odb::Point>& loc,
                       double net_weight,
                       const std::vector<int>* parts,
                       AssignmentProblem& problem);
  // Sum over the region's cells of the HPWL change (dbu, net weights
  // applied) of putting them where parts says, with every other local
  // pin modeled at its seed clamped into its assigned child: the
  // linearized wirelength cost of the side-of-cut decisions.
  double assignmentDeltaHpwl(const Region& region,
                             const SplitPlan& plan,
                             const std::vector<odb::Point>& loc,
                             const std::vector<int>& parts);

  // Child of plan whose box is nearest to p (Manhattan distance to the
  // box; 0 inside).  Ties go to the lowest index.
  // Manhattan distance from p to the box (0 inside).
  static int64_t boxDistance(const odb::Rect& b, const odb::Point& p);
  static int nearestChild(const SplitPlan& plan, const odb::Point& p);

  // Points beyond which the RSMT of a net's pins is replaced by HPWL.
  static constexpr int kMaxRsmtPoints = 9;

 private:
  void buildCutNet(const Region& region,
                   const SplitPlan& plan,
                   const std::vector<odb::Point>& loc,
                   Subproblem& sub,
                   SubproblemStats& stats);
  void buildVectors(const Region& region,
                    const SplitPlan& plan,
                    const std::vector<odb::Point>& loc,
                    Objective objective,
                    Subproblem& sub,
                    SubproblemStats& stats);
  // An external pin of a net, as seen by the vector objectives.  A
  // coordinate of a pin of a movable (not yet placed) cell that lies
  // within the terminal band of a cut line is "ambiguous": the pin then
  // does not pull the net along that axis, so the net pays the full
  // price for being cut (classic terminal propagation).  Pins of fixed
  // cells and I/O pins are never ambiguous.
  struct ExtPin
  {
    odb::Point p;
    bool amb_x = false;
    bool amb_y = false;
  };
  // Per-axis extent of the unambiguous external pin coordinates, plus the
  // nearest-child mask of the fully unambiguous pins (conn).
  struct ExtSummary
  {
    int count = 0;      // all external pins
    int effective = 0;  // pins that influence at least one axis
    int mask = 0;
    int x_lo = 0, x_hi = 0, y_lo = 0, y_hi = 0;
    bool has_x = false, has_y = false;
  };

  // Cost of a net occupying the children in mask, given its external
  // pins, in row heights (conn: count).  child_box[q] is where the net's
  // local pins are modeled when they are in child q (the child center as
  // a degenerate box, or the bbox of their seeds clamped into the child,
  // interpolated by seed_spread; the full child boxes for phpwl, which
  // also needs the number of the net's local pins).
  double maskCost(Objective objective,
                  int k,
                  int mask,
                  const std::vector<odb::Rect>& child_box,
                  const ExtSummary& ext,
                  const std::vector<ExtPin>& ext_points,
                  int num_local = 2);

  // Bounding box of a net's modeled pins with the two extremes per axis,
  // so that one pin can be left out in O(1).
  struct NetBox
  {
    int count = 0;
    int x_lo1 = 0, x_lo2 = 0, x_hi1 = 0, x_hi2 = 0;
    int y_lo1 = 0, y_lo2 = 0, y_hi1 = 0, y_hi2 = 0;
    void add(int x, int y);
    // Half-perimeter with the pin at (x, y) removed and one at (nx, ny)
    // added.
    int64_t hpwlReplacing(int x, int y, int nx, int ny) const;
  };
  // Modeled position of local cell v (local id i) given the assignment
  // parts (nullptr: clamped into the region).
  odb::Point modelPos(const Region& region,
                      const SplitPlan& plan,
                      const std::vector<int>* parts,
                      int i) const;
  // Fills net_box_ for every net touching the region with the modeled
  // local pins and the external pins at loc.  Sets local_ (the caller
  // clears it through touched_).
  void buildNetBoxes(const Region& region,
                     const SplitPlan& plan,
                     const std::vector<odb::Point>& loc,
                     const std::vector<int>* parts);
  // HPWL change (dbu, net weights applied) of moving local cell i from
  // its modeled position to p, every other pin fixed; needs net_box_.
  double deltaHpwl(const Region& region,
                   const SplitPlan& plan,
                   const std::vector<int>* parts,
                   int i,
                   const odb::Point& p) const;

  const NetlistModel& netlist_;
  const PlacementArea& area_;
  TerminalMode terminals_;
  double terminal_band_;
  double displacement_;
  double seed_spread_ = 1.0;
  const std::vector<odb::Point>* model_ = nullptr;
  std::vector<NetBox> net_box_;  // net -> modeled bbox (buildNetBoxes)
  int box_stamp_ = -2;           // next net_stamp_ value (build, buildNetBoxes)
  int build_stamp_ = -1;         // stamp of the current build() call
  std::vector<int> local_;      // vertex -> local id or -1
  std::vector<int> net_stamp_;  // net -> stamp of the last pass that visited it
  std::vector<int> touched_;
  std::unique_ptr<stt::flt::Flute> flute_;  // created on first rsmt use
  std::vector<int> xs_, ys_;                // scratch for flute
};

}  // namespace mcp
