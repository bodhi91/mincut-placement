// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "Optimal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "NetlistModel.h"
#include "PlacementArea.h"
#include "odb/db.h"

namespace mcp {

namespace {

constexpr double kEps = 1e-3;

// Bounding box that may be empty.
struct Box
{
  bool has = false;
  int x_lo = 0, y_lo = 0, x_hi = 0, y_hi = 0;

  void add(int x, int y)
  {
    if (!has) {
      x_lo = x_hi = x;
      y_lo = y_hi = y;
      has = true;
      return;
    }
    x_lo = std::min(x_lo, x);
    x_hi = std::max(x_hi, x);
    y_lo = std::min(y_lo, y);
    y_hi = std::max(y_hi, y);
  }
  void add(const odb::Rect& r)
  {
    add(r.xMin(), r.yMin());
    add(r.xMax(), r.yMax());
  }
  double hpwl() const
  {
    return has ? static_cast<double>(x_hi - x_lo) + (y_hi - y_lo) : 0.0;
  }
  double height() const { return has ? static_cast<double>(y_hi - y_lo) : 0.0; }
};

odb::Point pinAt(const odb::Point& center,
                 const OptPin& pin,
                 odb::dbOrientType orient)
{
  const odb::Point off = orientOffset(pin.dx, pin.dy, orient);
  return odb::Point(center.x() + off.x(), center.y() + off.y());
}

}  // namespace

odb::Point orientOffset(int dx, int dy, odb::dbOrientType orient)
{
  switch (orient.getValue()) {
    case odb::dbOrientType::MX:
      return odb::Point(dx, -dy);
    case odb::dbOrientType::MY:
      return odb::Point(-dx, dy);
    case odb::dbOrientType::R180:
      return odb::Point(-dx, -dy);
    default:
      return odb::Point(dx, dy);
  }
}

void OptInstance::build(const NetlistModel& netlist,
                        const std::vector<int>& vertices,
                        const std::vector<odb::Point>& loc,
                        int pad_left,
                        int pad_right)
{
  cells.clear();
  nets.clear();
  cell_nets.clear();
  std::unordered_map<int, int> local;
  for (size_t i = 0; i < vertices.size(); ++i) {
    const Vertex& vert = netlist.vertex(vertices[i]);
    OptCell cell;
    cell.vertex = vertices[i];
    cell.width = static_cast<int>(vert.inst->getMaster()->getWidth());
    cell.height = static_cast<int>(vert.inst->getMaster()->getHeight());
    cell.pad_left = std::max(0, pad_left);
    cell.pad_right = std::max(0, pad_right);
    cells.push_back(cell);
    local[vertices[i]] = static_cast<int>(i);
  }
  cell_nets.resize(cells.size());
  std::unordered_map<int, int> seen;
  for (size_t i = 0; i < vertices.size(); ++i) {
    for (int e : netlist.vertexNets(vertices[i])) {
      if (seen.count(e)) {
        continue;
      }
      seen[e] = 1;
      OptNet net;
      net.weight = netlist.netWeight(e);
      const auto verts = netlist.netVertices(e);
      const auto offsets = netlist.netPinOffsets(e);
      Box ext;
      for (size_t j = 0; j < verts.size(); ++j) {
        const int u = verts[j];
        auto it = local.find(u);
        if (it != local.end()) {
          net.pins.push_back({it->second, offsets[j].x(), offsets[j].y()});
        } else {
          ext.add(loc[u].x() + offsets[j].x(), loc[u].y() + offsets[j].y());
        }
      }
      if (net.pins.size() < 2 && !ext.has) {
        continue;  // constant
      }
      net.has_external = ext.has;
      if (ext.has) {
        net.external = odb::Rect(ext.x_lo, ext.y_lo, ext.x_hi, ext.y_hi);
      }
      const int idx = static_cast<int>(nets.size());
      for (const OptPin& pin : net.pins) {
        cell_nets[pin.cell].push_back(idx);
      }
      nets.push_back(std::move(net));
    }
  }
}

double OptInstance::cost(const std::vector<odb::Point>& centers,
                         const std::vector<odb::dbOrientType>& orients) const
{
  double sum = 0.0;
  for (const OptNet& net : nets) {
    Box box;
    if (net.has_external) {
      box.add(net.external);
    }
    for (const OptPin& pin : net.pins) {
      const odb::Point p = pinAt(centers[pin.cell], pin, orients[pin.cell]);
      box.add(p.x(), p.y());
    }
    sum += net.weight * box.hpwl();
  }
  return sum;
}

int64_t RowSlot::capacity() const
{
  int64_t sum = 0;
  for (const auto& [lo, hi] : segments) {
    sum += hi - lo;
  }
  return sum;
}

double slotSlack(const RowSlot& slot, int64_t width, int n)
{
  return std::max<double>(0.0, static_cast<double>(slot.capacity() - width))
         / (n + 1);
}

Packer::Packer(const RowSlot& slot, const PlacementArea& area, double slack)
    : slot_(&slot),
      area_(&area),
      slack_(slack),
      site_(std::max(1, area.siteWidth())),
      cursor_(slot.segments.empty() ? 0 : slot.segments[0].first),
      ideal_(cursor_)
{
}

int Packer::snapUp(int x) const
{
  const int s = area_->snapX(x);
  return s < x ? s + site_ : s;
}

int Packer::nextMin() const
{
  return snapUp(std::max(cursor_, slot_->segments[seg_].first));
}

int Packer::place(int width, bool& overflow)
{
  overflow = false;
  const auto& segs = slot_->segments;
  // Ideal (unsnapped) position of the cell; snapping to the nearest
  // site from the ideal position keeps rounding errors from
  // accumulating along the row.
  ideal_ += slack_;
  int x = std::max(snapUp(cursor_), area_->snapX(static_cast<int>(ideal_)));
  ideal_ += width;
  // Advance to a segment with room.
  while (seg_ < segs.size() && x + width > segs[seg_].second) {
    if (seg_ + 1 < segs.size()) {
      ++seg_;
      x = snapUp(std::max(x, segs[seg_].first));
    } else {
      break;
    }
  }
  const auto& seg = segs[seg_];
  if (x + width > seg.second) {
    // Does not fit: right-align in the last segment, overlapping.
    x = std::max(seg.first, area_->snapX(seg.second - width));
    if (x + width > seg.second && x - site_ >= seg.first) {
      x -= site_;
    }
    overflow = true;
  }
  cursor_ = x + width;
  return x;
}

std::vector<int> slotPositions(const OptInstance& inst,
                               const RowSlot& slot,
                               const std::vector<int>& order,
                               const PlacementArea& area,
                               std::vector<char>* overflow)
{
  int64_t total = 0;
  for (int c : order) {
    total += inst.cells[c].width;
  }
  Packer packer(
      slot, area, slotSlack(slot, total, static_cast<int>(order.size())));
  std::vector<int> xs;
  xs.reserve(order.size());
  if (overflow != nullptr) {
    overflow->assign(order.size(), 0);
  }
  for (size_t i = 0; i < order.size(); ++i) {
    bool over = false;
    xs.push_back(packer.place(inst.cells[order[i]].width, over));
    if (overflow != nullptr && over) {
      (*overflow)[i] = 1;
    }
  }
  return xs;
}

namespace {

// The nets of a set of cells of an instance as seen from one row: the
// box of every relevant net without the set's pins (external pins and
// the other cells of the instance at their centers), the weights, and
// the set cells' pins on them (OptPin::cell holds the local net index).
struct RowNets
{
  std::vector<Box> base;
  std::vector<float> weight;
  std::vector<std::vector<OptPin>> pins;  // indexed like inst.cells
  int m = 0;

  void build(const OptInstance& inst,
             const std::vector<int>& set,
             const std::vector<odb::Point>& centers,
             const std::vector<odb::dbOrientType>& orients)
  {
    std::vector<char> in_set(inst.cells.size(), 0);
    for (int c : set) {
      in_set[c] = 1;
    }
    std::vector<int> net_local(inst.nets.size(), -1);
    for (size_t e = 0; e < inst.nets.size(); ++e) {
      const OptNet& net = inst.nets[e];
      bool touches = false;
      Box box;
      if (net.has_external) {
        box.add(net.external);
      }
      for (const OptPin& pin : net.pins) {
        if (in_set[pin.cell]) {
          touches = true;
        } else {
          const odb::Point p = pinAt(centers[pin.cell], pin, orients[pin.cell]);
          box.add(p.x(), p.y());
        }
      }
      if (!touches) {
        continue;
      }
      net_local[e] = static_cast<int>(base.size());
      base.push_back(box);
      weight.push_back(net.weight);
    }
    m = static_cast<int>(base.size());
    pins.assign(inst.cells.size(), {});
    for (size_t e = 0; e < inst.nets.size(); ++e) {
      if (net_local[e] < 0) {
        continue;
      }
      for (const OptPin& pin : inst.nets[e].pins) {
        if (in_set[pin.cell]) {
          pins[pin.cell].push_back({net_local[e], pin.dx, pin.dy});
        }
      }
    }
  }
};

// Weight of the tie-break term pulling a cell toward its packed
// position; decides only where the HPWL is flat.
constexpr double kTie = 1e-7;

// Minimizes the weighted horizontal HPWL of the nets of a row for a
// fixed left-to-right order of its cells.  With the order fixed the
// leftmost (l) and rightmost (r) cell of every net are known, so
//   cost_e = w (max(H_e, x_r + a_r) - min(L_e, x_l + b_l))
// is a sum of convex piecewise-linear functions of single cell
// positions (a, b: pin offsets from the cell's left edge; H, L: the
// fixed part of the net's box).  The clumping algorithm places the cells
// one by one at their minimizers and merges a cell that collides with
// its left neighbor into one clump, re-minimizing the clump's total
// cost; this is exact for separable convex costs under abutment.
//
// Allocation-free after reserve(): the search calls it at every node.
class RowClumper
{
 public:
  // n cells, at most `terms` cost terms per cell.
  void reserve(int n, int terms)
  {
    cap_ = terms;
    terms_.resize(static_cast<size_t>(n) * terms);
    count_.assign(n, 0);
    widths_.assign(n, 0);
    bps_.reserve(static_cast<size_t>(n) * terms);
    merge_.reserve(static_cast<size_t>(n) * terms);
    clumps_.reserve(n);
  }

  // Resets the cells [0, n).
  void reset(int n)
  {
    std::fill(count_.begin(), count_.begin() + n, 0);
    constant_ = 0.0;
  }

  // preferred: position of a tie-break term (none when tie is false).
  void setCell(int i, int width, double preferred, bool tie)
  {
    widths_[i] = width;
    if (tie) {
      add(i, {kAbs, kTie, preferred});
    }
  }

  // Net with weight w whose fixed part reaches x_hi on the right
  // (has_hi) and x_lo on the left (has_lo); l / r are order indices,
  // off_l / off_r the pin offsets from the left edge of those cells.
  void addNet(bool has_hi,
              double x_hi,
              bool has_lo,
              double x_lo,
              double w,
              int l,
              int off_l,
              int r,
              int off_r)
  {
    if (has_hi) {
      add(r, {kMax, w, x_hi - off_r});
    } else {
      add(r, {kLin, w, 0.0});
    }
    if (has_lo) {
      add(l, {kMin, w, x_lo - off_l});
    } else {
      add(l, {kLin, -w, 0.0});
    }
    constant_ += w * (off_r - off_l);
  }

  // Cost of cell i's left edge at x (without constant()).
  double eval(int i, double x) const
  {
    double sum = 0.0;
    const Term* t = terms_.data() + static_cast<size_t>(i) * cap_;
    for (int k = 0; k < count_[i]; ++k, ++t) {
      switch (t->kind) {
        case kMax:
          sum += t->w * std::max(t->p, x);
          break;
        case kMin:
          sum -= t->w * std::min(t->p, x);
          break;
        case kLin:
          sum += t->w * x;
          break;
        case kAbs:
          sum += t->w * std::abs(x - t->p);
          break;
      }
    }
    return sum;
  }

  double constant() const { return constant_; }

  // Optimal left edges of the cells [begin, end) within [lo, hi];
  // returns the sum of their costs (add constant() for the nets' HPWL).
  // Every clump keeps its breakpoints sorted (relative to its left
  // edge) in a contiguous run of bps_, so a merge is a linear merge of
  // two runs and a minimizer a linear scan.
  double solve(int begin, int end, double lo, double hi, double* xs)
  {
    clumps_.clear();
    bps_.clear();
    for (int i = begin; i < end; ++i) {
      Clump c{i,
              i,
              static_cast<double>(widths_[i]),
              0.0,
              0.0,
              static_cast<int>(bps_.size()),
              0};
      const Term* t = terms_.data() + static_cast<size_t>(i) * cap_;
      for (int k = 0; k < count_[i]; ++k, ++t) {
        switch (t->kind) {
          case kMax:
            bps_.emplace_back(t->p, t->w);
            break;
          case kMin:
            c.slope -= t->w;
            bps_.emplace_back(t->p, t->w);
            break;
          case kLin:
            c.slope += t->w;
            break;
          case kAbs:
            c.slope -= t->w;
            bps_.emplace_back(t->p, 2.0 * t->w);
            break;
        }
      }
      c.bp_end = static_cast<int>(bps_.size());
      std::sort(bps_.begin() + c.bp_begin, bps_.begin() + c.bp_end);
      c.t = std::clamp(argmin(c), lo, std::max(lo, hi - c.width));
      while (!clumps_.empty()) {
        const Clump& prev = clumps_.back();
        if (prev.t + prev.width <= c.t + 1e-9) {
          break;
        }
        // Merge c behind prev: c's breakpoints shift by prev.width.
        merge_.clear();
        int a = prev.bp_begin;
        int b = c.bp_begin;
        while (a < prev.bp_end || b < c.bp_end) {
          if (b == c.bp_end
              || (a < prev.bp_end
                  && bps_[a].first <= bps_[b].first - prev.width)) {
            merge_.push_back(bps_[a++]);
          } else {
            merge_.emplace_back(bps_[b].first - prev.width, bps_[b].second);
            ++b;
          }
        }
        std::copy(merge_.begin(), merge_.end(), bps_.begin() + prev.bp_begin);
        c.first = prev.first;
        c.width += prev.width;
        c.slope += prev.slope;
        c.bp_begin = prev.bp_begin;
        c.bp_end = prev.bp_begin + static_cast<int>(merge_.size());
        clumps_.pop_back();
        c.t = std::clamp(argmin(c), lo, std::max(lo, hi - c.width));
      }
      bps_.resize(c.bp_end);
      clumps_.push_back(c);
    }
    double sum = 0.0;
    for (const Clump& c : clumps_) {
      double d = 0.0;
      for (int i = c.first; i <= c.last; ++i) {
        xs[i] = c.t + d;
        sum += eval(i, xs[i]);
        d += widths_[i];
      }
    }
    return sum;
  }

 private:
  enum Kind
  {
    kMax,  // w * max(p, x)
    kMin,  // -w * min(p, x)
    kLin,  // w * x
    kAbs   // w * |x - p|
  };
  struct Term
  {
    Kind kind;
    double w;
    double p;
  };
  struct Clump
  {
    int first;
    int last;  // inclusive
    double width;
    double t;      // left edge
    double slope;  // slope of the clump's cost at -infinity
    int bp_begin;  // its sorted breakpoints: bps_[bp_begin, bp_end)
    int bp_end;
  };

  void add(int i, const Term& term)
  {
    terms_[static_cast<size_t>(i) * cap_ + count_[i]++] = term;
  }

  // Minimizer of a clump's total cost over its left edge t;
  // +-infinity when unbounded.
  double argmin(const Clump& c) const
  {
    double slope = c.slope;
    if (slope >= 0.0) {
      return -std::numeric_limits<double>::infinity();
    }
    for (int k = c.bp_begin; k < c.bp_end; ++k) {
      slope += bps_[k].second;
      if (slope > 0.0) {
        return bps_[k].first;
      }
      if (slope == 0.0) {
        return k + 1 < c.bp_end ? 0.5 * (bps_[k].first + bps_[k + 1].first)
                                : bps_[k].first;
      }
    }
    return std::numeric_limits<double>::infinity();
  }

  int cap_ = 0;
  std::vector<Term> terms_;
  std::vector<int> count_;
  std::vector<int> widths_;
  std::vector<std::pair<double, double>> bps_;
  std::vector<std::pair<double, double>> merge_;
  std::vector<Clump> clumps_;
  double constant_ = 0.0;
};

// Site-aligned left edges from the continuous optimum, clump by clump
// (consecutive abutting cells move together) from left to right within
// a segment: the cheaper of the two site shifts around the optimum that
// keeps the clump right of its predecessor and leaves room for the
// cells after it.
void snapSegment(const RowClumper& clumper,
                 const std::vector<double>& cont,
                 const std::vector<int>& widths,
                 int begin,
                 int end,
                 int lo,
                 int hi,
                 const PlacementArea& area,
                 std::vector<int>& xs,
                 std::vector<char>& overflow)
{
  const int site = std::max(1, area.siteWidth());
  int64_t remaining = 0;
  for (int j = begin; j < end; ++j) {
    remaining += widths[j];
  }
  int prev_end = lo;
  int j = begin;
  while (j < end) {
    // The clump starting at j.
    int k = j;
    int64_t clump_width = widths[j];
    while (k + 1 < end
           && std::abs(cont[k + 1] - (cont[k] + widths[k])) < 1e-6) {
      ++k;
      clump_width += widths[k];
    }
    remaining -= clump_width;
    const int min_x = prev_end;
    const int64_t max_x64 = hi - remaining - clump_width;
    int floor_x = area.snapX(static_cast<int>(std::floor(cont[j])));
    if (floor_x > cont[j]) {
      floor_x -= site;
    }
    const int ceil_x = floor_x + site;
    int x;
    if (max_x64 < min_x) {
      // Does not fit: right-align, overlapping (reported).
      x = std::max(lo, area.snapX(static_cast<int>(hi - clump_width)));
      if (x + clump_width > hi && x - site >= lo) {
        x -= site;
      }
      for (int i = j; i <= k; ++i) {
        overflow[i] = 1;
      }
    } else {
      const int max_x = static_cast<int>(max_x64);
      const int a = std::clamp(floor_x, min_x, max_x);
      const int b = std::clamp(ceil_x, min_x, max_x);
      auto cost_at = [&](int start) {
        double sum = 0.0;
        double d = 0.0;
        for (int i = j; i <= k; ++i) {
          sum += clumper.eval(i, start + d);
          d += widths[i];
        }
        return sum;
      };
      const double ca = cost_at(a);
      const double cb = cost_at(b);
      x = (cb < ca - 1e-9
           || (std::abs(cb - ca) <= 1e-9
               && std::abs(b - cont[j]) < std::abs(a - cont[j])))
              ? b
              : a;
    }
    int d = 0;
    for (int i = j; i <= k; ++i) {
      xs[i] = x + d;
      d += widths[i];
    }
    prev_end = x + d;
    j = k + 1;
  }
}

// Shared by slotPositions(kOptimal) and the order search: the row
// problem for the cells of `set` (indices into inst.cells) in slot.
// The padding is reduced, in whole sites, to what the slot can afford
// for them (the legalizer does the same, by moving cells).  All
// buffers are sized once; relaxedBound() does not allocate.
class RowProblem
{
 public:
  RowProblem(const OptInstance& inst,
             const RowSlot& slot,
             const PlacementArea& area,
             const RowNets& nets,
             const std::vector<int>& set)
      : inst_(inst), slot_(slot), area_(area), nets_(nets)
  {
    const int site = std::max(1, area.siteWidth());
    int64_t width = 0;
    int pad_left = 0;
    int pad_right = 0;
    int max_terms = 1;
    for (int c : set) {
      width += inst.cells[c].width;
      pad_left = std::max(pad_left, inst.cells[c].pad_left);
      pad_right = std::max(pad_right, inst.cells[c].pad_right);
      max_terms
          = std::max(max_terms, 1 + 2 * static_cast<int>(nets.pins[c].size()));
    }
    const int64_t slack = slot.capacity() - width
                          - site * static_cast<int64_t>(slot.segments.size());
    const int64_t n = static_cast<int64_t>(set.size());
    // Largest uniform padding per side (in sites) the slack allows.
    const int64_t per_side = n > 0 && slack > 0 ? slack / (2 * n) / site : 0;
    pad_left_
        = static_cast<int>(std::min<int64_t>(pad_left / site, per_side)) * site;
    pad_right_ = static_cast<int>(std::min<int64_t>(pad_right / site, per_side))
                 * site;

    seg_lo_.resize(slot.segments.size());
    for (size_t s = 0; s < slot.segments.size(); ++s) {
      const int snapped = area.snapX(slot.segments[s].first);
      seg_lo_[s] = snapped < slot.segments[s].first ? snapped + site : snapped;
    }
    // Capacity of the segments after s (room for cells that follow the
    // prefix without crowding its segment).
    later_cap_.assign(slot.segments.size() + 1, 0);
    for (int s = static_cast<int>(slot.segments.size()) - 1; s >= 0; --s) {
      later_cap_[s] = later_cap_[s + 1]
                      + std::max(0, slot.segments[s].second - seg_lo_[s]);
    }
    total_footprint_ = 0;
    for (int c : set) {
      total_footprint_ += footprint(c);
    }
    const int cells = static_cast<int>(set.size());
    clumper_.reserve(cells, max_terms);
    widths_.resize(cells);
    seg_of_.resize(cells);
    overflow_.resize(cells);
    cont_.resize(cells);
    first_.assign(nets.m, -1);
    last_.resize(nets.m);
    off_l_.resize(nets.m);
    off_r_.resize(nets.m);
    touched_.reserve(nets.m);
    // Pin x offsets from the left edge of the padded footprint, in the
    // slot's orientation.
    pin_off_.assign(inst.cells.size(), {});
    for (int c : set) {
      const int half = pad_left_ + inst.cells[c].width / 2;
      pin_off_[c].reserve(nets.pins[c].size());
      for (const OptPin& pin : nets.pins[c]) {
        pin_off_[c].push_back(half
                              + orientOffset(pin.dx, pin.dy, slot.orient).x());
      }
    }
  }

  int padLeft() const { return pad_left_; }

  // Padded footprint of a cell (what the row must keep free for it).
  int footprint(int c) const
  {
    return inst_.cells[c].width + pad_left_ + pad_right_;
  }

  // Effective left edge of segment s (snapped up to a site).
  int segLo(int s) const { return seg_lo_[s]; }

  // Lower-left x of the cells of `order` under kOptimal.  The clumper
  // works on the padded footprints; the master sits pad_left inside.
  std::vector<int> positions(const std::vector<int>& order,
                             std::vector<char>* overflow_out)
  {
    const int n = static_cast<int>(order.size());
    prepare(order, n);
    const std::vector<int> packed = slotPositions(inst_, slot_, order, area_);
    for (int j = 0; j < n; ++j) {
      clumper_.setCell(j, widths_[j], packed[j] - pad_left_, true);
    }
    addNets(order, n, nullptr, nullptr, 0.0);

    std::vector<int> xs(n, 0);
    int begin = 0;
    while (begin < n) {
      int end = begin;
      while (end < n && seg_of_[end] == seg_of_[begin]) {
        ++end;
      }
      const int s = seg_of_[begin];
      const int lo = seg_lo_[s];
      const int hi = slot_.segments[s].second;
      clumper_.solve(begin, end, lo, hi, cont_.data());
      snapSegment(
          clumper_, cont_, widths_, begin, end, lo, hi, area_, xs, overflow_);
      begin = end;
    }
    for (int j = 0; j < n; ++j) {
      xs[j] += pad_left_;
    }
    if (overflow_out != nullptr) {
      *overflow_out = overflow_;
    }
    return xs;
  }

  // Lower bound on the horizontal HPWL of every completion of the
  // prefix `order[0..n)`: the exact optimum of the relaxed problem
  // (prefix cells only, free positions), where a net whose set pins are
  // not all placed must also reach next_x + lead (next_x: the leftmost
  // position the next cell can take).  `unplaced` and `lead` are per
  // local net.  The cells after the prefix need their footprints to its
  // right: whatever does not fit in the later segments shrinks the
  // room of the prefix's last segment.
  double relaxedBound(const std::vector<int>& order,
                      int n,
                      const std::vector<int>& unplaced,
                      const std::vector<int>& lead,
                      double next_x)
  {
    prepare(order, n);
    int64_t prefix_footprint = 0;
    for (int j = 0; j < n; ++j) {
      clumper_.setCell(j, widths_[j], 0.0, false);
      prefix_footprint += widths_[j];
    }
    double sum
        = clumper_.constant() + addNets(order, n, &unplaced, &lead, next_x);
    const int64_t remaining = total_footprint_ - prefix_footprint;
    int begin = 0;
    while (begin < n) {
      int end = begin;
      while (end < n && seg_of_[end] == seg_of_[begin]) {
        ++end;
      }
      const int s = seg_of_[begin];
      double hi = slot_.segments[s].second;
      if (end == n) {
        hi -= std::max<int64_t>(0, remaining - later_cap_[s + 1]);
      }
      sum += clumper_.solve(begin, end, seg_lo_[s], hi, cont_.data());
      begin = end;
    }
    return sum;
  }

 private:
  // Footprints and first-fit segments of order[0..n); resets the
  // clumper.
  void prepare(const std::vector<int>& order, int n)
  {
    const auto& segs = slot_.segments;
    int s = 0;
    int64_t used = 0;
    for (int j = 0; j < n; ++j) {
      widths_[j] = footprint(order[j]);
      while (s + 1 < static_cast<int>(segs.size())
             && seg_lo_[s] + used + widths_[j] > segs[s].second) {
        ++s;
        used = 0;
      }
      seg_of_[j] = s;
      overflow_[j] = seg_lo_[s] + used + widths_[j] > segs[s].second ? 1 : 0;
      used += widths_[j];
    }
    clumper_.reset(n);
  }

  // Adds the nets touched by order[0..n) to the clumper; returns the
  // horizontal HPWL of the relevant nets not touched by them (with the
  // unplaced-pin extension when requested).
  double addNets(const std::vector<int>& order,
                 int n,
                 const std::vector<int>* unplaced,
                 const std::vector<int>* lead,
                 double next_x)
  {
    const int m = nets_.m;
    touched_.clear();
    for (int j = 0; j < n; ++j) {
      const int c = order[j];
      const std::vector<OptPin>& pins = nets_.pins[c];
      const std::vector<int>& offs = pin_off_[c];
      for (size_t k = 0; k < pins.size(); ++k) {
        const int e = pins[k].cell;
        const int off = offs[k];
        if (first_[e] < 0) {
          first_[e] = last_[e] = j;
          off_l_[e] = off_r_[e] = off;
          touched_.push_back(e);
          continue;
        }
        if (first_[e] == j) {
          off_l_[e] = std::min(off_l_[e], off);
        }
        if (last_[e] != j) {
          last_[e] = j;
          off_r_[e] = off;
        } else {
          off_r_[e] = std::max(off_r_[e], off);
        }
      }
    }
    double untouched = 0.0;
    for (int e = 0; e < m; ++e) {
      const Box& base = nets_.base[e];
      bool has_hi = base.has;
      double x_hi = base.has ? base.x_hi : 0.0;
      if (unplaced != nullptr && (*unplaced)[e] > 0) {
        const double px = next_x + (*lead)[e];
        x_hi = has_hi ? std::max(x_hi, px) : px;
        has_hi = true;
      }
      if (first_[e] < 0) {
        if (base.has) {
          untouched += nets_.weight[e] * (x_hi - base.x_lo);
        }
        continue;
      }
      clumper_.addNet(has_hi,
                      x_hi,
                      base.has,
                      base.has ? base.x_lo : 0.0,
                      nets_.weight[e],
                      first_[e],
                      off_l_[e],
                      last_[e],
                      off_r_[e]);
    }
    for (int e : touched_) {
      first_[e] = -1;
    }
    return untouched;
  }

  const OptInstance& inst_;
  const RowSlot& slot_;
  const PlacementArea& area_;
  const RowNets& nets_;
  int pad_left_ = 0;
  int pad_right_ = 0;
  std::vector<int> seg_lo_;
  std::vector<int64_t> later_cap_;  // per segment: capacity of the later ones
  int64_t total_footprint_ = 0;
  // Scratch, sized once.
  RowClumper clumper_;
  std::vector<int> widths_;
  std::vector<int> seg_of_;
  std::vector<char> overflow_;
  std::vector<double> cont_;
  std::vector<int> first_, last_, off_l_, off_r_;
  std::vector<int> touched_;
  std::vector<std::vector<int>> pin_off_;  // indexed like inst.cells
};

}  // namespace

std::vector<int> slotPositions(const OptInstance& inst,
                               const RowSlot& slot,
                               const std::vector<int>& order,
                               const PlacementArea& area,
                               PositionRule rule,
                               const std::vector<odb::Point>& centers,
                               const std::vector<odb::dbOrientType>& orients,
                               std::vector<char>* overflow)
{
  if (rule == PositionRule::kEven || order.empty()) {
    return slotPositions(inst, slot, order, area, overflow);
  }
  RowNets nets;
  nets.build(inst, order, centers, orients);
  RowProblem problem(inst, slot, area, nets, order);
  return problem.positions(order, overflow);
}

namespace {

// Depth-first branch and bound over the orderings of a row.
class OrderSearch
{
 public:
  OrderSearch(const OptInstance& inst,
              const RowSlot& slot,
              const PlacementArea& area,
              const std::vector<odb::Point>& centers,
              const std::vector<odb::dbOrientType>& orients,
              PositionRule rule,
              const std::vector<int>& incumbent,
              int64_t budget)
      : inst_(inst),
        slot_(slot),
        area_(area),
        rule_(rule),
        incumbent_(incumbent),
        budget_(budget)
  {
    n_ = static_cast<int>(incumbent.size());
    nets_.build(inst, incumbent, centers, orients);
    m_ = nets_.m;
    // Lower bound helpers: every pin of a set cell that is still
    // unplaced ends up at x >= next position + lead, where lead is the
    // smallest (half width + oriented dx) over the net's set pins.
    // (Under kOptimal the positions refer to the padded footprint, so
    // the lead includes the left padding.)
    if (rule_ != PositionRule::kEven) {
      problem_
          = std::make_unique<RowProblem>(inst, slot, area, nets_, incumbent);
    }
    const int pad = problem_ ? problem_->padLeft() : 0;
    unplaced_.assign(m_, 0);
    lead_.assign(m_, std::numeric_limits<int>::max());
    for (int c : incumbent) {
      for (const OptPin& pin : nets_.pins[c]) {
        ++unplaced_[pin.cell];
        const int dx = orientOffset(pin.dx, pin.dy, slot.orient).x();
        lead_[pin.cell]
            = std::min(lead_[pin.cell], pad + inst.cells[c].width / 2 + dx);
      }
    }
    int64_t total = 0;
    for (int c : incumbent) {
      total += inst.cells[c].width;
    }
    slack_ = slotSlack(slot, total, n_);
    boxes_.assign(static_cast<size_t>(n_ + 1) * m_, Box());
    std::copy(nets_.base.begin(), nets_.base.end(), boxes_.begin());
    current_.assign(n_, -1);
    used_.assign(inst.cells.size(), 0);
    if (rule_ != PositionRule::kEven) {
      // The vertical part of the HPWL does not depend on the order:
      // every set cell sits in this row.
      std::vector<Box> boxes = nets_.base;
      for (int c : incumbent) {
        const int cy = slot.y_lo + inst.cells[c].height / 2;
        for (const OptPin& pin : nets_.pins[c]) {
          const odb::Point off = orientOffset(pin.dx, pin.dy, slot.orient);
          Box& box = boxes[pin.cell];
          if (box.has) {
            box.y_lo = std::min(box.y_lo, cy + off.y());
            box.y_hi = std::max(box.y_hi, cy + off.y());
          } else {
            box.add(0, cy + off.y());
          }
        }
      }
      vertical_ = 0.0;
      for (int e = 0; e < m_; ++e) {
        vertical_ += nets_.weight[e] * boxes[e].height();
      }
    }
  }

  // Lower-left x of the cells of a complete ordering under the rule.
  std::vector<int> positions(const std::vector<int>& order) const
  {
    if (rule_ != PositionRule::kEven) {
      return problem_->positions(order, nullptr);
    }
    return slotPositions(inst_, slot_, order, area_);
  }

  // Cost of a complete ordering under the rule.
  double evaluate(const std::vector<int>& order) const
  {
    std::vector<Box> boxes = nets_.base;
    const std::vector<int> xs = positions(order);
    for (size_t i = 0; i < order.size(); ++i) {
      const int c = order[i];
      const odb::Point center(xs[i] + inst_.cells[c].width / 2,
                              slot_.y_lo + inst_.cells[c].height / 2);
      for (const OptPin& pin : nets_.pins[c]) {
        const odb::Point p = pinAt(center, pin, slot_.orient);
        boxes[pin.cell].add(p.x(), p.y());
      }
    }
    double sum = 0.0;
    for (int e = 0; e < m_; ++e) {
      sum += nets_.weight[e] * boxes[e].hpwl();
    }
    return sum;
  }

  // Runs the search below `bound`; returns true when an ordering cheaper
  // than bound was found (in best()).
  bool run(double bound, SearchStats& stats)
  {
    best_cost_ = bound;
    found_ = false;
    aborted_ = false;
    nodes_ = 0;
    if (m_ == 0) {
      return false;
    }
    if (rule_ != PositionRule::kEven) {
      dfsOptimal(0, 0, 0);
    } else {
      double cost0 = 0.0;
      for (int e = 0; e < m_; ++e) {
        cost0 += nets_.weight[e] * nets_.base[e].hpwl();
      }
      dfs(0, Packer(slot_, area_, slack_), cost0);
    }
    stats.nodes += nodes_;
    if (aborted_) {
      stats.complete = false;
    }
    return found_;
  }

  const std::vector<int>& best() const { return best_order_; }
  double bestCost() const { return best_cost_; }

 private:
  // Packing rule: positions of the prefix are final, so the cost is
  // incremental.
  void dfs(int depth, const Packer& packer, double cost)
  {
    if (depth == n_) {
      if (cost < best_cost_ - kEps) {
        best_cost_ = cost;
        best_order_ = current_;
        found_ = true;
      }
      return;
    }
    const Box* from = boxes_.data() + static_cast<size_t>(depth) * m_;
    Box* to = boxes_.data() + static_cast<size_t>(depth + 1) * m_;
    for (int c : incumbent_) {
      if (used_[c]) {
        continue;
      }
      if (++nodes_ > budget_) {
        aborted_ = true;
        return;
      }
      const OptCell& cell = inst_.cells[c];
      Packer next_packer = packer;
      bool overflow = false;
      const int x = next_packer.place(cell.width, overflow);
      const odb::Point center(x + cell.width / 2, slot_.y_lo + cell.height / 2);
      std::copy(from, from + m_, to);
      double next_cost = cost;
      for (const OptPin& pin : nets_.pins[c]) {
        const odb::Point p = pinAt(center, pin, slot_.orient);
        Box& box = to[pin.cell];
        const double before = box.hpwl();
        box.add(p.x(), p.y());
        next_cost += nets_.weight[pin.cell] * (box.hpwl() - before);
        --unplaced_[pin.cell];
      }
      bool prune = next_cost >= best_cost_ - kEps;
      if (!prune && depth + 1 < n_) {
        // Nets with unplaced set pins must reach at least the leftmost
        // position those pins can still take.
        const int next_x = next_packer.nextMin();
        double bound = next_cost;
        for (int e = 0; e < m_ && bound < best_cost_ - kEps; ++e) {
          if (unplaced_[e] > 0 && to[e].has) {
            const int px = next_x + lead_[e];
            if (px > to[e].x_hi) {
              bound += nets_.weight[e] * (px - to[e].x_hi);
            }
          }
        }
        prune = bound >= best_cost_ - kEps;
      }
      if (!prune) {
        used_[c] = 1;
        current_[depth] = c;
        dfs(depth + 1, next_packer, next_cost);
        used_[c] = 0;
      }
      for (const OptPin& pin : nets_.pins[c]) {
        ++unplaced_[pin.cell];
      }
      if (aborted_) {
        return;
      }
    }
  }

  // Optimal positions: the prefix's positions move as cells are added,
  // so every node solves the relaxed prefix problem for its bound and
  // every leaf is evaluated with the snapped positions.  seg / used
  // track the first-fit segment of the next cell.
  void dfsOptimal(int depth, int seg, int64_t used)
  {
    if (depth == n_) {
      const double cost = evaluate(current_);
      if (cost < best_cost_ - kEps) {
        best_cost_ = cost;
        best_order_ = current_;
        found_ = true;
      }
      return;
    }
    const auto& segs = slot_.segments;
    for (int c : incumbent_) {
      if (used_[c]) {
        continue;
      }
      if (++nodes_ > budget_) {
        aborted_ = true;
        return;
      }
      const int width = problem_->footprint(c);
      int next_seg = seg;
      int64_t next_used = used;
      while (next_seg + 1 < static_cast<int>(segs.size())
             && problem_->segLo(next_seg) + next_used + width
                    > segs[next_seg].second) {
        ++next_seg;
        next_used = 0;
      }
      next_used += width;
      current_[depth] = c;
      for (const OptPin& pin : nets_.pins[c]) {
        --unplaced_[pin.cell];
      }
      // The relaxed optimum of the prefix bounds its completions; for
      // the full order it bounds the snapped evaluation.
      const double next_x = problem_->segLo(next_seg) + next_used;
      const double bound = problem_->relaxedBound(
                               current_, depth + 1, unplaced_, lead_, next_x)
                           + vertical_;
      const bool prune = bound >= best_cost_ - kEps;
      if (!prune) {
        used_[c] = 1;
        dfsOptimal(depth + 1, next_seg, next_used);
        used_[c] = 0;
      }
      for (const OptPin& pin : nets_.pins[c]) {
        ++unplaced_[pin.cell];
      }
      if (aborted_) {
        return;
      }
    }
  }

  const OptInstance& inst_;
  const RowSlot& slot_;
  const PlacementArea& area_;
  PositionRule rule_;
  const std::vector<int>& incumbent_;
  int64_t budget_;
  int n_ = 0;
  int m_ = 0;
  RowNets nets_;
  std::vector<int> unplaced_;  // per net: set pins not yet placed
  std::vector<int>
      lead_;  // per net: smallest pin x relative to a cell's left edge
  double slack_ = 0.0;
  std::vector<Box> boxes_;
  std::vector<int> current_;
  std::vector<char> used_;
  std::unique_ptr<RowProblem> problem_;
  double vertical_ = 0.0;
  double best_cost_ = 0.0;
  std::vector<int> best_order_;
  bool found_ = false;
  bool aborted_ = false;
  int64_t nodes_ = 0;
};

}  // namespace

bool solveRowOrder(const OptInstance& inst,
                   const RowSlot& slot,
                   const PlacementArea& area,
                   const std::vector<odb::Point>& centers,
                   const std::vector<odb::dbOrientType>& orients,
                   PositionRule rule,
                   std::vector<int>& order,
                   int64_t node_budget,
                   double& best_cost,
                   SearchStats& stats)
{
  if (order.size() < 2) {
    OrderSearch search(inst, slot, area, centers, orients, rule, order, 0);
    best_cost = search.evaluate(order);
    return false;
  }
  OrderSearch search(
      inst, slot, area, centers, orients, rule, order, node_budget);
  best_cost = search.evaluate(order);
  bool improved = false;
  if (rule != PositionRule::kEven) {
    // A node of the joint search costs about ten times a node of the
    // packing-rule search (the prefix's positions are not final, so its
    // bound is a clumping solve), and the two searches visit similar
    // numbers of nodes.  The packing-rule order, clumped, is within a
    // fraction of a percent of the joint optimum: kOptimal stops there,
    // kJoint continues from it.
    OrderSearch packed(inst,
                       slot,
                       area,
                       centers,
                       orients,
                       PositionRule::kEven,
                       order,
                       node_budget);
    if (packed.run(packed.evaluate(order), stats)) {
      const double cost = search.evaluate(packed.best());
      if (cost < best_cost - kEps) {
        best_cost = cost;
        order = packed.best();
        improved = true;
      }
    }
    if (rule == PositionRule::kOptimal) {
      return improved;
    }
  }
  if (search.run(best_cost, stats)) {
    best_cost = search.bestCost();
    order = search.best();
    improved = true;
  }
  return improved;
}

namespace {

class RowSearch
{
 public:
  RowSearch(const OptInstance& inst,
            const std::vector<OptRow>& rows,
            const std::vector<int>& cells,
            const std::vector<int>& incumbent,
            int64_t budget)
      : inst_(inst), rows_(rows), cells_(cells), budget_(budget)
  {
    n_ = static_cast<int>(cells.size());
    r_ = static_cast<int>(rows.size());
    std::vector<char> in_set(inst.cells.size(), 0);
    for (int c : cells) {
      in_set[c] = 1;
    }
    std::vector<int> net_local(inst.nets.size(), -1);
    for (size_t e = 0; e < inst.nets.size(); ++e) {
      const OptNet& net = inst.nets[e];
      bool touches = false;
      Box base;
      if (net.has_external) {
        base.add(net.external);
      }
      for (const OptPin& pin : net.pins) {
        if (in_set[pin.cell]) {
          touches = true;
        }
        // Cells outside the set are expected to be absent (the end case
        // assigns every cell of the leaf); they are ignored otherwise.
      }
      if (!touches) {
        continue;
      }
      net_local[e] = static_cast<int>(base_.size());
      base_.push_back(base);
      weight_.push_back(net.weight);
    }
    m_ = static_cast<int>(base_.size());
    pins_.assign(inst.cells.size(), {});
    for (size_t e = 0; e < inst.nets.size(); ++e) {
      if (net_local[e] < 0) {
        continue;
      }
      for (const OptPin& pin : inst.nets[e].pins) {
        if (in_set[pin.cell]) {
          pins_[pin.cell].push_back({net_local[e], pin.dx, pin.dy});
        }
      }
    }
    // Branch on the most connected cells first; try the incumbent row
    // first, then rows by distance from the cell's external pull.
    order_ = cells;
    std::sort(order_.begin(), order_.end(), [&](int a, int b) {
      if (pins_[a].size() != pins_[b].size()) {
        return pins_[a].size() > pins_[b].size();
      }
      return a < b;
    });
    row_order_.assign(inst.cells.size(), {});
    for (int c : cells) {
      double sy = 0.0;
      int cnt = 0;
      for (const OptPin& pin : pins_[c]) {
        const Box& b = base_[pin.cell];
        if (b.has) {
          sy += 0.5 * (b.y_lo + b.y_hi);
          ++cnt;
        }
      }
      const double pull = cnt > 0 ? sy / cnt : 0.0;
      std::vector<int>& ro = row_order_[c];
      for (int i = 0; i < r_; ++i) {
        ro.push_back(i);
      }
      std::sort(ro.begin(), ro.end(), [&](int a, int b) {
        if (a == incumbent[c]) {
          return true;
        }
        if (b == incumbent[c]) {
          return false;
        }
        const double da = std::abs(rows[a].y_lo + rows[a].height / 2.0 - pull);
        const double db = std::abs(rows[b].y_lo + rows[b].height / 2.0 - pull);
        if (da != db) {
          return da < db;
        }
        return a < b;
      });
    }
    boxes_.assign(static_cast<size_t>(n_ + 1) * m_, Box());
    std::copy(base_.begin(), base_.end(), boxes_.begin());
    load_.assign(r_, 0);
    current_.assign(inst.cells.size(), -1);
    remaining_.assign(n_ + 1, 0);
    for (int i = n_ - 1; i >= 0; --i) {
      remaining_[i] = remaining_[i + 1] + footprint(order_[i]);
    }
    // Lower bound helper: the pins of the cells assigned after depth d
    // on every net, with the y every row would give them.
    net_pins_.assign(m_, {});
    for (int d = 0; d < n_; ++d) {
      const int c = order_[d];
      for (const OptPin& pin : pins_[c]) {
        std::vector<int> ys(r_);
        for (int i = 0; i < r_; ++i) {
          ys[i] = rows[i].y_lo + inst.cells[c].height / 2
                  + orientOffset(pin.dx, pin.dy, rows[i].orient).y();
        }
        net_pins_[pin.cell].push_back({d, std::move(ys)});
      }
    }
  }

  // Lower bound on the final cost given the boxes after depth d: every
  // net must still reach the pins of the cells assigned later; the
  // largest single required extension is a valid bound.
  double bound(int depth, const Box* boxes, double cost) const
  {
    for (int e = 0; e < m_; ++e) {
      const Box& box = boxes[e];
      if (!box.has) {
        continue;
      }
      double need = 0.0;
      for (const auto& [d, ys] : net_pins_[e]) {
        if (d <= depth) {
          continue;
        }
        double nearest = std::numeric_limits<double>::infinity();
        for (int y : ys) {
          const double dist = y < box.y_lo   ? box.y_lo - y
                              : y > box.y_hi ? y - box.y_hi
                                             : 0.0;
          nearest = std::min(nearest, dist);
        }
        need = std::max(need, nearest);
      }
      cost += weight_[e] * need;
    }
    return cost;
  }

  // Row load of a cell: its width plus the placement padding.
  int footprint(int c) const
  {
    const OptCell& cell = inst_.cells[c];
    return cell.width + cell.pad_left + cell.pad_right;
  }

  double evaluate(const std::vector<int>& row_of, bool& feasible) const
  {
    std::vector<Box> boxes = base_;
    std::vector<int64_t> load(r_, 0);
    for (int c : cells_) {
      const OptRow& row = rows_[row_of[c]];
      load[row_of[c]] += footprint(c);
      const int cy = row.y_lo + inst_.cells[c].height / 2;
      for (const OptPin& pin : pins_[c]) {
        const odb::Point off = orientOffset(pin.dx, pin.dy, row.orient);
        boxes[pin.cell].add(0, cy + off.y());
      }
    }
    feasible = true;
    for (int i = 0; i < r_; ++i) {
      if (load[i] > rows_[i].capacity) {
        feasible = false;
      }
    }
    double sum = 0.0;
    for (int e = 0; e < m_; ++e) {
      sum += weight_[e] * boxes[e].height();
    }
    return sum;
  }

  bool run(double bound, SearchStats& stats)
  {
    best_cost_ = bound;
    found_ = false;
    aborted_ = false;
    nodes_ = 0;
    if (m_ == 0) {
      return false;
    }
    double cost0 = 0.0;
    for (int e = 0; e < m_; ++e) {
      cost0 += weight_[e] * base_[e].height();
    }
    dfs(0, cost0);
    stats.nodes += nodes_;
    if (aborted_) {
      stats.complete = false;
    }
    return found_;
  }

  const std::vector<int>& best() const { return best_; }

 private:
  void dfs(int depth, double cost)
  {
    if (depth == n_) {
      if (cost < best_cost_ - kEps) {
        best_cost_ = cost;
        best_ = current_;
        found_ = true;
      }
      return;
    }
    const int c = order_[depth];
    const OptCell& cell = inst_.cells[c];
    const int width = footprint(c);
    const Box* from = boxes_.data() + static_cast<size_t>(depth) * m_;
    Box* to = boxes_.data() + static_cast<size_t>(depth + 1) * m_;
    for (int r : row_order_[c]) {
      if (load_[r] + width > rows_[r].capacity) {
        continue;
      }
      if (++nodes_ > budget_) {
        aborted_ = true;
        return;
      }
      // The remaining cells must still fit somewhere.
      int64_t room = 0;
      for (int i = 0; i < r_; ++i) {
        room += rows_[i].capacity - load_[i] - (i == r ? width : 0);
      }
      if (room < remaining_[depth + 1]) {
        continue;
      }
      const OptRow& row = rows_[r];
      const int cy = row.y_lo + cell.height / 2;
      std::copy(from, from + m_, to);
      double next_cost = cost;
      for (const OptPin& pin : pins_[c]) {
        const odb::Point off = orientOffset(pin.dx, pin.dy, row.orient);
        Box& box = to[pin.cell];
        const double before = box.height();
        box.add(0, cy + off.y());
        next_cost += weight_[pin.cell] * (box.height() - before);
      }
      if (next_cost >= best_cost_ - kEps
          || bound(depth, to, next_cost) >= best_cost_ - kEps) {
        continue;
      }
      load_[r] += width;
      current_[c] = r;
      dfs(depth + 1, next_cost);
      load_[r] -= width;
      if (aborted_) {
        return;
      }
    }
  }

  const OptInstance& inst_;
  const std::vector<OptRow>& rows_;
  const std::vector<int>& cells_;
  int64_t budget_;
  int n_ = 0;
  int r_ = 0;
  int m_ = 0;
  std::vector<Box> base_;
  std::vector<float> weight_;
  std::vector<std::vector<OptPin>> pins_;
  std::vector<int> order_;
  std::vector<std::vector<int>> row_order_;
  std::vector<Box> boxes_;
  std::vector<int64_t> load_;
  std::vector<int> current_;
  std::vector<int64_t> remaining_;
  // Per net: (depth of the cell, its pin y in every row).
  std::vector<std::vector<std::pair<int, std::vector<int>>>> net_pins_;
  double best_cost_ = 0.0;
  std::vector<int> best_;
  bool found_ = false;
  bool aborted_ = false;
  int64_t nodes_ = 0;
};

}  // namespace

bool solveRowAssignment(const OptInstance& inst,
                        const std::vector<OptRow>& rows,
                        const std::vector<int>& cells,
                        std::vector<int>& row_of,
                        int64_t node_budget,
                        SearchStats& stats)
{
  if (rows.size() < 2 || cells.size() < 2) {
    return false;
  }
  RowSearch search(inst, rows, cells, row_of, node_budget);
  bool feasible = false;
  const double incumbent = search.evaluate(row_of, feasible);
  const double bound
      = feasible ? incumbent : std::numeric_limits<double>::infinity();
  if (search.run(bound, stats)) {
    for (int c : cells) {
      row_of[c] = search.best()[c];
    }
    return true;
  }
  return false;
}

}  // namespace mcp
