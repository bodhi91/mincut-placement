// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "Ironing.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <vector>

#include "NetlistModel.h"
#include "Optimal.h"
#include "PlacementArea.h"
#include "odb/db.h"
#include "utl/Logger.h"

namespace mcp {

namespace {

constexpr int64_t kNodeBudget = 100000;
constexpr double kEps = 1e-3;

struct Cell
{
  int vertex;
  int x;  // lower-left
  int width;
  int height;
};

}  // namespace

RowIroning::RowIroning(const NetlistModel& netlist,
                       const PlacementArea& area,
                       PositionRule positions,
                       int pad_left,
                       int pad_right,
                       utl::Logger* logger)
    : netlist_(netlist),
      area_(area),
      positions_(positions),
      pad_left_(pad_left),
      pad_right_(pad_right),
      logger_(logger)
{
}

void RowIroning::run(int window,
                     int passes,
                     std::vector<odb::Point>& loc,
                     IroningStats& stats) const
{
  const auto start = std::chrono::steady_clock::now();
  if (window < 2 || passes < 1) {
    return;
  }

  // Free segments of the core, and the placed cells of every segment.
  const std::vector<FreeSegment> segments = area_.freeSegments(area_.core());
  std::map<int, std::vector<int>> segments_at_y;  // y_lo -> segment ids
  for (size_t s = 0; s < segments.size(); ++s) {
    segments_at_y[segments[s].y_lo].push_back(static_cast<int>(s));
  }
  std::vector<std::vector<Cell>> cells(segments.size());
  for (int v : netlist_.movableVertices()) {
    odb::dbInst* inst = netlist_.vertex(v).inst;
    if (inst == nullptr || !inst->isPlaced()) {
      continue;
    }
    int x = 0;
    int y = 0;
    inst->getLocation(x, y);
    const int width = static_cast<int>(inst->getMaster()->getWidth());
    const int height = static_cast<int>(inst->getMaster()->getHeight());
    auto it = segments_at_y.find(y);
    if (it == segments_at_y.end()) {
      continue;  // not on a row boundary: left alone
    }
    for (int s : it->second) {
      const FreeSegment& seg = segments[s];
      if (x >= seg.x_lo && x + width <= seg.x_hi) {
        cells[s].push_back({v, x, width, height});
        break;
      }
    }
  }
  for (auto& list : cells) {
    std::sort(list.begin(), list.end(), [](const Cell& a, const Cell& b) {
      return a.x != b.x ? a.x < b.x : a.vertex < b.vertex;
    });
  }

  const int stride = std::max(1, window / 2);
  for (int pass = 0; pass < passes; ++pass) {
    ++stats.passes;
    double pass_gain = 0.0;
    for (size_t s = 0; s < segments.size(); ++s) {
      std::vector<Cell>& list = cells[s];
      const int n = static_cast<int>(list.size());
      if (n < 2) {
        continue;
      }
      const FreeSegment& seg = segments[s];
      for (int begin = 0; begin < n; begin += stride) {
        const int end = std::min(n, begin + window);
        if (end - begin < 2) {
          break;
        }
        // The window's free span; with optimal positions the padded footprints
        // must also clear the neighbors' padding.
        const bool padded = positions_ != PositionRule::kEven;
        RowSlot slot;
        slot.segments.emplace_back(
            begin > 0 ? list[begin - 1].x + list[begin - 1].width
                            + (padded ? pad_right_ : 0)
                      : seg.x_lo,
            end < n ? list[end].x - (padded ? pad_left_ : 0) : seg.x_hi);
        if (slot.segments[0].second <= slot.segments[0].first) {
          if (end == n) {
            break;
          }
          continue;
        }
        slot.y_lo = seg.y_lo;
        slot.orient = seg.orient;

        const int m = end - begin;
        std::vector<int> vertices(m);
        std::vector<odb::Point> centers(m);
        std::vector<odb::dbOrientType> orients(m, seg.orient);
        std::vector<int> order(m);
        for (int i = 0; i < m; ++i) {
          vertices[i] = list[begin + i].vertex;
          centers[i] = loc[vertices[i]];
          order[i] = i;
        }
        OptInstance inst;
        inst.build(netlist_,
                   vertices,
                   loc,
                   padded ? pad_left_ : 0,
                   padded ? pad_right_ : 0);
        ++stats.windows;
        if (inst.nets.empty()) {
          if (end == n) {
            break;
          }
          continue;
        }
        const double actual = inst.cost(centers, orients);
        SearchStats search;
        double best = 0.0;
        solveRowOrder(inst,
                      slot,
                      area_,
                      centers,
                      orients,
                      positions_,
                      order,
                      kNodeBudget,
                      best,
                      search);
        stats.nodes += search.nodes;
        if (!search.complete) {
          ++stats.incomplete;
        }
        if (best < actual - kEps) {
          const std::vector<int> xs = slotPositions(
              inst, slot, order, area_, positions_, centers, orients);
          for (int j = 0; j < m; ++j) {
            Cell& cell = list[begin + order[j]];
            cell.x = xs[j];
            odb::dbInst* db_inst = netlist_.vertex(cell.vertex).inst;
            db_inst->setOrient(seg.orient);
            db_inst->setLocation(cell.x, seg.y_lo);
            loc[cell.vertex] = db_inst->getBBox()->getBox().center();
          }
          std::sort(list.begin() + begin,
                    list.begin() + end,
                    [](const Cell& a, const Cell& b) {
                      return a.x != b.x ? a.x < b.x : a.vertex < b.vertex;
                    });
          ++stats.improved;
          pass_gain += actual - best;
        }
        if (end == n) {
          break;
        }
      }
    }
    stats.gain += pass_gain;
    debugPrint(logger_,
               utl::MCP,
               "ironing",
               1,
               "Pass {}: {} windows so far, {} improved, gain {:.0f} dbu",
               pass,
               stats.windows,
               stats.improved,
               pass_gain);
    if (pass_gain <= kEps) {
      break;
    }
  }
  stats.seconds
      = std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count();
}

}  // namespace mcp
