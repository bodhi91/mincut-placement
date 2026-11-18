// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "RegionTree.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "NetlistModel.h"
#include "PlacementArea.h"
#include "Region.h"
#include "odb/geom.h"
#include "utl/Logger.h"

namespace mcp {

RegionTree::RegionTree(const PlacementArea& area,
                       const NetlistModel& netlist,
                       const CapacityPolicy& policy,
                       int end_case_cells,
                       utl::Logger* logger)
    : area_(area),
      netlist_(netlist),
      capacity_(policy),
      end_case_cells_(end_case_cells),
      logger_(logger)
{
}

int RegionTree::newRegion(int parent, const odb::Rect& box)
{
  Region r;
  r.id = static_cast<int>(regions_.size());
  r.parent = parent;
  r.level = parent < 0 ? 0 : regions_[parent].level + 1;
  r.box = box;
  regions_.push_back(std::move(r));
  return regions_.back().id;
}

void RegionTree::finishRegion(Region& region)
{
  region.cell_area = 0;
  region.max_cell_width = 0;
  region.max_cell_height = 0;
  for (int v : region.cells) {
    const Vertex& vert = netlist_.vertex(v);
    region.cell_area += vert.area;
    region.max_cell_width = std::max(region.max_cell_width, vert.width);
    region.max_cell_height = std::max(region.max_cell_height, vert.height);
  }
  region.usable_area = area_.usableArea(region.box);
  if (region.cell_area > region.usable_area) {
    ++overfull_;
  }
}

int RegionTree::makeRoot()
{
  const int id = newRegion(-1, area_.core());
  Region& root = regions_[id];
  root.cells = netlist_.movableVertices();
  finishRegion(root);
  return id;
}

int RegionTree::chooseK(const Region& region, int forced_k) const
{
  if (forced_k == 2) {
    return 2;
  }
  const int rows = area_.rowsIn(region.box);
  const int sites
      = area_.siteWidth() > 0 ? region.box.dx() / area_.siteWidth() : 0;
  if (rows < 2 || sites < 2) {
    return 2;
  }
  const double ar = static_cast<double>(region.box.dx()) / region.box.dy();
  if (forced_k == 4) {
    return 4;
  }
  return (ar >= 0.5 && ar <= 2.0) ? 4 : 2;
}

bool RegionTree::isEndCase(const Region& region) const
{
  if (static_cast<int>(region.cells.size()) <= end_case_cells_) {
    return true;
  }
  const int rows = area_.rowsIn(region.box);
  const int sites
      = area_.siteWidth() > 0 ? region.box.dx() / area_.siteWidth() : 0;
  return rows < 2 && sites < 2;
}

bool RegionTree::planSplit(const Region& region,
                           int k,
                           SplitPlan& plan,
                           int force_vertical) const
{
  plan = SplitPlan();
  const odb::Rect& box = region.box;
  // No child may be narrower than the widest cell or shorter than the
  // tallest one; otherwise cells can never be placed inside it.
  const int mx = region.max_cell_width;
  const int my = std::max(region.max_cell_height, area_.rowHeight());
  int cx = 0;
  int cy = 0;
  bool can_x = box.dx() >= 2 * mx && area_.findCutX(box, 0.5, cx, mx, mx);
  bool can_y = box.dy() >= 2 * my && area_.findCutY(box, 0.5, cy, my, my);
  if (force_vertical >= 0) {
    k = 2;
    can_x = can_x && force_vertical == 1;
    can_y = can_y && force_vertical == 0;
  }
  if (!can_x && !can_y) {
    return false;
  }
  if (k == 4 && !(can_x && can_y)) {
    k = 2;
  }
  plan.k = k;

  if (k == 4) {
    plan.boxes = {odb::Rect(box.xMin(), box.yMin(), cx, cy),
                  odb::Rect(cx, box.yMin(), box.xMax(), cy),
                  odb::Rect(box.xMin(), cy, cx, box.yMax()),
                  odb::Rect(cx, cy, box.xMax(), box.yMax())};
  } else {
    // Cut the longer side when both directions are possible.
    const bool vertical = can_x && (!can_y || box.dx() >= box.dy());
    plan.vertical_cut = vertical;
    if (vertical) {
      plan.boxes = {odb::Rect(box.xMin(), box.yMin(), cx, box.yMax()),
                    odb::Rect(cx, box.yMin(), box.xMax(), box.yMax())};
    } else {
      plan.boxes = {odb::Rect(box.xMin(), box.yMin(), box.xMax(), cy),
                    odb::Rect(box.xMin(), cy, box.xMax(), box.yMax())};
    }
  }

  plan.usable.resize(k);
  for (int q = 0; q < k; ++q) {
    plan.usable[q] = area_.usableArea(plan.boxes[q]);
  }
  computeCapacities(region, plan);
  return true;
}

void RegionTree::computeCapacities(const Region& region, SplitPlan& plan) const
{
  const int k = plan.k;
  const double S = static_cast<double>(region.cell_area);
  const double U = static_cast<double>(region.usable_area);
  plan.upper.assign(k, 0.0);
  plan.lower.assign(k, 0.0);
  if (U <= 0.0) {
    return;
  }

  // Target density: the user's value when it can hold the cells,
  // otherwise the region's own utilization (uniform whitespace).
  bool too_low = false;
  capacity_.density(S / U, &too_low);
  if (too_low && !warned_density_) {
    logger_->warn(utl::MCP,
                  20,
                  "Target density {:.3f} is below the utilization {:.3f} "
                  "of region {}; using the utilization.",
                  capacity_.policy().density,
                  S / U,
                  region.id);
    warned_density_ = true;
  }

  std::vector<double> usable(k);
  for (int q = 0; q < k; ++q) {
    usable[q] = static_cast<double>(plan.usable[q]);
  }
  capacity_.bounds(S, U, usable, plan.upper, plan.lower);

  // Physical capacity at the target density; at least the proportional
  // share so that the children can always take the region's cells.
  const double d = capacity_.density(S / U, nullptr);
  plan.capacity.resize(k);
  for (int q = 0; q < k; ++q) {
    plan.capacity[q] = std::max(usable[q] * d, S * usable[q] / U);
    plan.capacity[q] = std::min(plan.capacity[q], usable[q]);
  }
}

RegionTree::Shift RegionTree::shiftedBoxes(const Region& region,
                                           const SplitPlan& plan,
                                           const std::vector<int>& part_of_cell,
                                           const std::vector<ChildHint>& hints,
                                           bool keep_seeds) const
{
  const int k = plan.k;
  std::vector<double> part_area(k, 0.0);
  std::vector<int> part_w(k, 0);
  std::vector<int> part_h(k, 0);
  for (size_t i = 0; i < region.cells.size(); ++i) {
    const int q = part_of_cell[i];
    const Vertex& vert = netlist_.vertex(region.cells[i]);
    part_area[q] += static_cast<double>(vert.area);
    part_w[q] = std::max(part_w[q], vert.width);
    part_h[q] = std::max(part_h[q], vert.height);
  }

  // Seeded mode: the gap around the planned line that no seed of a cell
  // on the matching side occupies.  Cells whose seed is already across
  // the planned line (moved by the capacities or the refiner) are
  // displaced anyway and do not narrow the gap.
  // side(q) is -1 for a child below/left of the line, +1 above/right,
  // 0 for a child the line does not separate.
  auto gap = [&](bool along_x, int planned, const auto& side) {
    int lo = std::numeric_limits<int>::min();
    int hi = std::numeric_limits<int>::max();
    for (size_t i = 0; i < region.cells.size(); ++i) {
      const int s = side(part_of_cell[i]);
      if (s == 0) {
        continue;
      }
      const odb::Point& seed = netlist_.vertex(region.cells[i]).loc;
      const int c = along_x ? seed.x() : seed.y();
      if (s < 0) {
        if (c <= planned) {
          lo = std::max(lo, c);
        }
      } else if (c >= planned) {
        hi = std::min(hi, c);
      }
    }
    return std::make_pair(lo, hi);
  };

  // Shift cut lines so that each child's usable area matches its
  // target: with the uniform policy the targets are the cell areas
  // (equal utilization among children) and children without cells keep
  // the planned line; the adaptive policies hand out the reserve each
  // child needs at the target density plus its share of the excess.
  const odb::Rect box = region.box;
  const std::vector<double> target = capacity_.targets(
      static_cast<double>(region.usable_area), part_area, hints);
  // The shifted line keeps every child at least as wide/tall as its
  // widest/tallest cell.
  auto shift_x = [&](const odb::Rect& r,
                     double left,
                     double right,
                     int planned,
                     int lo_margin,
                     int hi_margin,
                     std::pair<int, int> clamp_to) {
    const double total = left + right;
    if (left <= 0 || right <= 0 || total <= 0) {
      return planned;
    }
    int cut = planned;
    if (!area_.findCutX(r, left / total, cut, lo_margin, hi_margin)) {
      return planned;
    }
    const int clamped = std::clamp(cut, clamp_to.first, clamp_to.second);
    if (clamped == cut) {
      return cut;
    }
    const double frac = static_cast<double>(clamped - r.xMin()) / r.dx();
    if (area_.findCutX(r, frac, cut, lo_margin, hi_margin)) {
      return cut;
    }
    return planned;
  };
  auto shift_y = [&](const odb::Rect& r,
                     double lower,
                     double upper,
                     int planned,
                     int lo_margin,
                     int hi_margin,
                     std::pair<int, int> clamp_to) {
    const double total = lower + upper;
    if (lower <= 0 || upper <= 0 || total <= 0) {
      return planned;
    }
    int cut = planned;
    if (!area_.findCutY(r, lower / total, cut, lo_margin, hi_margin)) {
      return planned;
    }
    const int clamped = std::clamp(cut, clamp_to.first, clamp_to.second);
    if (clamped == cut) {
      return cut;
    }
    const double frac = static_cast<double>(clamped - r.yMin()) / r.dy();
    if (area_.findCutY(r, frac, cut, lo_margin, hi_margin)) {
      return cut;
    }
    return planned;
  };
  const std::pair<int, int> any(std::numeric_limits<int>::min(),
                                std::numeric_limits<int>::max());

  Shift shift;
  if (k == 4) {
    const int px = plan.boxes[0].xMax();
    const int pyl = plan.boxes[0].yMax();
    const int pyr = plan.boxes[1].yMax();
    shift.cut_x = shift_x(
        box,
        target[0] + target[2],
        target[1] + target[3],
        px,
        std::max(part_w[0], part_w[2]),
        std::max(part_w[1], part_w[3]),
        keep_seeds ? gap(true, px, [](int q) { return q % 2 == 0 ? -1 : 1; })
                   : any);
    const odb::Rect left(box.xMin(), box.yMin(), shift.cut_x, box.yMax());
    const odb::Rect right(shift.cut_x, box.yMin(), box.xMax(), box.yMax());
    shift.cut_y_left = shift_y(
        left,
        target[0],
        target[2],
        pyl,
        part_h[0],
        part_h[2],
        keep_seeds ? gap(false,
                         pyl,
                         [](int q) { return q == 0   ? -1
                                            : q == 2 ? 1
                                                     : 0; })
                   : any);
    shift.cut_y_right = shift_y(
        right,
        target[1],
        target[3],
        pyr,
        part_h[1],
        part_h[3],
        keep_seeds ? gap(false,
                         pyr,
                         [](int q) { return q == 1   ? -1
                                            : q == 3 ? 1
                                                     : 0; })
                   : any);
    shift.boxes
        = {odb::Rect(box.xMin(), box.yMin(), shift.cut_x, shift.cut_y_left),
           odb::Rect(shift.cut_x, box.yMin(), box.xMax(), shift.cut_y_right),
           odb::Rect(box.xMin(), shift.cut_y_left, shift.cut_x, box.yMax()),
           odb::Rect(shift.cut_x, shift.cut_y_right, box.xMax(), box.yMax())};
  } else if (plan.vertical_cut) {
    const int px = plan.boxes[0].xMax();
    shift.cut_x = shift_x(
        box,
        target[0],
        target[1],
        px,
        part_w[0],
        part_w[1],
        keep_seeds ? gap(true, px, [](int q) { return q == 0 ? -1 : 1; })
                   : any);
    shift.boxes = {odb::Rect(box.xMin(), box.yMin(), shift.cut_x, box.yMax()),
                   odb::Rect(shift.cut_x, box.yMin(), box.xMax(), box.yMax())};
  } else {
    const int py = plan.boxes[0].yMax();
    shift.cut_y_left = shift_y(
        box,
        target[0],
        target[1],
        py,
        part_h[0],
        part_h[1],
        keep_seeds ? gap(false, py, [](int q) { return q == 0 ? -1 : 1; })
                   : any);
    shift.boxes
        = {odb::Rect(box.xMin(), box.yMin(), box.xMax(), shift.cut_y_left),
           odb::Rect(box.xMin(), shift.cut_y_left, box.xMax(), box.yMax())};
  }
  return shift;
}

std::vector<int> RegionTree::commit(int region_id,
                                    const SplitPlan& plan,
                                    const std::vector<int>& part_of_cell,
                                    const std::vector<ChildHint>& hints,
                                    bool keep_seeds)
{
  const int k = plan.k;
  const Shift shift = shiftedBoxes(
      regions_[region_id], plan, part_of_cell, hints, keep_seeds);
  std::vector<std::vector<int>> part_cells(k);
  {
    const Region& region = regions_[region_id];
    for (size_t i = 0; i < region.cells.size(); ++i) {
      part_cells[part_of_cell[i]].push_back(region.cells[i]);
    }
  }

  std::vector<int> children;
  children.reserve(k);
  for (int q = 0; q < k; ++q) {
    const int id = newRegion(region_id, shift.boxes[q]);
    regions_[id].cells = std::move(part_cells[q]);
    finishRegion(regions_[id]);
    children.push_back(id);
  }
  Region& region = regions_[region_id];  // regions_ may have reallocated
  region.children = children;
  region.cut_x = shift.cut_x;
  region.cut_y_left = shift.cut_y_left;
  region.cut_y_right = shift.cut_y_right;
  return children;
}

}  // namespace mcp
