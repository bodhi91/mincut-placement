// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <cstdint>
#include <vector>

#include "odb/geom.h"

namespace mcp {

// A rectangular region of the core holding a set of movable cells.  The
// region tree is the placement state: a cell's position is the center of
// the region it belongs to until the end case writes it to the database.
struct Region
{
  int id = -1;
  int parent = -1;
  int level = 0;
  odb::Rect box;
  std::vector<int> cells;  // movable vertex ids (NetlistModel indices)
  int64_t cell_area = 0;
  int64_t usable_area = 0;    // free site area inside box
  int max_cell_width = 0;     // widest / tallest cell (dbu); bounds the
  int max_cell_height = 0;    // narrowest child a split may create
  std::vector<int> children;  // empty for leaves
  // Cut lines used to create the children (for reports and graphics);
  // -1 when not applicable.
  int cut_x = -1;
  int cut_y_left = -1;
  int cut_y_right = -1;

  odb::Point center() const { return box.center(); }
  bool isLeaf() const { return children.empty(); }
};

// Geometry and capacities of a prospective split of one region.
// Child order for k = 4: 0 = lower-left, 1 = lower-right, 2 = upper-left,
// 3 = upper-right.  For k = 2: 0 = left/lower, 1 = right/upper.
struct SplitPlan
{
  int k = 0;
  bool vertical_cut = true;  // k = 2 only: true = cut line is vertical
  std::vector<odb::Rect> boxes;
  std::vector<int64_t> usable;
  // Two-sided balance on the cell area assigned to each child.
  std::vector<double> upper;
  std::vector<double> lower;
  // Cell area each child holds at the target density (usable * d); the
  // seed assignment is capped by it rather than by the refiner's balance
  // tolerance.  Sums to at least the region's cell area.
  std::vector<double> capacity;
};

}  // namespace mcp
