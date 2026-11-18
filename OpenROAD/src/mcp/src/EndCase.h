// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

// End-case placement of a leaf region.  A heuristic layout first: cells
// are ordered by the centroid of their external connections and packed
// row by row into the free segments of the region.  Leaves with at most
// optimal_cells cells are then improved by branch and bound (Optimal.h):
// the row assignment for the vertical HPWL and the ordering within every
// row for the full HPWL, each starting from the heuristic and kept only
// when cheaper.  The result is site-aligned but may overlap when the
// region is overfull; detailed placement legalizes.

#pragma once

#include <cstdint>
#include <vector>

#include "Optimal.h"
#include "odb/dbTypes.h"
#include "odb/geom.h"

namespace utl {
class Logger;
}

namespace mcp {

class NetlistModel;
class PlacementArea;
struct Region;

struct EndCaseStats
{
  int regions = 0;
  int cells = 0;
  int overflow_cells = 0;  // did not fit; left at the region center
  // Branch-and-bound stage: leaves it ran on, leaves where it beat the
  // heuristic, leaves where a search hit its node budget, and the HPWL
  // it saved over the heuristic layouts (dbu, instance model).
  int optimal_regions = 0;
  int optimal_improved = 0;
  int optimal_incomplete = 0;
  int64_t optimal_nodes = 0;
  double optimal_gain = 0.0;
  // Feedback passes (place() with refine): leaves revisited, leaves
  // rewritten, and the HPWL saved (dbu, instance model).
  int refine_passes = 0;
  int refine_regions = 0;
  int refine_improved = 0;
  double refine_gain = 0.0;
  // Diagnostics over the leaves the instance model was built for: rows
  // available in the leaf, rows holding a cell in the final layout and
  // (seeded) in the seed; the instance cost of the final layout and
  // (seeded) of the seed positions clamped to the region (dbu).
  int64_t rows_available = 0;
  int64_t rows_used = 0;
  int64_t seed_rows_used = 0;
  double final_cost = 0.0;
  double seed_cost = 0.0;
};

class EndCase
{
 public:
  struct Item;
  struct Row;
  struct Layout;

  // With use_seed the cells are ordered by their seed location
  // (Vertex::loc, clamped to the region) instead of the centroid of
  // their connections.  optimal_cells <= 0 disables the branch-and-bound
  // stage; positions is the position rule of that stage (the heuristic
  // layout always packs with even slack) and pad_left / pad_right (dbu)
  // the placement padding it keeps between cells.  optimal_rows false
  // skips the row-assignment search of that stage (orders only).
  EndCase(const NetlistModel& netlist,
          const PlacementArea& area,
          bool use_seed,
          int optimal_cells,
          bool optimal_rows,
          PositionRule positions,
          int pad_left,
          int pad_right,
          utl::Logger* logger);

  // Computes a location (lower-left corner) and orientation for every
  // cell of the region and writes them to the database.  loc is
  // updated with the new centers.
  // use_seed overrides the constructor's flag for this leaf (-1: keep).
  // With refine the leaf has been placed before: the current layout
  // (read from the database) is the incumbent and the cells of the
  // other leaves are at loc[]; the leaf is rewritten only when a
  // cheaper layout is found.  Returns true when the layout changed.
  bool place(const Region& region,
             std::vector<odb::Point>& loc,
             EndCaseStats& stats,
             int use_seed = -1,
             bool refine = false) const;

 private:
  static RowSlot slotOf(const Row& row);
  // Packs the items of `order` into the row's free segments; fills x
  // and orientation per item and returns the items that did not fit.
  std::vector<int> packRow(const Row& row,
                           const std::vector<int>& order,
                           const std::vector<Item>& items,
                           std::vector<int>& xs,
                           std::vector<odb::dbOrientType>& orients) const;
  // Branch-and-bound improvement of the incumbent layout; returns true
  // and overwrites layout when a cheaper one was found.
  bool optimize(const OptInstance& inst,
                const std::vector<Item>& items,
                const std::vector<Row>& rows,
                Layout& layout,
                EndCaseStats& stats) const;
  // The region's cells with their pull points (seed: their seed
  // location clamped to the region).
  std::vector<Item> buildItems(const Region& region,
                               const std::vector<odb::Point>& loc,
                               bool seed) const;
  // The region's rows with their free segments (bottom to top).
  std::vector<Row> buildRows(const Region& region) const;
  // The heuristic layout: cells dealt to rows by pull y in proportion
  // to the rows' capacities, ordered by pull x and packed.
  Layout heuristicLayout(const std::vector<Item>& items,
                         const std::vector<Row>& rows) const;
  // The layout the database currently holds for the items.
  Layout currentLayout(const std::vector<Item>& items,
                       const std::vector<Row>& rows) const;

 public:
  // Accumulates the diagnostics of EndCaseStats for a placed leaf.
  void diagnose(const Region& region,
                const std::vector<odb::Point>& loc,
                EndCaseStats& stats,
                bool seeded) const;

 private:
  const NetlistModel& netlist_;
  const PlacementArea& area_;
  bool use_seed_;
  int optimal_cells_;
  bool optimal_rows_;
  PositionRule positions_;
  int pad_left_;
  int pad_right_;
  utl::Logger* logger_;
};

}  // namespace mcp
