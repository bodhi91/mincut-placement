// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

// Exact partitioning of small subproblems by branch and bound (Capo's
// optimal partitioner for the last levels, where FM-style refiners are
// least reliable).  The search starts from the engine's result as the
// incumbent and enumerates the assignments of the movable vertices in
// order of decreasing degree; the cost of the nets touched so far is a
// lower bound because every net cost (cut, or a net vector) is monotone
// in the set of occupied children.  The result is never worse than the
// incumbent; a node budget bounds the worst case.

#pragma once

#include <cstdint>
#include <vector>

namespace mcp {

struct Subproblem;

struct ExactStats
{
  int64_t nodes = 0;
  bool complete = true;  // the search finished within its budget
  bool improved = false;
};

// Improves parts (the incumbent partition of sub's movable vertices) in
// place.  Returns true when a cheaper feasible partition was found.
bool exactPartition(const Subproblem& sub,
                    std::vector<int>& parts,
                    int64_t node_budget,
                    ExactStats& stats);

}  // namespace mcp
