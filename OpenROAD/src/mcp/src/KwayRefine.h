// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

// X1b: whole-level k-way refinement.  After a level's bisections are
// committed, the assignment of all movable cells to the k regions of the
// new level is refined as one k-way mapping problem (Mt-KaHyPar's
// Steiner-tree process mapping, V-cycles from the tree's assignment):
// the regions are the nodes of the target graph, adjacent regions are
// connected with their center distance, every region keeps the cell area
// the tree gave it (times a small slack), and the I/O pins, fixed cells
// and the cells of leaves already decided are fixed to their regions.  A
// cell may thereby move between any two regions of the level, not only
// across the last cut -- the irrevocability of recursive bisection
// (E8) relaxed one level at a time.  X0 measured -2 % (k = 4) to -4 %
// (k = 12-16) of the level's HPWL-at-centers on ibex / aes, fading at
// k = 64.
//
// The Steiner metric places a fixed pin at the center of its region, so
// the caller must accept the result only if its own HPWL evaluator
// improves (X0 finding 4).

#pragma once

#include <cstdint>
#include <vector>

#include "odb/geom.h"

namespace utl {
class Logger;
}

namespace mcp {

class NetlistModel;
class PlacementArea;
class RegionTree;

struct KwayRefineStats
{
  int level = -1;
  int k = 0;
  int moved = 0;  // cells whose region changed
  // Mt-KaHyPar's Steiner-tree objective before / after (site units).
  int64_t steiner_before = 0;
  int64_t steiner_after = 0;
  double seconds = 0.0;
  bool ran = false;
};

class KwayRefiner
{
 public:
  KwayRefiner(const NetlistModel& netlist,
              const PlacementArea& area,
              utl::Logger* logger);

  // Whether this build has Mt-KaHyPar.
  static bool available();

  // blocks: the regions of the level (every movable cell not in one of
  // them is treated as fixed at loc[v]).  region_of[v]: region of every
  // movable cell (-1 for fixed vertices and pins); loc: the location of
  // every vertex.  slack: capacity of a block relative to its current
  // cell area.  from_scratch (X1a): the mapping is constructed by
  // Mt-KaHyPar's multilevel process mapping instead of refined from the
  // tree's assignment (the tree still supplies boxes and loads).  On
  // success new_region_of holds the region of every cell of the blocks
  // (unchanged cells included) and the return value is true.
  bool refine(const std::vector<int>& blocks,
              const RegionTree& tree,
              const std::vector<int>& region_of,
              const std::vector<odb::Point>& loc,
              double slack,
              int vcycles,
              int threads,
              unsigned seed,
              bool from_scratch,
              std::vector<int>& new_region_of,
              KwayRefineStats& stats) const;

 private:
  const NetlistModel& netlist_;
  const PlacementArea& area_;
  utl::Logger* logger_;
};

}  // namespace mcp
