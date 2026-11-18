// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "Oracle.h"

#include <Eigen/IterativeLinearSolvers>
#include <Eigen/SparseCore>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Capacity.h"
#include "NetlistModel.h"
#include "Region.h"
#include "RegionTree.h"
#include "utl/Logger.h"

namespace mcp {

namespace {

using SpMat = Eigen::SparseMatrix<double>;
using Triplet = Eigen::Triplet<double>;

// Pins of a net along one axis: coordinate of the pin and the row of its
// vertex in the system (-1: fixed), plus the pin offset (the variable is
// the cell center; the pin is center + offset).
struct AxisPin
{
  double coord;
  double offset;
  int row;
};

// Adds the B2B connections of one net along one axis.  Weight model of
// Kraftwerk2: every pin connects to the two extreme pins (the extremes
// to each other) with 2 / ((p - 1) * distance).
void addNet(const std::vector<AxisPin>& pins,
            double net_weight,
            std::vector<Triplet>& triplets,
            Eigen::VectorXd& rhs)
{
  const int p = static_cast<int>(pins.size());
  if (p < 2) {
    return;
  }
  int lo = 0, hi = 0;
  for (int j = 1; j < p; ++j) {
    if (pins[j].coord < pins[lo].coord) {
      lo = j;
    }
    if (pins[j].coord > pins[hi].coord) {
      hi = j;
    }
  }
  // Pins closer than this (dbu) get the weight of this distance: the
  // weights stay finite for coincident pins.
  constexpr double kMinDistance = 100.0;
  auto connect = [&](int a, int b) {
    const AxisPin& pa = pins[a];
    const AxisPin& pb = pins[b];
    if (pa.row < 0 && pb.row < 0) {
      return;
    }
    const double d = std::max(kMinDistance, std::abs(pa.coord - pb.coord));
    const double w = net_weight * 2.0 / ((p - 1) * d);
    // w * (x_a + o_a - x_b - o_b)^2
    if (pa.row >= 0 && pb.row >= 0) {
      triplets.emplace_back(pa.row, pa.row, w);
      triplets.emplace_back(pb.row, pb.row, w);
      triplets.emplace_back(pa.row, pb.row, -w);
      triplets.emplace_back(pb.row, pa.row, -w);
      rhs[pa.row] += w * (pb.offset - pa.offset);
      rhs[pb.row] += w * (pa.offset - pb.offset);
    } else if (pa.row >= 0) {
      triplets.emplace_back(pa.row, pa.row, w);
      rhs[pa.row] += w * (pb.coord - pa.offset);
    } else {
      triplets.emplace_back(pb.row, pb.row, w);
      rhs[pb.row] += w * (pa.coord - pb.offset);
    }
  };
  for (int j = 0; j < p; ++j) {
    if (j == lo || j == hi) {
      continue;
    }
    connect(j, lo);
    connect(j, hi);
  }
  if (lo != hi) {
    connect(lo, hi);
  }
}

}  // namespace

QpOracle::QpOracle(const NetlistModel& netlist, utl::Logger* logger)
    : netlist_(netlist), logger_(logger)
{
}

void QpOracle::solve(const std::vector<odb::Point>& init,
                     const std::vector<int>& region_of,
                     const RegionTree& tree,
                     double anchor,
                     double anchor_util,
                     int rounds,
                     std::vector<odb::Point>& pos,
                     std::vector<odb::Point>& clamped,
                     OracleStats& stats)
{
  const auto start = std::chrono::steady_clock::now();
  const int nv = netlist_.numVertices();
  index_.assign(nv, -1);
  int n = 0;
  for (int v = 0; v < nv; ++v) {
    const Vertex& vert = netlist_.vertex(v);
    if (!vert.fixed && vert.bterm == nullptr && region_of[v] >= 0) {
      index_[v] = n++;
    }
  }
  pos = init;
  clamped = init;
  if (n == 0) {
    return;
  }

  // Anchors: weight anchor * degree / region size, so that anchor 1
  // holds a cell within about its region against its nets (B2B forces
  // are of order one per net).
  Eigen::VectorXd anchor_w(n), anchor_x(n), anchor_y(n);
  for (int v = 0; v < nv; ++v) {
    const int r = index_[v];
    if (r < 0) {
      continue;
    }
    const Region& region = tree.region(region_of[v]);
    const double size
        = 0.5 * (static_cast<double>(region.box.dx()) + region.box.dy());
    const int degree = static_cast<int>(netlist_.vertexNets(v).size());
    double scale = 1.0;
    if (anchor_util != 0.0 && region.usable_area > 0) {
      const double own = static_cast<double>(region.cell_area)
                         / static_cast<double>(region.usable_area);
      const double d = tree.capacity().density(own, nullptr);
      scale = std::pow(std::clamp(own / std::max(d, 1e-9), 1e-3, 1.0),
                       anchor_util);
    }
    anchor_w[r] = anchor * scale * std::max(1, degree) / std::max(1.0, size);
    anchor_x[r] = region.box.xCenter();
    anchor_y[r] = region.box.yCenter();
  }

  Eigen::VectorXd x(n), y(n);
  for (int v = 0; v < nv; ++v) {
    if (index_[v] >= 0) {
      x[index_[v]] = pos[v].x();
      y[index_[v]] = pos[v].y();
    }
  }

  std::vector<Triplet> tx, ty;
  std::vector<AxisPin> px, py;
  for (int round = 0; round < std::max(1, rounds); ++round) {
    tx.clear();
    ty.clear();
    Eigen::VectorXd bx = Eigen::VectorXd::Zero(n);
    Eigen::VectorXd by = Eigen::VectorXd::Zero(n);
    for (int e = 0; e < netlist_.numNets(); ++e) {
      const auto verts = netlist_.netVertices(e);
      if (verts.size() < 2) {
        continue;
      }
      const auto offsets = netlist_.netPinOffsets(e);
      px.clear();
      py.clear();
      for (size_t j = 0; j < verts.size(); ++j) {
        const int u = verts[j];
        const int r = index_[u];
        const double cx = r >= 0 ? x[r] : static_cast<double>(pos[u].x());
        const double cy = r >= 0 ? y[r] : static_cast<double>(pos[u].y());
        px.push_back({cx + offsets[j].x(), static_cast<double>(offsets[j].x()), r});
        py.push_back({cy + offsets[j].y(), static_cast<double>(offsets[j].y()), r});
      }
      const double w = netlist_.netWeight(e);
      addNet(px, w, tx, bx);
      addNet(py, w, ty, by);
    }
    for (int r = 0; r < n; ++r) {
      tx.emplace_back(r, r, anchor_w[r]);
      ty.emplace_back(r, r, anchor_w[r]);
      bx[r] += anchor_w[r] * anchor_x[r];
      by[r] += anchor_w[r] * anchor_y[r];
    }
    SpMat ax(n, n), ay(n, n);
    ax.setFromTriplets(tx.begin(), tx.end());
    ay.setFromTriplets(ty.begin(), ty.end());

    Eigen::ConjugateGradient<SpMat, Eigen::Lower | Eigen::Upper> cg;
    cg.setMaxIterations(300);
    cg.setTolerance(1e-6);
    cg.compute(ax);
    x = cg.solveWithGuess(bx, x);
    cg.compute(ay);
    y = cg.solveWithGuess(by, y);
    ++stats.solves;
  }

  for (int v = 0; v < nv; ++v) {
    const int r = index_[v];
    if (r < 0) {
      continue;
    }
    pos[v] = odb::Point(static_cast<int>(std::lround(x[r])),
                        static_cast<int>(std::lround(y[r])));
    clamped[v] = tree.region(region_of[v]).box.closestPtInside(pos[v]);
  }

  // HPWL of the clamped positions: the oracle's quality in isolation.
  int64_t hpwl = 0;
  for (int e = 0; e < netlist_.numNets(); ++e) {
    const auto verts = netlist_.netVertices(e);
    if (verts.size() < 2) {
      continue;
    }
    const auto offsets = netlist_.netPinOffsets(e);
    int x_lo = 0, x_hi = 0, y_lo = 0, y_hi = 0;
    for (size_t j = 0; j < verts.size(); ++j) {
      const int cx = clamped[verts[j]].x() + offsets[j].x();
      const int cy = clamped[verts[j]].y() + offsets[j].y();
      if (j == 0) {
        x_lo = x_hi = cx;
        y_lo = y_hi = cy;
      } else {
        x_lo = std::min(x_lo, cx);
        x_hi = std::max(x_hi, cx);
        y_lo = std::min(y_lo, cy);
        y_hi = std::max(y_hi, cy);
      }
    }
    hpwl += static_cast<int64_t>(netlist_.netWeight(e) * ((x_hi - x_lo) + (y_hi - y_lo)));
  }
  stats.hpwl = hpwl;
  stats.seconds += std::chrono::duration<double>(
                       std::chrono::steady_clock::now() - start)
                       .count();
}

}  // namespace mcp
