// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "Assignment.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

#include "lemon/network_simplex.h"
#include "lemon/smart_graph.h"

namespace mcp {

namespace {

using Graph = lemon::SmartDigraph;
using Simplex = lemon::NetworkSimplex<Graph, int64_t, int64_t>;

// Cell -> child flows of the fractional optimum (flow[i * k + q]), or
// false when the network is infeasible.
bool fractionalFlow(const AssignmentProblem& p,
                    bool with_lower,
                    std::vector<int64_t>& flow)
{
  const int n = static_cast<int>(p.weight.size());
  const int k = p.k;
  Graph g;
  Graph::ArcMap<int64_t> lower(g);
  Graph::ArcMap<int64_t> upper(g);
  Graph::ArcMap<int64_t> cost(g);
  Graph::NodeMap<int64_t> supply(g);

  std::vector<Graph::Node> cell(n);
  std::vector<Graph::Node> child(k);
  const Graph::Node sink = g.addNode();
  int64_t total = 0;
  for (int i = 0; i < n; ++i) {
    cell[i] = g.addNode();
    supply[cell[i]] = p.weight[i];
    total += p.weight[i];
  }
  for (int q = 0; q < k; ++q) {
    child[q] = g.addNode();
    supply[child[q]] = 0;
  }
  supply[sink] = -total;

  std::vector<Graph::Arc> arc(static_cast<size_t>(n) * k);
  for (int i = 0; i < n; ++i) {
    for (int q = 0; q < k; ++q) {
      const Graph::Arc a = g.addArc(cell[i], child[q]);
      lower[a] = 0;
      upper[a] = p.weight[i];
      cost[a] = p.cost[static_cast<size_t>(i) * k + q];
      arc[static_cast<size_t>(i) * k + q] = a;
    }
  }
  for (int q = 0; q < k; ++q) {
    const Graph::Arc a = g.addArc(child[q], sink);
    lower[a] = with_lower ? std::min(p.lower[q], p.upper[q]) : 0;
    upper[a] = std::max<int64_t>(p.upper[q], lower[a]);
    cost[a] = 0;
  }

  Simplex simplex(g);
  simplex.lowerMap(lower).upperMap(upper).costMap(cost).supplyMap(supply);
  if (simplex.run() != Simplex::OPTIMAL) {
    return false;
  }
  flow.assign(static_cast<size_t>(n) * k, 0);
  for (size_t idx = 0; idx < arc.size(); ++idx) {
    flow[idx] = simplex.flow(arc[idx]);
  }
  return true;
}

}  // namespace

bool solveTransport(const AssignmentProblem& p, AssignmentResult& result)
{
  const int n = static_cast<int>(p.weight.size());
  const int k = p.k;
  result = AssignmentResult();
  result.part.assign(n, 0);
  if (n == 0) {
    result.feasible = true;
    return true;
  }
  auto cost_of
      = [&](int i, int q) { return p.cost[static_cast<size_t>(i) * k + q]; };

  std::vector<int64_t> flow;
  bool solved = fractionalFlow(p, true, flow);
  if (!solved) {
    solved = fractionalFlow(p, false, flow);
  }
  if (solved) {
    // Round every cell to the child with its largest share.
    for (int i = 0; i < n; ++i) {
      int best = 0;
      int positive = 0;
      for (int q = 0; q < k; ++q) {
        const int64_t f = flow[static_cast<size_t>(i) * k + q];
        if (f > 0) {
          ++positive;
        }
        const int64_t fb = flow[static_cast<size_t>(i) * k + best];
        if (f > fb || (f == fb && cost_of(i, q) < cost_of(i, best))) {
          best = q;
        }
      }
      result.part[i] = best;
      if (positive > 1) {
        ++result.split_cells;
      }
    }
  } else {
    for (int i = 0; i < n; ++i) {
      int best = 0;
      for (int q = 1; q < k; ++q) {
        if (cost_of(i, q) < cost_of(i, best)) {
          best = q;
        }
      }
      result.part[i] = best;
    }
  }

  // Repair the bounds: move the cheapest cells out of over-full children
  // and into under-full ones.  Only the split cells (at most k - 1) can
  // have broken them, so this ends after a few moves.
  std::vector<int64_t> load(k, 0);
  for (int i = 0; i < n; ++i) {
    load[result.part[i]] += p.weight[i];
  }
  auto over = [&](int q) { return load[q] > p.upper[q]; };
  auto under = [&](int q) { return load[q] < p.lower[q]; };
  for (int iter = 0; iter < n + k; ++iter) {
    int q_over = -1;
    int q_under = -1;
    for (int q = 0; q < k; ++q) {
      if (over(q)
          && (q_over < 0
              || load[q] - p.upper[q] > load[q_over] - p.upper[q_over])) {
        q_over = q;
      }
      if (under(q)
          && (q_under < 0
              || p.lower[q] - load[q] > p.lower[q_under] - load[q_under])) {
        q_under = q;
      }
    }
    if (q_over < 0 && q_under < 0) {
      break;
    }
    // Best move: cell i from its child to q, feasible for both sides
    // (the source may stay over-full, the target may stay under-full).
    int best_i = -1;
    int best_q = -1;
    int64_t best_delta = std::numeric_limits<int64_t>::max();
    for (int i = 0; i < n; ++i) {
      const int from = result.part[i];
      const int64_t w = p.weight[i];
      if (q_over >= 0 && from != q_over) {
        continue;
      }
      if (q_over < 0 && load[from] - w < p.lower[from]) {
        continue;
      }
      for (int q = 0; q < k; ++q) {
        if (q == from) {
          continue;
        }
        if (q_over < 0 && q != q_under) {
          continue;
        }
        if (load[q] + w > p.upper[q]) {
          continue;
        }
        const int64_t delta = w * (cost_of(i, q) - cost_of(i, from));
        if (delta < best_delta) {
          best_delta = delta;
          best_i = i;
          best_q = q;
        }
      }
    }
    if (best_i < 0) {
      break;
    }
    load[result.part[best_i]] -= p.weight[best_i];
    load[best_q] += p.weight[best_i];
    result.part[best_i] = best_q;
    ++result.repair_moves;
  }

  result.feasible = true;
  for (int q = 0; q < k; ++q) {
    if (over(q) || under(q)) {
      result.feasible = false;
    }
  }
  for (int i = 0; i < n; ++i) {
    result.cost += p.weight[i] * cost_of(i, result.part[i]);
  }
  return solved;
}

}  // namespace mcp
