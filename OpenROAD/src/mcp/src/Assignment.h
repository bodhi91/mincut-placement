// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

// Geometric assignment of a region's cells to its children: minimize
// sum_i weight_i * cost_iq subject to per-child bounds on the assigned
// weight.  This is a transportation problem (cells x k children) and
// is solved exactly in its fractional form by min-cost flow
// (lemon::NetworkSimplex); a basic optimal solution splits at most
// k - 1 cells, which are rounded to their largest share, followed by a
// greedy repair of the bounds.  With cost_iq = distance of the cell's
// seed location to child q's center this is the "cut the seed
// placement at the capacity-balanced line" step of partitioning-based
// placers that start from an analytical placement (BonnPlace's
// assignment step is the classic reference; the formulation here is
// the plain transportation problem, not its multisection variant).

#pragma once

#include <cstdint>
#include <vector>

namespace mcp {

struct AssignmentProblem
{
  int k = 2;
  // Per cell: weight (> 0) and k costs per unit weight (>= 0).
  std::vector<int64_t> weight;
  std::vector<int64_t> cost;  // [i * k + q]
  // Per child: bounds on the assigned weight (lower <= upper).
  std::vector<int64_t> lower;
  std::vector<int64_t> upper;
};

struct AssignmentResult
{
  std::vector<int> part;  // child of every cell
  int64_t cost = 0;       // sum of weight_i * cost_i,part_i
  int split_cells = 0;    // cells the fractional optimum split
  int repair_moves = 0;   // cells moved to restore the bounds
  bool feasible = false;  // bounds hold after rounding and repair
};

// Solves the problem.  Returns false only when the flow network has no
// feasible solution even with the lower bounds dropped (sum of upper
// bounds below the total weight); the result is then a nearest-child
// assignment and feasible is false.
bool solveTransport(const AssignmentProblem& problem, AssignmentResult& result);

}  // namespace mcp
