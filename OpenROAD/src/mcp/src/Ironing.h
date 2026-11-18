// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

// Row ironing (Capo's detail placement step): after the end cases a
// window of `window` consecutive cells slides along every free row
// segment and the cells of the window are reordered optimally for HPWL
// by branch and bound (Optimal.h), with the window's free space spread
// evenly between them.  A window is rewritten only when the new order
// is cheaper than the current positions, so the HPWL never increases.

#pragma once

#include <cstdint>
#include <vector>

#include "mcp/MinCutPlacer.h"
#include "odb/geom.h"

namespace utl {
class Logger;
}

namespace mcp {

class NetlistModel;
class PlacementArea;

struct IroningStats
{
  int passes = 0;
  int windows = 0;
  int improved = 0;
  int incomplete = 0;  // windows whose search hit its node budget
  int64_t nodes = 0;   // branch-and-bound nodes over all windows
  double gain = 0.0;   // HPWL saved (dbu, window model)
  double seconds = 0.0;
};

class RowIroning
{
 public:
  // positions: position rule of the reordered windows.
  RowIroning(const NetlistModel& netlist,
             const PlacementArea& area,
             PositionRule positions,
             int pad_left,
             int pad_right,
             utl::Logger* logger);

  // Runs up to `passes` passes (stops early when a pass changes nothing)
  // over the placed movable cells; writes the moves to the database and
  // updates loc with the new centers.
  void run(int window,
           int passes,
           std::vector<odb::Point>& loc,
           IroningStats& stats) const;

 private:
  const NetlistModel& netlist_;
  const PlacementArea& area_;
  PositionRule positions_;
  int pad_left_;
  int pad_right_;
  utl::Logger* logger_;
};

}  // namespace mcp
