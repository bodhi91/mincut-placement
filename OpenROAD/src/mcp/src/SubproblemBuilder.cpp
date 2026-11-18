// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "SubproblemBuilder.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "Assignment.h"
#include "NetlistModel.h"
#include "PlacementArea.h"
#include "Region.h"
#include "par/NetVectors.h"
#include "stt/flute.h"

namespace mcp {

SubproblemBuilder::SubproblemBuilder(const NetlistModel& netlist,
                                     const PlacementArea& area,
                                     TerminalMode terminals,
                                     double terminal_band,
                                     double displacement)
    : netlist_(netlist),
      area_(area),
      terminals_(terminals),
      terminal_band_(terminal_band),
      displacement_(displacement),
      local_(netlist.numVertices(), -1),
      net_stamp_(netlist.numNets(), -1)
{
}

SubproblemBuilder::SubproblemBuilder(SubproblemBuilder&&) noexcept = default;
SubproblemBuilder::~SubproblemBuilder() = default;

int64_t SubproblemBuilder::boxDistance(const odb::Rect& b, const odb::Point& p)
{
  const int64_t dx
      = std::max<int64_t>({0,
                           static_cast<int64_t>(b.xMin()) - p.x(),
                           static_cast<int64_t>(p.x()) - b.xMax()});
  const int64_t dy
      = std::max<int64_t>({0,
                           static_cast<int64_t>(b.yMin()) - p.y(),
                           static_cast<int64_t>(p.y()) - b.yMax()});
  return dx + dy;
}

int SubproblemBuilder::nearestChild(const SplitPlan& plan, const odb::Point& p)
{
  int best = 0;
  int64_t best_dist = -1;
  for (int q = 0; q < plan.k; ++q) {
    const int64_t dist = boxDistance(plan.boxes[q], p);
    if (best_dist < 0 || dist < best_dist) {
      best_dist = dist;
      best = q;
    }
  }
  return best;
}

void SubproblemBuilder::build(const Region& region,
                              const SplitPlan& plan,
                              const std::vector<odb::Point>& loc,
                              Objective objective,
                              unsigned seed,
                              int threads,
                              Subproblem& sub,
                              SubproblemStats& stats)
{
  const int k = plan.k;
  const int n = static_cast<int>(region.cells.size());
  const double site_area
      = static_cast<double>(area_.siteWidth()) * area_.rowHeight();

  sub = Subproblem();
  sub.k = k;
  sub.num_movable = n;
  sub.seed = seed;
  sub.threads = threads;
  sub.region_id = region.id;
  stats = SubproblemStats();
  // A fresh stamp per call: the same region may be built more than once
  // (E25 -cut_direction sparsest builds both directions), and a stamp
  // tied to the region id would make the second pass skip every net.
  build_stamp_ = box_stamp_--;

  // Local ids and vertex weights (sites, at least 1).
  sub.vertex_weights.reserve(n + k);
  sub.fixed_part.reserve(n + k);
  for (int i = 0; i < n; ++i) {
    const int v = region.cells[i];
    local_[v] = i;
    touched_.push_back(v);
    const double sites = netlist_.vertex(v).area / site_area;
    sub.vertex_weights.push_back(
        static_cast<float>(std::max(1.0, std::round(sites))));
    sub.fixed_part.push_back(-1);
  }

  if (objective == Objective::kCutNet) {
    buildCutNet(region, plan, loc, sub, stats);
  } else {
    buildVectors(region, plan, loc, objective, sub, stats);
  }

  sub.upper.resize(k);
  sub.lower.resize(k);
  for (int q = 0; q < k; ++q) {
    sub.upper[q] = static_cast<float>(plan.upper[q] / site_area);
    sub.lower[q] = static_cast<float>(plan.lower[q] / site_area);
  }

  for (int v : touched_) {
    local_[v] = -1;
  }
  touched_.clear();
}

void SubproblemBuilder::buildCutNet(const Region& region,
                                    const SplitPlan& plan,
                                    const std::vector<odb::Point>& loc,
                                    Subproblem& sub,
                                    SubproblemStats& stats)
{
  const int k = plan.k;
  const int n = static_cast<int>(region.cells.size());

  // Terminal vertices are created on first use.
  std::vector<int> terminal_id(k, -1);
  auto terminal = [&](int q) {
    if (terminal_id[q] < 0) {
      terminal_id[q] = static_cast<int>(sub.vertex_weights.size());
      sub.vertex_weights.push_back(0.0f);
      sub.fixed_part.push_back(q);
    }
    return terminal_id[q];
  };

  const int stamp = build_stamp_;
  std::vector<int> edge;
  std::vector<bool> child_hit(k);
  for (int i = 0; i < n; ++i) {
    const int v = region.cells[i];
    for (int e : netlist_.vertexNets(v)) {
      if (net_stamp_[e] == stamp) {
        continue;
      }
      net_stamp_[e] = stamp;

      edge.clear();
      std::fill(child_hit.begin(), child_hit.end(), false);
      int num_children_hit = 0;
      const auto verts = netlist_.netVertices(e);
      const auto offsets = netlist_.netPinOffsets(e);
      for (size_t j = 0; j < verts.size(); ++j) {
        const int u = verts[j];
        const int lid = local_[u];
        if (lid >= 0) {
          edge.push_back(lid);
          continue;
        }
        if (terminals_ == TerminalMode::kNone) {
          continue;
        }
        const odb::Point pin(loc[u].x() + offsets[j].x(),
                             loc[u].y() + offsets[j].y());
        const int q = nearestChild(plan, pin);
        if (!child_hit[q]) {
          child_hit[q] = true;
          ++num_children_hit;
        }
      }

      if (num_children_hit >= 2) {
        // Cut whatever the assignment: no influence on the cut-net
        // objective.
        ++stats.nets_inessential;
        continue;
      }
      if (num_children_hit == 1) {
        for (int q = 0; q < k; ++q) {
          if (child_hit[q]) {
            edge.push_back(terminal(q));
          }
        }
        ++stats.nets_terminal;
      } else {
        if (edge.size() < 2) {
          ++stats.nets_single;
          continue;
        }
        ++stats.nets_internal;
      }
      sub.hyperedges.push_back(edge);
      sub.hyperedge_weights.push_back(netlist_.netWeight(e));
      ++stats.nets;
    }
  }
}

void SubproblemBuilder::buildVectors(const Region& region,
                                     const SplitPlan& plan,
                                     const std::vector<odb::Point>& loc,
                                     Objective objective,
                                     Subproblem& sub,
                                     SubproblemStats& stats)
{
  const int k = plan.k;
  const int n = static_cast<int>(region.cells.size());
  const int num_masks = 1 << k;

  auto vectors = std::make_shared<par::NetVectors>();
  vectors->num_parts = k;
  vectors->num_masks = num_masks;

  std::vector<odb::Point> centers(k);
  // Where a net's local pins are modeled in child q: the center (the
  // classical model, seed_spread 1) or, with a seed, the bbox of their
  // seeds clamped into the child interpolated towards the center by
  // seed_spread.  The classical boxes are shared by every net.
  std::vector<odb::Rect> center_box(k);
  std::vector<odb::Rect> child_box(k);
  const bool seed_model = seed_spread_ < 1.0;
  std::vector<odb::Point> seed_pins;  // local pins of the net (seed_model)
  for (int q = 0; q < k; ++q) {
    centers[q] = odb::Point(plan.boxes[q].xCenter(), plan.boxes[q].yCenter());
    center_box[q] = odb::Rect(centers[q], centers[q]);
  }

  // Internal cut lines of the plan and the half-width of the ambiguity
  // band around them (a fraction of the child extent along that axis).
  std::vector<int> x_cuts, y_cuts;
  int band_x = 0, band_y = 0;
  if (terminal_band_ > 0.0) {
    int min_dx = region.box.dx(), min_dy = region.box.dy();
    for (int q = 0; q < k; ++q) {
      const odb::Rect& b = plan.boxes[q];
      min_dx = std::min(min_dx, b.dx());
      min_dy = std::min(min_dy, b.dy());
      for (int x : {b.xMin(), b.xMax()}) {
        if (x > region.box.xMin() && x < region.box.xMax()
            && std::find(x_cuts.begin(), x_cuts.end(), x) == x_cuts.end()) {
          x_cuts.push_back(x);
        }
      }
      for (int y : {b.yMin(), b.yMax()}) {
        if (y > region.box.yMin() && y < region.box.yMax()
            && std::find(y_cuts.begin(), y_cuts.end(), y) == y_cuts.end()) {
          y_cuts.push_back(y);
        }
      }
    }
    band_x = static_cast<int>(terminal_band_ * min_dx);
    band_y = static_cast<int>(terminal_band_ * min_dy);
  }
  auto ambiguous = [](int c, const std::vector<int>& cuts, int band) {
    for (int cut : cuts) {
      if (std::abs(c - cut) <= band) {
        return true;
      }
    }
    return false;
  };

  const int stamp = build_stamp_;
  std::vector<int> edge;
  std::vector<ExtPin> ext_points;
  for (int i = 0; i < n; ++i) {
    const int v = region.cells[i];
    for (int e : netlist_.vertexNets(v)) {
      if (net_stamp_[e] == stamp) {
        continue;
      }
      net_stamp_[e] = stamp;

      edge.clear();
      ext_points.clear();
      seed_pins.clear();
      ExtSummary ext;
      const auto verts = netlist_.netVertices(e);
      const auto offsets = netlist_.netPinOffsets(e);
      for (size_t j = 0; j < verts.size(); ++j) {
        const int u = verts[j];
        const int lid = local_[u];
        if (lid >= 0) {
          edge.push_back(lid);
          if (seed_model) {
            const odb::Point& s
                = model_ != nullptr ? (*model_)[u] : netlist_.vertex(u).loc;
            seed_pins.emplace_back(s.x() + offsets[j].x(),
                                   s.y() + offsets[j].y());
          }
          continue;
        }
        if (terminals_ == TerminalMode::kNone) {
          continue;
        }
        ExtPin pin;
        pin.p = odb::Point(loc[u].x() + offsets[j].x(),
                           loc[u].y() + offsets[j].y());
        const Vertex& vert = netlist_.vertex(u);
        if (!vert.fixed && vert.bterm == nullptr) {
          pin.amb_x = ambiguous(pin.p.x(), x_cuts, band_x);
          pin.amb_y = ambiguous(pin.p.y(), y_cuts, band_y);
        }
        ++ext.count;
        if (!pin.amb_x) {
          ext.x_lo = ext.has_x ? std::min(ext.x_lo, pin.p.x()) : pin.p.x();
          ext.x_hi = ext.has_x ? std::max(ext.x_hi, pin.p.x()) : pin.p.x();
          ext.has_x = true;
        }
        if (!pin.amb_y) {
          ext.y_lo = ext.has_y ? std::min(ext.y_lo, pin.p.y()) : pin.p.y();
          ext.y_hi = ext.has_y ? std::max(ext.y_hi, pin.p.y()) : pin.p.y();
          ext.has_y = true;
        }
        if (!pin.amb_x && !pin.amb_y) {
          ext.mask |= 1 << nearestChild(plan, pin.p);
        }
        if (!pin.amb_x || !pin.amb_y) {
          ++ext.effective;
        }
        if (objective == Objective::kRsmt
            && static_cast<int>(ext_points.size()) < kMaxRsmtPoints) {
          ext_points.push_back(pin);
        }
      }

      if (edge.size() < 2 && ext.effective == 0) {
        ++stats.nets_single;
        continue;
      }
      if (ext.count > 0) {
        ++stats.nets_terminal;
      } else {
        ++stats.nets_internal;
      }

      const std::vector<odb::Rect>* boxes = &center_box;
      if (objective == Objective::kPhpwl) {
        // The local pins are modeled as uniform over the occupied
        // children's boxes (not at their centers, and not at a seed).
        boxes = &plan.boxes;
      } else if (seed_model && !seed_pins.empty()) {
        const double a = seed_spread_;
        for (int q = 0; q < k; ++q) {
          const odb::Rect& b = plan.boxes[q];
          int x_lo = 0, x_hi = 0, y_lo = 0, y_hi = 0;
          bool first = true;
          for (const odb::Point& s : seed_pins) {
            const int x = std::clamp(s.x(), b.xMin(), b.xMax());
            const int y = std::clamp(s.y(), b.yMin(), b.yMax());
            x_lo = first ? x : std::min(x_lo, x);
            x_hi = first ? x : std::max(x_hi, x);
            y_lo = first ? y : std::min(y_lo, y);
            y_hi = first ? y : std::max(y_hi, y);
            first = false;
          }
          const odb::Point& c = centers[q];
          auto mix = [a](int center, int seed) {
            return static_cast<int>(std::lround(a * center + (1.0 - a) * seed));
          };
          child_box[q] = odb::Rect(mix(c.x(), x_lo),
                                   mix(c.y(), y_lo),
                                   mix(c.x(), x_hi),
                                   mix(c.y(), y_hi));
        }
        boxes = &child_box;
      }

      const float w = netlist_.netWeight(e);
      const size_t base = vectors->net.size();
      vectors->net.resize(base + num_masks, 0.0f);
      const int num_local = static_cast<int>(edge.size());
      for (int mask = 1; mask < num_masks; ++mask) {
        vectors->net[base + mask] = static_cast<float>(
            w
            * maskCost(objective, k, mask, *boxes, ext, ext_points, num_local));
      }
      sub.hyperedges.push_back(edge);
      sub.hyperedge_weights.push_back(w);
      ++stats.nets;
    }
  }

  // Displacement cost of a seed placement: cell i in child q costs
  // delta * (a_i / mean area) * d(seed_i, c_q) / unit.  The unit makes
  // delta = 1 mean "moving an average cell from one child center to
  // another costs one net spanning those children": a row height for
  // hpwl/rsmt (net costs are in row heights), the mean distance between
  // child centers for conn (a cut net costs 1).
  if (displacement_ > 0.0 && n > 0) {
    double mean_area = 0.0;
    for (int v : region.cells) {
      mean_area += static_cast<double>(netlist_.vertex(v).area);
    }
    mean_area /= n;
    double unit = area_.rowHeight();
    if (objective == Objective::kConn) {
      double sum = 0.0;
      int pairs = 0;
      for (int a = 0; a < k; ++a) {
        for (int b = a + 1; b < k; ++b) {
          sum += std::abs(static_cast<double>(centers[a].x() - centers[b].x()))
                 + std::abs(
                     static_cast<double>(centers[a].y() - centers[b].y()));
          ++pairs;
        }
      }
      unit = std::max(unit, sum / std::max(1, pairs));
    }
    vectors->vertex.assign(static_cast<size_t>(n) * k, 0.0f);
    for (int i = 0; i < n; ++i) {
      const Vertex& vert = netlist_.vertex(region.cells[i]);
      const double scale = displacement_ * vert.area / mean_area / unit;
      for (int q = 0; q < k; ++q) {
        // Distance to the child's box (0 inside it): for a bisection the
        // distance to the cut line, so a cell on its seed's side pays
        // nothing wherever it is within the child.  (Distance to the
        // center would move the decision boundary to the bisector of
        // the centers, which is far from the cut line once the shifted
        // lines make the children unequal.)
        const double d
            = static_cast<double>(boxDistance(plan.boxes[q], vert.loc));
        vectors->vertex[static_cast<size_t>(i) * k + q]
            = static_cast<float>(scale * d);
      }
    }
  }
  sub.vectors = std::move(vectors);
}

void SubproblemBuilder::NetBox::add(int x, int y)
{
  if (count == 0) {
    x_lo1 = x_lo2 = x_hi1 = x_hi2 = x;
    y_lo1 = y_lo2 = y_hi1 = y_hi2 = y;
  } else {
    if (x < x_lo1) {
      x_lo2 = x_lo1;
      x_lo1 = x;
    } else if (count == 1 || x < x_lo2) {
      x_lo2 = x;
    }
    if (x > x_hi1) {
      x_hi2 = x_hi1;
      x_hi1 = x;
    } else if (count == 1 || x > x_hi2) {
      x_hi2 = x;
    }
    if (y < y_lo1) {
      y_lo2 = y_lo1;
      y_lo1 = y;
    } else if (count == 1 || y < y_lo2) {
      y_lo2 = y;
    }
    if (y > y_hi1) {
      y_hi2 = y_hi1;
      y_hi1 = y;
    } else if (count == 1 || y > y_hi2) {
      y_hi2 = y;
    }
  }
  ++count;
}

int64_t SubproblemBuilder::NetBox::hpwlReplacing(int x,
                                                 int y,
                                                 int nx,
                                                 int ny) const
{
  if (count <= 1) {
    return 0;
  }
  // Extremes of the other pins: the second extreme when the removed pin
  // is the first (ties keep the value, as the second extreme is then
  // equal to the first).
  const int x_lo = (x == x_lo1) ? x_lo2 : x_lo1;
  const int x_hi = (x == x_hi1) ? x_hi2 : x_hi1;
  const int y_lo = (y == y_lo1) ? y_lo2 : y_lo1;
  const int y_hi = (y == y_hi1) ? y_hi2 : y_hi1;
  return static_cast<int64_t>(std::max(x_hi, nx) - std::min(x_lo, nx))
         + (std::max(y_hi, ny) - std::min(y_lo, ny));
}

odb::Point SubproblemBuilder::modelPos(const Region& region,
                                       const SplitPlan& plan,
                                       const std::vector<int>* parts,
                                       int i) const
{
  const odb::Rect& box
      = parts != nullptr ? plan.boxes[(*parts)[i]] : region.box;
  return box.closestPtInside(netlist_.vertex(region.cells[i]).loc);
}

void SubproblemBuilder::buildNetBoxes(const Region& region,
                                      const SplitPlan& plan,
                                      const std::vector<odb::Point>& loc,
                                      const std::vector<int>* parts)
{
  const int n = static_cast<int>(region.cells.size());
  if (net_box_.size() != static_cast<size_t>(netlist_.numNets())) {
    net_box_.resize(netlist_.numNets());
  }
  for (int i = 0; i < n; ++i) {
    local_[region.cells[i]] = i;
    touched_.push_back(region.cells[i]);
  }
  // A stamp distinct from build()'s and from the previous call's, so
  // that no pass over the nets skips another's.
  const int stamp = box_stamp_--;
  for (int i = 0; i < n; ++i) {
    for (int e : netlist_.vertexNets(region.cells[i])) {
      if (net_stamp_[e] == stamp) {
        continue;
      }
      net_stamp_[e] = stamp;
      NetBox& box = net_box_[e];
      box = NetBox();
      const auto verts = netlist_.netVertices(e);
      const auto offsets = netlist_.netPinOffsets(e);
      for (size_t j = 0; j < verts.size(); ++j) {
        const int u = verts[j];
        const int lid = local_[u];
        if (lid >= 0) {
          const odb::Point p = modelPos(region, plan, parts, lid);
          box.add(p.x() + offsets[j].x(), p.y() + offsets[j].y());
        } else if (terminals_ != TerminalMode::kNone) {
          box.add(loc[u].x() + offsets[j].x(), loc[u].y() + offsets[j].y());
        }
      }
    }
  }
}

double SubproblemBuilder::deltaHpwl(const Region& region,
                                    const SplitPlan& plan,
                                    const std::vector<int>* parts,
                                    int i,
                                    const odb::Point& p) const
{
  const int v = region.cells[i];
  const odb::Point from = modelPos(region, plan, parts, i);
  const auto nets = netlist_.vertexNets(v);
  const auto offsets = netlist_.vertexPinOffsets(v);
  double delta = 0.0;
  for (size_t j = 0; j < nets.size(); ++j) {
    const NetBox& box = net_box_[nets[j]];
    if (box.count <= 1) {
      continue;
    }
    const int fx = from.x() + offsets[j].x();
    const int fy = from.y() + offsets[j].y();
    const int64_t before = box.hpwlReplacing(fx, fy, fx, fy);
    const int64_t after
        = box.hpwlReplacing(fx, fy, p.x() + offsets[j].x(), p.y() + offsets[j].y());
    delta += netlist_.netWeight(nets[j]) * static_cast<double>(after - before);
  }
  return delta;
}

void SubproblemBuilder::buildAssignment(const Region& region,
                                        const SplitPlan& plan,
                                        const std::vector<odb::Point>& loc,
                                        double net_weight,
                                        const std::vector<int>* parts,
                                        AssignmentProblem& problem)
{
  const int k = plan.k;
  const int n = static_cast<int>(region.cells.size());
  const double site_area
      = static_cast<double>(area_.siteWidth()) * area_.rowHeight();
  problem = AssignmentProblem();
  problem.k = k;
  problem.weight.resize(n);
  problem.cost.resize(static_cast<size_t>(n) * k);
  const bool nets = net_weight > 0.0;
  if (nets) {
    buildNetBoxes(region, plan, loc, parts);
  }
  for (int i = 0; i < n; ++i) {
    const Vertex& vert = netlist_.vertex(region.cells[i]);
    const double sites = vert.area / site_area;
    problem.weight[i] = static_cast<int64_t>(std::max(1.0, std::round(sites)));
    int64_t* cost = &problem.cost[static_cast<size_t>(i) * k];
    int64_t lowest = 0;
    for (int q = 0; q < k; ++q) {
      // Distance to the child's box, see buildVectors, plus the
      // wirelength change of the cell's nets when it sits there.
      double c = static_cast<double>(boxDistance(plan.boxes[q], vert.loc));
      if (nets) {
        const odb::Point p = plan.boxes[q].closestPtInside(vert.loc);
        c += net_weight * deltaHpwl(region, plan, parts, i, p);
      }
      cost[q] = static_cast<int64_t>(std::llround(c));
      lowest = q == 0 ? cost[q] : std::min(lowest, cost[q]);
    }
    // The cell goes to exactly one child, so a per-cell constant does
    // not change the optimum; keeping the costs non-negative keeps the
    // flow solver in its comfort zone.
    for (int q = 0; q < k; ++q) {
      cost[q] -= lowest;
    }
  }
  if (nets) {
    for (int v : touched_) {
      local_[v] = -1;
    }
    touched_.clear();
  }
  // Bounds: the physical capacity of each child at the target density
  // and the lower bound the other children's capacities force.  The
  // partitioner's balance bounds (plan.upper/lower) do not apply: the
  // seed decides how the cells are distributed, the capacities only
  // keep every child legalizable.
  double total_cap = 0.0;
  for (int q = 0; q < k; ++q) {
    total_cap += plan.capacity[q];
  }
  double S = 0.0;
  for (int i = 0; i < n; ++i) {
    S += static_cast<double>(netlist_.vertex(region.cells[i]).area);
  }
  problem.upper.resize(k);
  problem.lower.resize(k);
  for (int q = 0; q < k; ++q) {
    const double cap = plan.capacity[q];
    const double lower = std::max(0.0, S - (total_cap - cap));
    problem.upper[q] = static_cast<int64_t>(std::floor(cap / site_area));
    problem.lower[q] = static_cast<int64_t>(std::ceil(lower / site_area));
    problem.lower[q] = std::min(problem.lower[q], problem.upper[q]);
  }
}

double SubproblemBuilder::assignmentDeltaHpwl(
    const Region& region,
    const SplitPlan& plan,
    const std::vector<odb::Point>& loc,
    const std::vector<int>& parts)
{
  const int n = static_cast<int>(region.cells.size());
  buildNetBoxes(region, plan, loc, &parts);
  // With every pin at its assigned position, deltaHpwl(i, region
  // position) is the saving of undoing cell i's move; the cost of the
  // move is its negative.
  double total = 0.0;
  for (int i = 0; i < n; ++i) {
    const Vertex& vert = netlist_.vertex(region.cells[i]);
    if (boxDistance(plan.boxes[parts[i]], vert.loc) == 0) {
      continue;
    }
    total -= deltaHpwl(
        region, plan, &parts, i, region.box.closestPtInside(vert.loc));
  }
  for (int v : touched_) {
    local_[v] = -1;
  }
  touched_.clear();
  return total;
}

double SubproblemBuilder::maskCost(Objective objective,
                                   int k,
                                   int mask,
                                   const std::vector<odb::Rect>& child_box,
                                   const ExtSummary& ext,
                                   const std::vector<ExtPin>& ext_points,
                                   int num_local)
{
  const double unit = area_.rowHeight();
  switch (objective) {
    case Objective::kConn: {
      // Connectivity - 1, with every unambiguous external pin counted in
      // the child nearest to it (as terminal propagation would place it).
      return std::popcount(static_cast<unsigned>(mask | ext.mask)) - 1;
    }
    case Objective::kPhpwl: {
      // Expected bounding box of the net: its d local pins uniform over
      // the union of the occupied children's boxes (child_box holds the
      // full boxes here), the external pins where they are.  The
      // expected minimum / maximum of d uniform points on an interval
      // of length L lie L / (d + 1) inside its ends, so an uncut net is
      // not free (it spans (d - 1) / (d + 1) of its child) and cutting a
      // net costs (d - 1) / (d + 1) times the extent the cut adds: a
      // sixth of the region for a 2-pin net, half of it for a large
      // fanout.  The conditioning on "pins in both children" of a cut
      // net is neglected.
      int x_lo = 0, x_hi = 0, y_lo = 0, y_hi = 0;
      bool first = true;
      for (int q = 0; q < k; ++q) {
        if (mask & (1 << q)) {
          const odb::Rect& c = child_box[q];
          x_lo = first ? c.xMin() : std::min(x_lo, c.xMin());
          x_hi = first ? c.xMax() : std::max(x_hi, c.xMax());
          y_lo = first ? c.yMin() : std::min(y_lo, c.yMin());
          y_hi = first ? c.yMax() : std::max(y_hi, c.yMax());
          first = false;
        }
      }
      const double d = std::max(1, num_local);
      const double in_x = static_cast<double>(x_hi - x_lo) / (d + 1.0);
      const double in_y = static_cast<double>(y_hi - y_lo) / (d + 1.0);
      double lo_x = x_lo + in_x, hi_x = x_hi - in_x;
      double lo_y = y_lo + in_y, hi_y = y_hi - in_y;
      if (ext.has_x) {
        lo_x = std::min(lo_x, static_cast<double>(ext.x_lo));
        hi_x = std::max(hi_x, static_cast<double>(ext.x_hi));
      }
      if (ext.has_y) {
        lo_y = std::min(lo_y, static_cast<double>(ext.y_lo));
        hi_y = std::max(hi_y, static_cast<double>(ext.y_hi));
      }
      return ((hi_x - lo_x) + (hi_y - lo_y)) / unit;
    }
    case Objective::kHpwl:
    case Objective::kRsmt: {
      // Per-axis spans of the occupied children's boxes and the external
      // pins that are unambiguous along that axis.
      int x_lo = 0, x_hi = 0, y_lo = 0, y_hi = 0;
      int num_points = 0;
      for (int q = 0; q < k; ++q) {
        if (mask & (1 << q)) {
          const odb::Rect& c = child_box[q];
          x_lo = num_points ? std::min(x_lo, c.xMin()) : c.xMin();
          x_hi = num_points ? std::max(x_hi, c.xMax()) : c.xMax();
          y_lo = num_points ? std::min(y_lo, c.yMin()) : c.yMin();
          y_hi = num_points ? std::max(y_hi, c.yMax()) : c.yMax();
          ++num_points;
        }
      }
      if (ext.has_x) {
        x_lo = std::min(x_lo, ext.x_lo);
        x_hi = std::max(x_hi, ext.x_hi);
      }
      if (ext.has_y) {
        y_lo = std::min(y_lo, ext.y_lo);
        y_hi = std::max(y_hi, ext.y_hi);
      }
      const double hpwl
          = (static_cast<double>(x_hi - x_lo) + (y_hi - y_lo)) / unit;
      if (objective == Objective::kHpwl) {
        return hpwl;
      }
      // RSMT over the occupied children's points (box centers) and the
      // external pins when that is few enough points; HPWL otherwise
      // (also when the external pins were truncated).  An ambiguous
      // coordinate is replaced by the mean of the occupied centers'
      // coordinate, so it neither pulls the net nor adds length along
      // that axis.
      const int total = num_points + static_cast<int>(ext_points.size());
      const bool ext_truncated
          = ext.count > static_cast<int>(ext_points.size());
      if (total <= 2 || total > kMaxRsmtPoints || ext_truncated) {
        return hpwl;
      }
      xs_.clear();
      ys_.clear();
      int64_t sum_x = 0, sum_y = 0;
      for (int q = 0; q < k; ++q) {
        if (mask & (1 << q)) {
          const int cx = child_box[q].xCenter();
          const int cy = child_box[q].yCenter();
          xs_.push_back(cx);
          ys_.push_back(cy);
          sum_x += cx;
          sum_y += cy;
        }
      }
      const int mean_x = static_cast<int>(sum_x / num_points);
      const int mean_y = static_cast<int>(sum_y / num_points);
      for (const ExtPin& pin : ext_points) {
        xs_.push_back(pin.amb_x ? mean_x : pin.p.x());
        ys_.push_back(pin.amb_y ? mean_y : pin.p.y());
      }
      if (flute_ == nullptr) {
        flute_ = std::make_unique<stt::flt::Flute>();
      }
      const int wl = flute_->flute_wl(total, xs_, ys_, 3);
      return wl / unit;
    }
    case Objective::kCutNet:
      break;
  }
  return 0.0;
}

}  // namespace mcp
