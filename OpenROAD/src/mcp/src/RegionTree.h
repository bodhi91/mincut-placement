// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

// Region tree: creation of the root, planning of a split (child boxes
// and capacities), and committing a partitioning result (children with
// cut lines shifted to the realized cell areas).

#pragma once

#include <cstdint>
#include <vector>

#include "Capacity.h"
#include "Region.h"

namespace utl {
class Logger;
}

namespace mcp {

class NetlistModel;
class PlacementArea;

class RegionTree
{
 public:
  RegionTree(const PlacementArea& area,
             const NetlistModel& netlist,
             const CapacityPolicy& policy,
             int end_case_cells,
             utl::Logger* logger);

  // Root region covering the core with all movable cells.
  int makeRoot();

  const Region& region(int id) const { return regions_[id]; }
  Region& region(int id) { return regions_[id]; }
  int numRegions() const { return static_cast<int>(regions_.size()); }

  // 2 or 4 (forced_k = 0 chooses automatically).  A region that cannot
  // be quadrisected (one row, narrow, or extreme aspect ratio) gets 2.
  int chooseK(const Region& region, int forced_k) const;

  // Child boxes at the planned cut lines plus capacity bounds.  Returns
  // false when no legal cut exists; the region is then an end case.
  // force_vertical: -1 = the longer side (k = 2) / both (k = 4); 1 / 0
  // = a vertical / horizontal bisection, false when that direction is
  // not possible.
  bool planSplit(const Region& region,
                 int k,
                 SplitPlan& plan,
                 int force_vertical = -1) const;

  // Create the children of region_id from a partitioning result
  // (part_of_cell[i] is the child of region.cells[i]) and shift the cut
  // lines so that each child's usable area matches the target of the
  // capacity policy (uniform: its share of cells, i.e. equal
  // utilization).  hints carry the per-child inputs of the adaptive
  // policies (empty for uniform).  With keep_seeds the shift is clamped
  // to the gap between the seed locations (Vertex::loc) of the two
  // sides, so that no cell whose seed is on its child's side of the
  // planned line ends up outside its child.  Returns the child ids.
  std::vector<int> commit(int region_id,
                          const SplitPlan& plan,
                          const std::vector<int>& part_of_cell,
                          const std::vector<ChildHint>& hints = {},
                          bool keep_seeds = false);

 private:
  // The child boxes commit() creates: the planned boxes with the cut
  // lines shifted to the capacity policy's targets.
  struct Shift
  {
    std::vector<odb::Rect> boxes;
    int cut_x = -1;
    int cut_y_left = -1;
    int cut_y_right = -1;
  };
  Shift shiftedBoxes(const Region& region,
                     const SplitPlan& plan,
                     const std::vector<int>& part_of_cell,
                     const std::vector<ChildHint>& hints,
                     bool keep_seeds) const;

 public:
  bool isEndCase(const Region& region) const;

  const Capacity& capacity() const { return capacity_; }

  // Number of regions that ended up with more cell area than usable area.
  int overfullRegions() const { return overfull_; }

 private:
  void computeCapacities(const Region& region, SplitPlan& plan) const;
  int newRegion(int parent, const odb::Rect& box);
  void finishRegion(Region& region);

  const PlacementArea& area_;
  const NetlistModel& netlist_;
  Capacity capacity_;
  int end_case_cells_;
  utl::Logger* logger_;
  std::vector<Region> regions_;
  int overfull_ = 0;
  mutable bool warned_density_ = false;
};

}  // namespace mcp
