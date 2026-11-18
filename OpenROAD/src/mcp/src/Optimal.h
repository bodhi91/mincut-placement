// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

// Branch-and-bound solvers for small placement instances (a handful of
// cells), in the spirit of Capo's end-case placer: the cells of one row
// are ordered optimally for HPWL, and the cells of a small multi-row
// region are assigned to rows optimally for the vertical part of HPWL.
// The pins outside the instance are fixed; the bounding box of a net
// only grows as cells are placed, so the HPWL of the placed prefix is a
// lower bound that prunes the search.  Every search starts from an
// incumbent (the heuristic layout) and can only improve on it; a node
// budget keeps the worst case bounded.

#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "mcp/MinCutPlacer.h"
#include "odb/dbTypes.h"
#include "odb/geom.h"

namespace mcp {

class NetlistModel;
class PlacementArea;

struct OptCell
{
  int vertex = -1;
  int width = 0;
  int height = 0;
  // Placement padding (dbu) the legalizer will keep free on either
  // side; the optimal position rule spaces the cells by it.
  int pad_left = 0;
  int pad_right = 0;
};

// A pin of an instance cell: offset of the pin from the cell center in
// orientation R0.
struct OptPin
{
  int cell;
  int dx;
  int dy;
};

struct OptNet
{
  std::vector<OptPin> pins;
  // Bounding box of the pins outside the instance (if any).
  bool has_external = false;
  odb::Rect external;
  float weight = 1.0f;
};

// A few cells and the nets touching them.  Everything outside the
// instance is a fixed pin folded into OptNet::external.
struct OptInstance
{
  std::vector<OptCell> cells;
  std::vector<OptNet> nets;
  std::vector<std::vector<int>> cell_nets;  // nets of every cell

  // Builds the instance for `vertices`; every other pin of their nets is
  // at loc[u] + its offset.  pad_left / pad_right (dbu) fill
  // OptCell::pad_*.
  void build(const NetlistModel& netlist,
             const std::vector<int>& vertices,
             const std::vector<odb::Point>& loc,
             int pad_left = 0,
             int pad_right = 0);

  // Weighted HPWL of the instance's nets for the given cell centers and
  // orientations.
  double cost(const std::vector<odb::Point>& centers,
              const std::vector<odb::dbOrientType>& orients) const;
};

// Offset of a pin from the cell center after orienting the cell.
odb::Point orientOffset(int dx, int dy, odb::dbOrientType orient);

// The free segments of one row (left to right): cells are placed from
// left to right, each at the site nearest to its ideal position with the
// row's free space spread evenly between the cells, moving on to the
// next segment when a cell does not fit (the end case's packing rule).
struct RowSlot
{
  std::vector<std::pair<int, int>> segments;  // [x_lo, x_hi)
  int y_lo = 0;
  odb::dbOrientType orient = odb::dbOrientType::R0;

  int64_t capacity() const;
};

// Incremental state of the packing rule; small enough to copy per node
// of a search.
class Packer
{
 public:
  Packer(const RowSlot& slot, const PlacementArea& area, double slack);
  // Places the next cell; returns its lower-left x.  A cell that does
  // not fit in any remaining segment is right-aligned in the last one
  // (overlapping) and reported through overflow.
  int place(int width, bool& overflow);
  // Smallest x the next cell can get.
  int nextMin() const;

 private:
  int snapUp(int x) const;

  const RowSlot* slot_;
  const PlacementArea* area_;
  double slack_;
  int site_;
  size_t seg_ = 0;
  int cursor_;
  double ideal_;
};

// Even slack between n cells of total width `width` in slot.
double slotSlack(const RowSlot& slot, int64_t width, int n);

struct SearchStats
{
  int64_t nodes = 0;
  bool complete = true;  // the search finished within its budget
};

// Lower-left x of every cell of `order` (indices into inst.cells) when
// packed into slot by the packing rule; overflow (parallel to order,
// optional) flags the cells that did not fit.
std::vector<int> slotPositions(const OptInstance& inst,
                               const RowSlot& slot,
                               const std::vector<int>& order,
                               const PlacementArea& area,
                               std::vector<char>* overflow = nullptr);

// Lower-left x of every cell of `order` under `rule`.  kEven is the
// packing rule above.  kOptimal and kJoint fill the slot's segments
// first-fit in order and, within every segment, place the cells at the
// positions that minimize the weighted HPWL of the instance's nets for that
// order (the other cells of the instance at centers[]): with the order fixed
// the leftmost and rightmost cell of every net are known, so the cost is
// a sum of convex piecewise-linear functions of single cell positions,
// which the clumping algorithm minimizes exactly subject to abutment;
// the result is snapped to sites.  Cells without connections keep their
// packed position.
std::vector<int> slotPositions(const OptInstance& inst,
                               const RowSlot& slot,
                               const std::vector<int>& order,
                               const PlacementArea& area,
                               PositionRule rule,
                               const std::vector<odb::Point>& centers,
                               const std::vector<odb::dbOrientType>& orients,
                               std::vector<char>* overflow = nullptr);

// Best ordering of the cells in `order` within slot.  The remaining
// cells of the instance stay at centers[]; `order` is the incumbent and
// is overwritten with the best ordering found; its cost (weighted HPWL of
// the instance's nets, with the positions of `rule`) is returned in
// best_cost.  Returns true when the search found an ordering cheaper
// than the incumbent's.  kOptimal orders with the packing-rule search
// and then clumps that order.  kJoint continues with a search under the
// clumped positions, where the lower bound of a prefix is the exact
// optimum of the relaxed problem (prefix cells only, free positions),
// so it is exact up to site snapping.
bool solveRowOrder(const OptInstance& inst,
                   const RowSlot& slot,
                   const PlacementArea& area,
                   const std::vector<odb::Point>& centers,
                   const std::vector<odb::dbOrientType>& orients,
                   PositionRule rule,
                   std::vector<int>& order,
                   int64_t node_budget,
                   double& best_cost,
                   SearchStats& stats);

struct OptRow
{
  int y_lo = 0;
  int height = 0;
  odb::dbOrientType orient = odb::dbOrientType::R0;
  int64_t capacity = 0;  // free width (dbu)
};

// Best assignment of the cells in `cells` to rows for the vertical part
// of HPWL (a cell's y is fixed by its row), subject to the rows' width
// capacities (loads are the padded widths).  row_of (indexed like inst.cells;
// entries of other cells are ignored) is the incumbent and is overwritten.
// Returns true when a cheaper assignment was found.
bool solveRowAssignment(const OptInstance& inst,
                        const std::vector<OptRow>& rows,
                        const std::vector<int>& cells,
                        std::vector<int>& row_of,
                        int64_t node_budget,
                        SearchStats& stats);

}  // namespace mcp
