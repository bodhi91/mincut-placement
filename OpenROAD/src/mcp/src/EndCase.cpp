// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "EndCase.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include "NetlistModel.h"
#include "Optimal.h"
#include "PlacementArea.h"
#include "Region.h"
#include "odb/db.h"
#include "utl/Logger.h"

namespace mcp {

struct EndCase::Item
{
  int vertex;
  odb::dbInst* inst;
  int width;
  int height;
  odb::Point pull;
};

// A row of the region with its free segments.
struct EndCase::Row
{
  int y_lo;
  int y_hi;
  int64_t capacity = 0;
  std::vector<FreeSegment> segments;
};

// A complete layout of the region's items.
struct EndCase::Layout
{
  std::vector<int> row_of;              // per item
  std::vector<std::vector<int>> order;  // per row, items left to right
  std::vector<int> x;                   // per item, lower-left x
  std::vector<odb::dbOrientType> orient;
  std::vector<int> overflow;  // items that did not fit
};

namespace {

// Node budget of one branch-and-bound search.
constexpr int64_t kNodeBudget = 200000;

void setLocation(odb::dbInst* inst,
                 int x,
                 int y,
                 odb::dbOrientType orient,
                 odb::Point& center)
{
  inst->setOrient(orient);
  inst->setLocation(x, y);
  inst->setPlacementStatus(odb::dbPlacementStatus::PLACED);
  center = inst->getBBox()->getBox().center();
}

}  // namespace

EndCase::EndCase(const NetlistModel& netlist,
                 const PlacementArea& area,
                 bool use_seed,
                 int optimal_cells,
                 bool optimal_rows,
                 PositionRule positions,
                 int pad_left,
                 int pad_right,
                 utl::Logger* logger)
    : netlist_(netlist),
      area_(area),
      use_seed_(use_seed),
      optimal_cells_(optimal_cells),
      optimal_rows_(optimal_rows),
      positions_(positions),
      pad_left_(pad_left),
      pad_right_(pad_right),
      logger_(logger)
{
}

RowSlot EndCase::slotOf(const Row& row)
{
  RowSlot slot;
  for (const FreeSegment& seg : row.segments) {
    slot.segments.emplace_back(seg.x_lo, seg.x_hi);
  }
  slot.y_lo = row.y_lo;
  if (!row.segments.empty()) {
    slot.orient = row.segments[0].orient;
  }
  return slot;
}

std::vector<int> EndCase::packRow(const Row& row,
                                  const std::vector<int>& order,
                                  const std::vector<Item>& items,
                                  std::vector<int>& xs,
                                  std::vector<odb::dbOrientType>& orients) const
{
  // The packing rule lives in Optimal.cpp (Packer) so that the searches
  // evaluate exactly what is written; the items double as an instance
  // with the same indices.
  std::vector<int> overflow;
  if (order.empty()) {
    return overflow;
  }
  OptInstance inst;
  inst.cells.resize(items.size());
  for (size_t i = 0; i < items.size(); ++i) {
    inst.cells[i].width = items[i].width;
    inst.cells[i].height = items[i].height;
  }
  const RowSlot slot = slotOf(row);
  std::vector<char> over;
  const std::vector<int> pos = slotPositions(inst, slot, order, area_, &over);
  for (size_t j = 0; j < order.size(); ++j) {
    xs[order[j]] = pos[j];
    orients[order[j]] = slot.orient;
    if (over[j]) {
      overflow.push_back(order[j]);
    }
  }
  return overflow;
}

std::vector<EndCase::Item> EndCase::buildItems(
    const Region& region,
    const std::vector<odb::Point>& loc,
    bool seed) const
{
  // Pull point of each cell: mean location of the other pins on its nets.
  std::vector<Item> items;
  items.reserve(region.cells.size());
  for (int v : region.cells) {
    const Vertex& vert = netlist_.vertex(v);
    Item item;
    item.vertex = v;
    item.inst = vert.inst;
    item.width = vert.inst->getMaster()->getWidth();
    item.height = vert.inst->getMaster()->getHeight();
    int64_t sx = 0;
    int64_t sy = 0;
    int count = 0;
    for (int e : netlist_.vertexNets(v)) {
      const auto verts = netlist_.netVertices(e);
      const auto offsets = netlist_.netPinOffsets(e);
      for (size_t j = 0; j < verts.size(); ++j) {
        if (verts[j] == v) {
          continue;
        }
        sx += loc[verts[j]].x() + offsets[j].x();
        sy += loc[verts[j]].y() + offsets[j].y();
        ++count;
      }
    }
    item.pull = count > 0 ? odb::Point(static_cast<int>(sx / count),
                                       static_cast<int>(sy / count))
                          : region.center();
    if (seed) {
      item.pull = odb::Point(
          std::clamp(vert.loc.x(), region.box.xMin(), region.box.xMax()),
          std::clamp(vert.loc.y(), region.box.yMin(), region.box.yMax()));
    }
    items.push_back(item);
  }
  return items;
}

std::vector<EndCase::Row> EndCase::buildRows(const Region& region) const
{
  std::vector<Row> rows;
  for (const FreeSegment& seg : area_.freeSegments(region.box)) {
    if (rows.empty() || rows.back().y_lo != seg.y_lo) {
      Row row;
      row.y_lo = seg.y_lo;
      row.y_hi = seg.y_hi;
      rows.push_back(std::move(row));
    }
    rows.back().segments.push_back(seg);
    rows.back().capacity += seg.x_hi - seg.x_lo;
  }
  return rows;
}

EndCase::Layout EndCase::heuristicLayout(const std::vector<Item>& items,
                                         const std::vector<Row>& rows) const
{
  const int n = static_cast<int>(items.size());
  // Distribute cells over rows in order of their pull y so that each
  // row receives its proportional share of the total cell width.
  int64_t total_width = 0;
  int64_t total_capacity = 0;
  for (const Item& item : items) {
    total_width += item.width;
  }
  for (const Row& row : rows) {
    total_capacity += row.capacity;
  }
  const double fill = total_capacity > 0
                          ? std::min(1.0,
                                     static_cast<double>(total_width)
                                         / static_cast<double>(total_capacity))
                          : 1.0;

  std::vector<int> by_y(n);
  for (int i = 0; i < n; ++i) {
    by_y[i] = i;
  }
  std::sort(by_y.begin(), by_y.end(), [&](int a, int b) {
    if (items[a].pull.y() != items[b].pull.y()) {
      return items[a].pull.y() < items[b].pull.y();
    }
    return items[a].vertex < items[b].vertex;
  });

  Layout layout;
  layout.row_of.assign(n, 0);
  layout.order.assign(rows.size(), {});
  layout.x.assign(n, 0);
  layout.orient.assign(n, odb::dbOrientType::R0);

  // Cell i (in pull-y order) goes to the first row whose cumulative
  // target width (capacity * fill) has not been reached yet; the last
  // row takes whatever is left.
  size_t r = 0;
  double cum_target = rows[0].capacity * fill;
  int64_t cum_width = 0;
  for (int idx : by_y) {
    const Item& item = items[idx];
    while (r + 1 < rows.size()
           && static_cast<double>(cum_width) + item.width / 2.0 > cum_target) {
      ++r;
      cum_target += rows[r].capacity * fill;
    }
    layout.row_of[idx] = static_cast<int>(r);
    layout.order[r].push_back(idx);
    cum_width += item.width;
  }

  // Within each row: order by pull x and pack.
  for (size_t i = 0; i < rows.size(); ++i) {
    std::vector<int>& order = layout.order[i];
    std::sort(order.begin(), order.end(), [&](int a, int b) {
      if (items[a].pull.x() != items[b].pull.x()) {
        return items[a].pull.x() < items[b].pull.x();
      }
      return items[a].vertex < items[b].vertex;
    });
    const std::vector<int> overflow
        = packRow(rows[i], order, items, layout.x, layout.orient);
    layout.overflow.insert(
        layout.overflow.end(), overflow.begin(), overflow.end());
  }
  return layout;
}

namespace {

// Index of the row holding y (the nearest row when y is outside all).
int rowAt(const std::vector<EndCase::Row>& rows, int y);

}  // namespace

EndCase::Layout EndCase::currentLayout(const std::vector<Item>& items,
                                       const std::vector<Row>& rows) const
{
  const int n = static_cast<int>(items.size());
  Layout layout;
  layout.row_of.assign(n, 0);
  layout.order.assign(rows.size(), {});
  layout.x.assign(n, 0);
  layout.orient.assign(n, odb::dbOrientType::R0);
  for (int i = 0; i < n; ++i) {
    int x = 0;
    int y = 0;
    items[i].inst->getLocation(x, y);
    layout.x[i] = x;
    layout.orient[i] = items[i].inst->getOrient();
    layout.row_of[i] = rowAt(rows, y);
    layout.order[layout.row_of[i]].push_back(i);
  }
  for (size_t r = 0; r < rows.size(); ++r) {
    std::vector<int>& order = layout.order[r];
    std::sort(order.begin(), order.end(), [&](int a, int b) {
      if (layout.x[a] != layout.x[b]) {
        return layout.x[a] < layout.x[b];
      }
      return items[a].vertex < items[b].vertex;
    });
    // A cell overflows when it overlaps its left neighbor or lies
    // outside the row's free segments.
    int prev_end = std::numeric_limits<int>::min();
    for (int c : order) {
      const int lo = layout.x[c];
      const int hi = lo + items[c].width;
      bool inside = false;
      for (const FreeSegment& seg : rows[r].segments) {
        if (lo >= seg.x_lo && hi <= seg.x_hi) {
          inside = true;
          break;
        }
      }
      if (lo < prev_end || !inside) {
        layout.overflow.push_back(c);
      }
      prev_end = std::max(prev_end, hi);
    }
  }
  return layout;
}

namespace {

int rowAt(const std::vector<EndCase::Row>& rows, int y)
{
  int best = 0;
  int64_t best_dist = std::numeric_limits<int64_t>::max();
  for (size_t r = 0; r < rows.size(); ++r) {
    if (y >= rows[r].y_lo && y < rows[r].y_hi) {
      return static_cast<int>(r);
    }
    const int64_t dist = std::abs(static_cast<int64_t>(y) - rows[r].y_lo);
    if (dist < best_dist) {
      best_dist = dist;
      best = static_cast<int>(r);
    }
  }
  return best;
}

// Cell centers of a layout.
std::vector<odb::Point> layoutCenters(const std::vector<EndCase::Item>& items,
                                      const std::vector<EndCase::Row>& rows,
                                      const EndCase::Layout& layout)
{
  std::vector<odb::Point> centers(items.size());
  for (size_t i = 0; i < items.size(); ++i) {
    centers[i] = odb::Point(layout.x[i] + items[i].width / 2,
                            rows[layout.row_of[i]].y_lo + items[i].height / 2);
  }
  return centers;
}

bool sameLayout(const EndCase::Layout& a, const EndCase::Layout& b)
{
  return a.row_of == b.row_of && a.x == b.x && a.orient == b.orient;
}

}  // namespace

bool EndCase::place(const Region& region,
                    std::vector<odb::Point>& loc,
                    EndCaseStats& stats,
                    int use_seed,
                    bool refine) const
{
  const bool seed = use_seed < 0 ? use_seed_ : use_seed > 0;
  if (!refine) {
    ++stats.regions;
    stats.cells += static_cast<int>(region.cells.size());
  } else {
    ++stats.refine_regions;
  }

  const std::vector<Item> items = buildItems(region, loc, seed);
  const int n = static_cast<int>(items.size());
  const std::vector<Row> rows = buildRows(region);

  const odb::Point center = region.center();
  if (rows.empty()) {
    if (refine) {
      return false;
    }
    for (const Item& item : items) {
      setLocation(item.inst,
                  center.x() - item.width / 2,
                  center.y() - item.height / 2,
                  odb::dbOrientType::R0,
                  loc[item.vertex]);
      ++stats.overflow_cells;
    }
    return true;
  }

  Layout layout = heuristicLayout(items, rows);

  // The instance model: the leaf's cells and nets with every other pin
  // fixed at loc[].  The padding is modelled by the optimal position
  // rule only; the packing rule leaves the gaps to the legalizer.
  const bool optimize_leaf
      = optimal_cells_ > 0 && n >= 2 && n <= optimal_cells_;
  OptInstance inst;
  if (optimize_leaf || refine) {
    std::vector<int> vertices(n);
    for (int i = 0; i < n; ++i) {
      vertices[i] = items[i].vertex;
    }
    const bool padded = positions_ != PositionRule::kEven;
    inst.build(netlist_,
               vertices,
               loc,
               padded ? pad_left_ : 0,
               padded ? pad_right_ : 0);
  }

  // Feedback pass: the current layout is the incumbent; the heuristic
  // (recomputed with the neighbors at their placed positions) competes
  // with it.
  Layout current;
  double current_cost = 0.0;
  if (refine) {
    current = currentLayout(items, rows);
    current_cost
        = inst.cost(layoutCenters(items, rows, current), current.orient);
    const double heuristic_cost
        = inst.cost(layoutCenters(items, rows, layout), layout.orient);
    if (!(heuristic_cost < current_cost - 1e-3
          && layout.overflow.size() <= current.overflow.size())) {
      layout = current;
    }
  }

  // Branch-and-bound improvement for small leaves.  A feedback pass
  // counts its work under the refine statistics only.
  if (optimize_leaf) {
    if (refine) {
      EndCaseStats scratch;
      optimize(inst, items, rows, layout, scratch);
      stats.optimal_nodes += scratch.optimal_nodes;
    } else {
      optimize(inst, items, rows, layout, stats);
    }
  }

  if (refine) {
    if (sameLayout(layout, current)) {
      return false;
    }
    const double cost
        = inst.cost(layoutCenters(items, rows, layout), layout.orient);
    if (cost >= current_cost - 1e-3) {
      return false;
    }
    ++stats.refine_improved;
    stats.refine_gain += current_cost - cost;
    stats.overflow_cells += static_cast<int>(layout.overflow.size())
                            - static_cast<int>(current.overflow.size());
  }

  for (int i = 0; i < n; ++i) {
    setLocation(items[i].inst,
                layout.x[i],
                rows[layout.row_of[i]].y_lo,
                layout.orient[i],
                loc[items[i].vertex]);
  }

  if (!refine && !layout.overflow.empty()) {
    int64_t total_width = 0;
    int64_t total_capacity = 0;
    for (const Item& item : items) {
      total_width += item.width;
    }
    for (const Row& row : rows) {
      total_capacity += row.capacity;
    }
    debugPrint(logger_,
               utl::MCP,
               "endcase",
               1,
               "Region {}: {} of {} cells overflow; box {} x {} dbu, {} rows, "
               "capacity {} dbu, cell width {} dbu, usable {} cell area {}",
               region.id,
               layout.overflow.size(),
               items.size(),
               region.box.dx(),
               region.box.dy(),
               rows.size(),
               total_capacity,
               total_width,
               region.usable_area,
               region.cell_area);
    for (size_t i = 0; i < rows.size(); ++i) {
      std::string widths;
      int64_t width = 0;
      for (int idx : layout.order[i]) {
        widths += " " + std::to_string(items[idx].width);
        width += items[idx].width;
      }
      debugPrint(logger_,
                 utl::MCP,
                 "endcase",
                 1,
                 "  row y={} capacity {} ({} segments) width {}:{}",
                 rows[i].y_lo,
                 rows[i].capacity,
                 rows[i].segments.size(),
                 width,
                 widths);
    }
  }
  if (!refine) {
    stats.overflow_cells += static_cast<int>(layout.overflow.size());
  }
  return true;
}

void EndCase::diagnose(const Region& region,
                       const std::vector<odb::Point>& loc,
                       EndCaseStats& stats,
                       bool seeded) const
{
  const std::vector<Item> items = buildItems(region, loc, false);
  const int n = static_cast<int>(items.size());
  const std::vector<Row> rows = buildRows(region);
  if (n == 0 || rows.empty()) {
    return;
  }
  std::vector<int> vertices(n);
  for (int i = 0; i < n; ++i) {
    vertices[i] = items[i].vertex;
  }
  OptInstance inst;
  inst.build(netlist_, vertices, loc);
  const Layout current = currentLayout(items, rows);
  stats.rows_available += static_cast<int64_t>(rows.size());
  std::vector<char> used(rows.size(), 0);
  for (int r : current.row_of) {
    used[r] = 1;
  }
  stats.rows_used += std::count(used.begin(), used.end(), 1);
  stats.final_cost
      += inst.cost(layoutCenters(items, rows, current), current.orient);
  if (!seeded) {
    return;
  }
  std::vector<odb::Point> seed_centers(n);
  std::vector<char> seed_used(rows.size(), 0);
  for (int i = 0; i < n; ++i) {
    const odb::Point& s = netlist_.vertex(items[i].vertex).loc;
    seed_centers[i]
        = odb::Point(std::clamp(s.x(), region.box.xMin(), region.box.xMax()),
                     std::clamp(s.y(), region.box.yMin(), region.box.yMax()));
    seed_used[rowAt(rows, seed_centers[i].y() - items[i].height / 2)] = 1;
  }
  stats.seed_rows_used += std::count(seed_used.begin(), seed_used.end(), 1);
  stats.seed_cost += inst.cost(seed_centers, current.orient);
}

bool EndCase::optimize(const OptInstance& inst,
                       const std::vector<Item>& items,
                       const std::vector<Row>& rows,
                       Layout& layout,
                       EndCaseStats& stats) const
{
  const int n = static_cast<int>(items.size());
  ++stats.optimal_regions;
  if (inst.nets.empty()) {
    return false;
  }

  auto centers_of
      = [&](const Layout& l) { return layoutCenters(items, rows, l); };
  std::vector<RowSlot> slots;
  slots.reserve(rows.size());
  for (const Row& row : rows) {
    slots.push_back(slotOf(row));
  }
  auto overflow_of = [&](Layout& l) {
    l.overflow.clear();
    const std::vector<odb::Point> centers = centers_of(l);
    for (size_t r = 0; r < rows.size(); ++r) {
      std::vector<char> over;
      slotPositions(inst,
                    slots[r],
                    l.order[r],
                    area_,
                    positions_,
                    centers,
                    l.orient,
                    &over);
      for (size_t j = 0; j < over.size(); ++j) {
        if (over[j]) {
          l.overflow.push_back(l.order[r][j]);
        }
      }
    }
    return l.overflow.size();
  };

  const double incumbent_cost = inst.cost(centers_of(layout), layout.orient);
  SearchStats search;

  // Optimal ordering within every row, with the positions of the
  // position rule; rows swept until no row changes (a row's result
  // depends on the positions in the other rows).
  auto refine_orders = [&](Layout& l) {
    std::vector<odb::Point> centers = centers_of(l);
    const bool optimal_positions = positions_ != PositionRule::kEven;
    for (int sweep = 0; sweep < 2; ++sweep) {
      bool changed = false;
      for (size_t r = 0; r < rows.size(); ++r) {
        if (l.order[r].empty()) {
          continue;
        }
        const RowSlot& slot = slots[r];
        std::vector<int> order = l.order[r];
        double cost = 0.0;
        const bool reordered = order.size() >= 2
                               && solveRowOrder(inst,
                                                slot,
                                                area_,
                                                centers,
                                                l.orient,
                                                positions_,
                                                order,
                                                kNodeBudget,
                                                cost,
                                                search);
        if (!reordered && !optimal_positions) {
          continue;
        }
        // With optimal positions the row moves even when the order does
        // not; rewrite the row and note whether anything changed.
        l.order[r] = order;
        const std::vector<int> xs = slotPositions(
            inst, slot, order, area_, positions_, centers, l.orient);
        for (size_t j = 0; j < order.size(); ++j) {
          const int c = order[j];
          const odb::Point center(xs[j] + items[c].width / 2,
                                  slot.y_lo + items[c].height / 2);
          if (reordered || center != centers[c]) {
            changed = true;
          }
          l.x[c] = xs[j];
          centers[c] = center;
        }
      }
      if (!changed) {
        break;
      }
    }
    return inst.cost(centers, l.orient);
  };

  // A candidate may not overflow more than the incumbent (overlapping
  // cells make HPWL look better than it is).
  const size_t max_overflow = layout.overflow.size();

  // Candidate A: the incumbent's rows with optimal orderings.
  Layout a = layout;
  double cost_a = refine_orders(a);
  if (overflow_of(a) > max_overflow) {
    cost_a = std::numeric_limits<double>::infinity();
  }

  // Candidate B: optimal rows (vertical HPWL), then optimal orderings.
  // Without optimal_rows the row-assignment search is skipped and the
  // candidate keeps the heuristic rows (single-row end case: only the
  // orders are searched); everything else about it is unchanged.
  Layout b = layout;
  double cost_b = std::numeric_limits<double>::infinity();
  {
    std::vector<OptRow> opt_rows(rows.size());
    for (size_t r = 0; r < rows.size(); ++r) {
      opt_rows[r].y_lo = rows[r].y_lo;
      opt_rows[r].height = rows[r].y_hi - rows[r].y_lo;
      opt_rows[r].orient = rows[r].segments[0].orient;
      opt_rows[r].capacity = rows[r].capacity;
    }
    std::vector<int> all(n);
    for (int i = 0; i < n; ++i) {
      all[i] = i;
    }
    std::vector<int> row_of = layout.row_of;
    // On a one-row leaf the assignment search is trivial and is kept, so
    // that -optimal_rows 0 changes nothing there.
    const bool skip_rows = !optimal_rows_ && rows.size() > 1;
    if (skip_rows
        || solveRowAssignment(
            inst, opt_rows, all, row_of, kNodeBudget, search)) {
      b.row_of = row_of;
      for (auto& order : b.order) {
        order.clear();
      }
      for (int i = 0; i < n; ++i) {
        b.order[row_of[i]].push_back(i);
      }
      b.overflow.clear();
      for (size_t r = 0; r < rows.size(); ++r) {
        std::sort(b.order[r].begin(), b.order[r].end(), [&](int p, int q) {
          if (items[p].pull.x() != items[q].pull.x()) {
            return items[p].pull.x() < items[q].pull.x();
          }
          return items[p].vertex < items[q].vertex;
        });
        const std::vector<int> overflow
            = packRow(rows[r], b.order[r], items, b.x, b.orient);
        b.overflow.insert(b.overflow.end(), overflow.begin(), overflow.end());
      }
      if (b.overflow.size() <= max_overflow) {
        cost_b = refine_orders(b);
        if (overflow_of(b) > max_overflow) {
          cost_b = std::numeric_limits<double>::infinity();
        }
      }
    }
  }

  stats.optimal_nodes += search.nodes;
  if (!search.complete) {
    ++stats.optimal_incomplete;
  }
  const double best = std::min(cost_a, cost_b);
  if (best >= incumbent_cost - 1e-3) {
    return false;
  }
  layout = cost_b < cost_a ? std::move(b) : std::move(a);
  ++stats.optimal_improved;
  stats.optimal_gain += incumbent_cost - best;
  return true;
}

}  // namespace mcp
