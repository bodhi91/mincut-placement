// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

// Routing-demand surrogate on a grid over the core, rebuilt from the
// current vertex locations whenever a wave of regions has been
// committed.  Two maps are kept: RUDY (every net spreads its HPWL
// uniformly over its bounding box; Spindler and Johannes, DATE 2007)
// and pin density.  A query integrates both over a box and returns
// their normalized blend, which the congestion and mixed capacity
// policies use to distribute whitespace.

#pragma once

#include <vector>

#include "odb/geom.h"

namespace mcp {

class NetlistModel;

class DemandMap
{
 public:
  // grid x grid bins over core.
  DemandMap(const NetlistModel& netlist, const odb::Rect& core, int grid);

  // Rebuilds both maps from loc[v] (the center of every vertex).
  void build(const std::vector<odb::Point>& loc);

  // 0.5 * RUDY(box) / RUDY(core) + 0.5 * pins(box) / pins(core); bins
  // partially inside the box count by their overlap.
  double demand(const odb::Rect& box) const;

  double totalWire() const { return total_wire_; }
  double totalPins() const { return total_pins_; }

 private:
  int binX(int64_t x) const;
  int binY(int64_t y) const;
  double integrate(const std::vector<double>& map, const odb::Rect& box) const;

  const NetlistModel& netlist_;
  odb::Rect core_;
  int grid_;
  double bin_w_;
  double bin_h_;
  std::vector<double> wire_;  // RUDY wire length per bin
  std::vector<double> pins_;  // pins per bin
  double total_wire_ = 0.0;
  double total_pins_ = 0.0;
};

}  // namespace mcp
