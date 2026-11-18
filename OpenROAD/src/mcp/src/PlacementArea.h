// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

// Legal placement area: the rows of the block minus obstacles (fixed
// instances, macros, placement blockages).  Answers "how much free site
// area lies inside this rectangle", snaps coordinates to the row/site
// grid, and finds cut lines that split a rectangle's free area in a
// given ratio.  All coordinates are dbu.

#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "odb/dbTypes.h"
#include "odb/geom.h"

namespace odb {
class dbBlock;
}

namespace mcp {

// A free stretch of one row, clipped to a query rectangle.
struct FreeSegment
{
  int y_lo;
  int y_hi;
  int x_lo;
  int x_hi;
  odb::dbOrientType orient;
};

class PlacementArea
{
 public:
  // obstacles: rectangles that cannot hold standard cells (fixed
  // instance bounding boxes, blockages).
  PlacementArea(odb::dbBlock* block, const std::vector<odb::Rect>& obstacles);

  const odb::Rect& core() const { return core_; }
  int rowHeight() const { return row_height_; }
  int siteWidth() const { return site_width_; }
  int64_t totalUsableArea() const { return total_usable_; }

  // Free site area inside rect.
  int64_t usableArea(const odb::Rect& rect) const;

  // Number of rows intersecting rect's y-range.
  int rowsIn(const odb::Rect& rect) const;

  // Free row segments inside rect (rows fully or partially covered in
  // y; x clipped to rect), ordered by y then x.
  std::vector<FreeSegment> freeSegments(const odb::Rect& rect) const;

  // Nearest row boundary / site boundary.
  int snapY(int y) const;
  int snapX(int x) const;

  // Cut line inside rect such that the free area of the lower/left part
  // is as close as possible to fraction * usableArea(rect).  The result
  // is a row boundary (horizontal) or site boundary (vertical) strictly
  // inside rect with non-empty free area on both sides, at least
  // lo_margin from the lower/left edge and hi_margin from the upper/right
  // edge; returns false if no such line exists.
  bool findCutY(const odb::Rect& rect,
                double fraction,
                int& cut_y,
                int lo_margin = 0,
                int hi_margin = 0) const;
  bool findCutX(const odb::Rect& rect,
                double fraction,
                int& cut_x,
                int lo_margin = 0,
                int hi_margin = 0) const;

 private:
  struct RowSpan
  {
    int y_lo;
    int y_hi;
    int x_lo;
    int x_hi;
    odb::dbOrientType orient;
    std::vector<std::pair<int, int>> free;  // sorted, disjoint [lo, hi)
  };

  void buildRows(odb::dbBlock* block);
  void subtractObstacles(const std::vector<odb::Rect>& obstacles);
  int64_t freeLength(const RowSpan& row, int x_lo, int x_hi) const;
  // Candidate cut positions inside [lo, hi).
  std::vector<int> yBoundariesIn(int lo, int hi) const;
  std::vector<int> xBoundariesIn(int lo, int hi) const;

  odb::Rect core_;
  int row_height_ = 0;
  int site_width_ = 0;
  int site_x0_ = 0;  // x origin of the site grid
  int64_t total_usable_ = 0;
  std::vector<RowSpan> rows_;  // sorted by y_lo
  std::vector<int> y_bounds_;  // sorted unique row boundaries
};

}  // namespace mcp
