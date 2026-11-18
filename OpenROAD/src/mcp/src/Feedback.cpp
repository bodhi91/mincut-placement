// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "Feedback.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "NetlistModel.h"
#include "odb/geom.h"

namespace mcp {

DemandMap::DemandMap(const NetlistModel& netlist,
                     const odb::Rect& core,
                     int grid)
    : netlist_(netlist), core_(core), grid_(std::max(1, grid))
{
  bin_w_ = static_cast<double>(std::max(1, core_.dx())) / grid_;
  bin_h_ = static_cast<double>(std::max(1, core_.dy())) / grid_;
  wire_.assign(static_cast<size_t>(grid_) * grid_, 0.0);
  pins_.assign(wire_.size(), 0.0);
}

int DemandMap::binX(int64_t x) const
{
  const double f = (static_cast<double>(x) - core_.xMin()) / bin_w_;
  return std::clamp(static_cast<int>(f), 0, grid_ - 1);
}

int DemandMap::binY(int64_t y) const
{
  const double f = (static_cast<double>(y) - core_.yMin()) / bin_h_;
  return std::clamp(static_cast<int>(f), 0, grid_ - 1);
}

void DemandMap::build(const std::vector<odb::Point>& loc)
{
  std::fill(wire_.begin(), wire_.end(), 0.0);
  std::fill(pins_.begin(), pins_.end(), 0.0);
  total_wire_ = 0.0;
  total_pins_ = 0.0;

  // Pins at the vertex centers.
  for (int v = 0; v < netlist_.numVertices(); ++v) {
    const Vertex& vert = netlist_.vertex(v);
    if (vert.num_pins <= 0) {
      continue;
    }
    const odb::Point& p = loc[v];
    pins_[static_cast<size_t>(binY(p.y())) * grid_ + binX(p.x())]
        += vert.num_pins;
    total_pins_ += vert.num_pins;
  }

  // RUDY: HPWL of every net spread uniformly over its bounding box
  // (at least one bin in each direction), added with a 2-D difference
  // array so that a net costs O(1) regardless of its box.
  std::vector<double> diff(static_cast<size_t>(grid_ + 1) * (grid_ + 1), 0.0);
  const int stride = grid_ + 1;
  for (int e = 0; e < netlist_.numNets(); ++e) {
    const auto verts = netlist_.netVertices(e);
    if (verts.size() < 2) {
      continue;
    }
    int64_t x_lo = INT64_MAX;
    int64_t x_hi = INT64_MIN;
    int64_t y_lo = INT64_MAX;
    int64_t y_hi = INT64_MIN;
    for (int v : verts) {
      const odb::Point& p = loc[v];
      x_lo = std::min<int64_t>(x_lo, p.x());
      x_hi = std::max<int64_t>(x_hi, p.x());
      y_lo = std::min<int64_t>(y_lo, p.y());
      y_hi = std::max<int64_t>(y_hi, p.y());
    }
    const double hpwl = static_cast<double>(x_hi - x_lo + y_hi - y_lo);
    if (hpwl <= 0.0) {
      continue;
    }
    const int i0 = binX(x_lo);
    const int i1 = binX(x_hi);
    const int j0 = binY(y_lo);
    const int j1 = binY(y_hi);
    const double bins = static_cast<double>(i1 - i0 + 1) * (j1 - j0 + 1);
    const double per_bin = hpwl * netlist_.netWeight(e) / bins;
    diff[static_cast<size_t>(j0) * stride + i0] += per_bin;
    diff[static_cast<size_t>(j0) * stride + i1 + 1] -= per_bin;
    diff[static_cast<size_t>(j1 + 1) * stride + i0] -= per_bin;
    diff[static_cast<size_t>(j1 + 1) * stride + i1 + 1] += per_bin;
    total_wire_ += hpwl * netlist_.netWeight(e);
  }
  // Prefix sums: along x, then along y.
  for (int j = 0; j <= grid_; ++j) {
    for (int i = 1; i <= grid_; ++i) {
      diff[static_cast<size_t>(j) * stride + i]
          += diff[static_cast<size_t>(j) * stride + i - 1];
    }
  }
  for (int j = 1; j <= grid_; ++j) {
    for (int i = 0; i <= grid_; ++i) {
      diff[static_cast<size_t>(j) * stride + i]
          += diff[static_cast<size_t>(j - 1) * stride + i];
    }
  }
  for (int j = 0; j < grid_; ++j) {
    for (int i = 0; i < grid_; ++i) {
      wire_[static_cast<size_t>(j) * grid_ + i]
          = diff[static_cast<size_t>(j) * stride + i];
    }
  }
}

double DemandMap::integrate(const std::vector<double>& map,
                            const odb::Rect& box) const
{
  const odb::Rect r = box.intersect(core_);
  if (r.dx() <= 0 || r.dy() <= 0) {
    return 0.0;
  }
  const int i0 = binX(r.xMin());
  const int i1 = binX(r.xMax() - 1);
  const int j0 = binY(r.yMin());
  const int j1 = binY(r.yMax() - 1);
  double sum = 0.0;
  for (int j = j0; j <= j1; ++j) {
    const double by0 = core_.yMin() + j * bin_h_;
    const double by1 = by0 + bin_h_;
    const double oy
        = std::min<double>(by1, r.yMax()) - std::max<double>(by0, r.yMin());
    if (oy <= 0.0) {
      continue;
    }
    for (int i = i0; i <= i1; ++i) {
      const double bx0 = core_.xMin() + i * bin_w_;
      const double bx1 = bx0 + bin_w_;
      const double ox
          = std::min<double>(bx1, r.xMax()) - std::max<double>(bx0, r.xMin());
      if (ox <= 0.0) {
        continue;
      }
      sum += map[static_cast<size_t>(j) * grid_ + i] * (ox * oy)
             / (bin_w_ * bin_h_);
    }
  }
  return sum;
}

double DemandMap::demand(const odb::Rect& box) const
{
  double d = 0.0;
  if (total_wire_ > 0.0) {
    d += 0.5 * integrate(wire_, box) / total_wire_;
  }
  if (total_pins_ > 0.0) {
    d += 0.5 * integrate(pins_, box) / total_pins_;
  }
  return d;
}

}  // namespace mcp
