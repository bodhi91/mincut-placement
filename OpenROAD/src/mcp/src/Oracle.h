// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

// Analytical oracle of the decomposition: after a level is committed, a
// quadratic placement of all movable cells with every cell tethered to
// the center of its current region (an anchor pseudo-net, SimPL's
// lower-bound step; Gordian's center-of-gravity constraint in the limit
// of a stiff anchor) gives every cell a position inside the structure
// the tree has decided so far.  The next level's splits see those
// positions -- for the external pins (terminal propagation, ATP), the
// balance of the split (ACG) and the net costs (seed_spread) -- instead
// of region centers.  Wirelength model: bound-to-bound (Kraftwerk2),
// re-weighted from the previous solution for a few rounds; solver:
// conjugate gradient on the separable x / y systems.

#pragma once

#include <vector>

#include "odb/geom.h"

namespace utl {
class Logger;
}

namespace mcp {

class NetlistModel;
class RegionTree;

struct OracleStats
{
  int solves = 0;
  double seconds = 0.0;
  int64_t hpwl = 0;  // HPWL of the clamped oracle positions (dbu)
};

class QpOracle
{
 public:
  QpOracle(const NetlistModel& netlist, utl::Logger* logger);

  // Solves for the movable cells.  init: current locations (fixed
  // vertices at their database location; movable cells anywhere
  // sensible -- the B2B weights of the first round come from them).
  // region_of[v]: region of every movable cell; its center is the
  // anchor, its box the clamp.  anchor: Options::oracle_anchor.
  // anchor_util: exponent of the region's utilization relative to the
  // target density in the anchor weight (0: the same anchor everywhere;
  // 1: a region at a quarter of the target density anchors its cells a
  // quarter as hard -- sparse regions let the nets decide where the
  // mass is, dense ones hold the cells where they will have to be).
  // rounds: B2B re-weightings.  pos: the unclamped solution (fixed
  // vertices copied from init); clamped: the solution clamped into the
  // cells' regions.
  void solve(const std::vector<odb::Point>& init,
             const std::vector<int>& region_of,
             const RegionTree& tree,
             double anchor,
             double anchor_util,
             int rounds,
             std::vector<odb::Point>& pos,
             std::vector<odb::Point>& clamped,
             OracleStats& stats);

 private:
  const NetlistModel& netlist_;
  utl::Logger* logger_;
  std::vector<int> index_;  // vertex -> row in the system, -1 if fixed
};

}  // namespace mcp
