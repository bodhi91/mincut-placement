// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "PlacementArea.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "odb/db.h"
#include "odb/geom.h"

namespace mcp {

PlacementArea::PlacementArea(odb::dbBlock* block,
                             const std::vector<odb::Rect>& obstacles)
{
  buildRows(block);
  subtractObstacles(obstacles);
  total_usable_ = 0;
  for (const RowSpan& row : rows_) {
    total_usable_ += freeLength(row, row.x_lo, row.x_hi)
                     * static_cast<int64_t>(row.y_hi - row.y_lo);
  }
}

void PlacementArea::buildRows(odb::dbBlock* block)
{
  core_.mergeInit();
  bool first = true;
  for (odb::dbRow* db_row : block->getRows()) {
    if (db_row->getDirection() != odb::dbRowDir::HORIZONTAL) {
      continue;
    }
    const odb::Rect bbox = db_row->getBBox();
    RowSpan row;
    row.y_lo = bbox.yMin();
    row.y_hi = bbox.yMax();
    row.x_lo = bbox.xMin();
    row.x_hi = bbox.xMax();
    row.orient = db_row->getOrient();
    row.free.emplace_back(row.x_lo, row.x_hi);
    rows_.push_back(std::move(row));
    core_.merge(bbox);
    y_bounds_.push_back(bbox.yMin());
    y_bounds_.push_back(bbox.yMax());

    const int height = bbox.dy();
    const int spacing = db_row->getSpacing();
    if (first) {
      row_height_ = height;
      site_width_ = spacing;
      site_x0_ = bbox.xMin();
      first = false;
    } else {
      row_height_ = std::min(row_height_, height);
      site_width_ = std::min(site_width_, spacing);
    }
  }
  std::sort(rows_.begin(), rows_.end(), [](const RowSpan& a, const RowSpan& b) {
    return a.y_lo != b.y_lo ? a.y_lo < b.y_lo : a.x_lo < b.x_lo;
  });
  std::sort(y_bounds_.begin(), y_bounds_.end());
  y_bounds_.erase(std::unique(y_bounds_.begin(), y_bounds_.end()),
                  y_bounds_.end());
}

void PlacementArea::subtractObstacles(const std::vector<odb::Rect>& obstacles)
{
  if (obstacles.empty() || rows_.empty()) {
    return;
  }
  // Bucket obstacles by the rows they overlap.
  std::vector<std::vector<std::pair<int, int>>> blocked(rows_.size());
  for (const odb::Rect& obs : obstacles) {
    // Rows are sorted by y_lo; all rows with y_lo < obs.yMax() and
    // y_hi > obs.yMin() overlap.  Row heights may differ slightly, so
    // start a bit early.
    auto it
        = std::lower_bound(rows_.begin(),
                           rows_.end(),
                           obs.yMin() - row_height_,
                           [](const RowSpan& r, int y) { return r.y_lo < y; });
    for (; it != rows_.end() && it->y_lo < obs.yMax(); ++it) {
      if (it->y_hi <= obs.yMin()) {
        continue;
      }
      const int lo = std::max(it->x_lo, obs.xMin());
      const int hi = std::min(it->x_hi, obs.xMax());
      if (lo < hi) {
        blocked[it - rows_.begin()].emplace_back(lo, hi);
      }
    }
  }
  for (size_t i = 0; i < rows_.size(); ++i) {
    auto& b = blocked[i];
    if (b.empty()) {
      continue;
    }
    std::sort(b.begin(), b.end());
    RowSpan& row = rows_[i];
    std::vector<std::pair<int, int>> free;
    int cursor = row.x_lo;
    for (const auto& [lo, hi] : b) {
      if (lo > cursor) {
        free.emplace_back(cursor, lo);
      }
      cursor = std::max(cursor, hi);
    }
    if (cursor < row.x_hi) {
      free.emplace_back(cursor, row.x_hi);
    }
    row.free = std::move(free);
  }
}

int64_t PlacementArea::freeLength(const RowSpan& row, int x_lo, int x_hi) const
{
  int64_t len = 0;
  // free is sorted; skip intervals ending before x_lo.
  auto it = std::lower_bound(
      row.free.begin(),
      row.free.end(),
      x_lo,
      [](const std::pair<int, int>& f, int x) { return f.second <= x; });
  for (; it != row.free.end() && it->first < x_hi; ++it) {
    const int lo = std::max(it->first, x_lo);
    const int hi = std::min(it->second, x_hi);
    if (lo < hi) {
      len += hi - lo;
    }
  }
  return len;
}

int64_t PlacementArea::usableArea(const odb::Rect& rect) const
{
  int64_t area = 0;
  auto it
      = std::lower_bound(rows_.begin(),
                         rows_.end(),
                         rect.yMin() - row_height_,
                         [](const RowSpan& r, int y) { return r.y_lo < y; });
  for (; it != rows_.end() && it->y_lo < rect.yMax(); ++it) {
    const int y_lo = std::max(it->y_lo, rect.yMin());
    const int y_hi = std::min(it->y_hi, rect.yMax());
    if (y_lo >= y_hi) {
      continue;
    }
    area += freeLength(*it, rect.xMin(), rect.xMax())
            * static_cast<int64_t>(y_hi - y_lo);
  }
  return area;
}

std::vector<FreeSegment> PlacementArea::freeSegments(
    const odb::Rect& rect) const
{
  std::vector<FreeSegment> out;
  auto it
      = std::lower_bound(rows_.begin(),
                         rows_.end(),
                         rect.yMin() - row_height_,
                         [](const RowSpan& r, int y) { return r.y_lo < y; });
  for (; it != rows_.end() && it->y_lo < rect.yMax(); ++it) {
    if (it->y_hi <= rect.yMin()) {
      continue;
    }
    for (const auto& [lo, hi] : it->free) {
      const int x_lo = std::max(lo, rect.xMin());
      const int x_hi = std::min(hi, rect.xMax());
      if (x_lo < x_hi) {
        out.push_back({it->y_lo, it->y_hi, x_lo, x_hi, it->orient});
      }
    }
  }
  return out;
}

int PlacementArea::rowsIn(const odb::Rect& rect) const
{
  if (rect.yMin() >= rect.yMax() || y_bounds_.size() < 2) {
    return 0;
  }
  // Row bands are delimited by consecutive boundaries; the number of
  // bands overlapping (yMin, yMax) is the number of boundaries strictly
  // inside plus one.
  auto a = std::upper_bound(y_bounds_.begin(), y_bounds_.end(), rect.yMin());
  auto b = std::lower_bound(y_bounds_.begin(), y_bounds_.end(), rect.yMax());
  return static_cast<int>(b - a) + 1;
}

int PlacementArea::snapY(int y) const
{
  if (y_bounds_.empty()) {
    return y;
  }
  auto it = std::lower_bound(y_bounds_.begin(), y_bounds_.end(), y);
  if (it == y_bounds_.end()) {
    return y_bounds_.back();
  }
  if (it == y_bounds_.begin()) {
    return *it;
  }
  const int above = *it;
  const int below = *(it - 1);
  return (y - below) <= (above - y) ? below : above;
}

int PlacementArea::snapX(int x) const
{
  if (site_width_ <= 0) {
    return x;
  }
  const double n = std::round(static_cast<double>(x - site_x0_) / site_width_);
  return site_x0_ + static_cast<int>(n) * site_width_;
}

std::vector<int> PlacementArea::yBoundariesIn(int lo, int hi) const
{
  std::vector<int> out;
  auto it = std::upper_bound(y_bounds_.begin(), y_bounds_.end(), lo);
  for (; it != y_bounds_.end() && *it < hi; ++it) {
    out.push_back(*it);
  }
  return out;
}

std::vector<int> PlacementArea::xBoundariesIn(int lo, int hi) const
{
  std::vector<int> out;
  if (site_width_ <= 0) {
    return out;
  }
  int x = snapX(lo);
  if (x <= lo) {
    x += site_width_;
  }
  for (; x < hi; x += site_width_) {
    out.push_back(x);
  }
  return out;
}

namespace {

// Binary search for the candidate whose lower-part area is closest to
// target; area(c) must be non-decreasing in c.
template <typename AreaFn>
bool searchCut(const std::vector<int>& candidates,
               int64_t total,
               double fraction,
               AreaFn area,
               int& cut)
{
  if (candidates.empty() || total <= 0) {
    return false;
  }
  const double target = fraction * static_cast<double>(total);
  size_t lo = 0;
  size_t hi = candidates.size();  // first candidate with area >= target
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    if (static_cast<double>(area(candidates[mid])) < target) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  // Consider lo and lo-1; pick the closer with free area on both sides.
  int best = -1;
  double best_err = std::numeric_limits<double>::infinity();
  for (size_t idx : {lo, lo == 0 ? lo : lo - 1}) {
    if (idx >= candidates.size()) {
      continue;
    }
    const int64_t a = area(candidates[idx]);
    if (a <= 0 || a >= total) {
      continue;
    }
    const double err = std::abs(static_cast<double>(a) - target);
    if (err < best_err) {
      best_err = err;
      best = candidates[idx];
    }
  }
  if (best < 0) {
    // Fall back to any candidate with area on both sides.
    for (int c : candidates) {
      const int64_t a = area(c);
      if (a > 0 && a < total) {
        const double err = std::abs(static_cast<double>(a) - target);
        if (err < best_err) {
          best_err = err;
          best = c;
        }
      }
    }
  }
  if (best < 0) {
    return false;
  }
  cut = best;
  return true;
}

}  // namespace

bool PlacementArea::findCutY(const odb::Rect& rect,
                             double fraction,
                             int& cut_y,
                             int lo_margin,
                             int hi_margin) const
{
  const int64_t total = usableArea(rect);
  const std::vector<int> cands
      = yBoundariesIn(rect.yMin() + lo_margin, rect.yMax() - hi_margin);
  return searchCut(
      cands,
      total,
      fraction,
      [&](int y) {
        return usableArea(odb::Rect(rect.xMin(), rect.yMin(), rect.xMax(), y));
      },
      cut_y);
}

bool PlacementArea::findCutX(const odb::Rect& rect,
                             double fraction,
                             int& cut_x,
                             int lo_margin,
                             int hi_margin) const
{
  const int64_t total = usableArea(rect);
  const std::vector<int> cands
      = xBoundariesIn(rect.xMin() + lo_margin, rect.xMax() - hi_margin);
  return searchCut(
      cands,
      total,
      fraction,
      [&](int x) {
        return usableArea(odb::Rect(rect.xMin(), rect.yMin(), x, rect.yMax()));
      },
      cut_x);
}

}  // namespace mcp
