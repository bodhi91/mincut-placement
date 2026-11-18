// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "ExactPartition.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "Engine.h"

namespace mcp {

namespace {

constexpr double kEps = 1e-4;

class Search
{
 public:
  Search(const Subproblem& sub, int64_t budget) : sub_(sub), budget_(budget)
  {
    n_ = sub.num_movable;
    k_ = sub.k;
    const int num_edges = static_cast<int>(sub.hyperedges.size());
    adj_.assign(n_, {});
    mask_.assign(num_edges, 0);
    for (int e = 0; e < num_edges; ++e) {
      for (int v : sub.hyperedges[e]) {
        if (v < n_) {
          adj_[v].push_back(e);
        } else {
          mask_[e] |= 1 << sub.fixed_part[v];
        }
      }
    }
    // Most connected (then heaviest) vertices first.
    order_.resize(n_);
    for (int v = 0; v < n_; ++v) {
      order_[v] = v;
    }
    std::sort(order_.begin(), order_.end(), [&](int a, int b) {
      if (adj_[a].size() != adj_[b].size()) {
        return adj_[a].size() > adj_[b].size();
      }
      if (sub.vertex_weights[a] != sub.vertex_weights[b]) {
        return sub.vertex_weights[a] > sub.vertex_weights[b];
      }
      return a < b;
    });
    remaining_.assign(n_ + 1, 0.0);
    for (int i = n_ - 1; i >= 0; --i) {
      remaining_[i] = remaining_[i + 1] + sub.vertex_weights[order_[i]];
    }
    load_.assign(k_, 0.0);
    current_.assign(n_, -1);
  }

  double netCost(int e, int mask) const
  {
    if (sub_.vectors) {
      return mask == 0 ? 0.0 : sub_.vectors->NetCost(e, mask);
    }
    return (mask & (mask - 1)) != 0 ? sub_.hyperedge_weights[e] : 0.0;
  }

  double vertexCost(int v, int q) const
  {
    if (sub_.vectors && sub_.vectors->HasVertexCost()) {
      return sub_.vectors->VertexCost(v, q);
    }
    return 0.0;
  }

  // Cost and feasibility of a complete partition.
  double evaluate(const std::vector<int>& parts, bool& feasible) const
  {
    std::vector<int> mask = mask_;
    std::vector<double> load(k_, 0.0);
    double cost = 0.0;
    for (int v = 0; v < n_; ++v) {
      load[parts[v]] += sub_.vertex_weights[v];
      cost += vertexCost(v, parts[v]);
      for (int e : adj_[v]) {
        mask[e] |= 1 << parts[v];
      }
    }
    for (size_t e = 0; e < mask.size(); ++e) {
      cost += netCost(static_cast<int>(e), mask[e]);
    }
    feasible = true;
    for (int q = 0; q < k_; ++q) {
      if (load[q] > sub_.upper[q] + kEps || load[q] < sub_.lower[q] - kEps) {
        feasible = false;
      }
    }
    return cost;
  }

  bool run(const std::vector<int>& incumbent, double bound, ExactStats& stats)
  {
    incumbent_ = &incumbent;
    best_cost_ = bound;
    found_ = false;
    aborted_ = false;
    nodes_ = 0;
    double cost0 = 0.0;
    for (size_t e = 0; e < mask_.size(); ++e) {
      cost0 += netCost(static_cast<int>(e), mask_[e]);
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
      for (int q = 0; q < k_; ++q) {
        if (load_[q] < sub_.lower[q] - kEps) {
          return;
        }
      }
      if (cost < best_cost_ - kEps) {
        best_cost_ = cost;
        best_ = current_;
        found_ = true;
      }
      return;
    }
    const int v = order_[depth];
    const double w = sub_.vertex_weights[v];
    const int first = (*incumbent_)[v];
    for (int i = 0; i < k_; ++i) {
      // Incumbent's part first so that the first leaf is the incumbent.
      const int q = i == 0 ? first : (i <= first ? i - 1 : i);
      if (load_[q] + w > sub_.upper[q] + kEps) {
        continue;
      }
      // The lower bounds must still be reachable with what is left.
      double deficit = 0.0;
      for (int p = 0; p < k_; ++p) {
        const double have = load_[p] + (p == q ? w : 0.0);
        deficit += std::max(0.0, sub_.lower[p] - have);
      }
      if (deficit > remaining_[depth + 1] + kEps) {
        continue;
      }
      if (++nodes_ > budget_) {
        aborted_ = true;
        return;
      }
      double next = cost + vertexCost(v, q);
      const size_t undo_mark = undo_.size();
      const int bit = 1 << q;
      for (int e : adj_[v]) {
        if ((mask_[e] & bit) == 0) {
          undo_.emplace_back(e, mask_[e]);
          const double before = netCost(e, mask_[e]);
          mask_[e] |= bit;
          next += netCost(e, mask_[e]) - before;
        }
      }
      if (next < best_cost_ - kEps) {
        load_[q] += w;
        current_[v] = q;
        dfs(depth + 1, next);
        load_[q] -= w;
      }
      while (undo_.size() > undo_mark) {
        mask_[undo_.back().first] = undo_.back().second;
        undo_.pop_back();
      }
      if (aborted_) {
        return;
      }
    }
  }

  const Subproblem& sub_;
  int64_t budget_;
  int n_ = 0;
  int k_ = 0;
  std::vector<std::vector<int>> adj_;
  std::vector<int> mask_;
  std::vector<int> order_;
  std::vector<double> remaining_;
  std::vector<double> load_;
  std::vector<int> current_;
  std::vector<std::pair<int, int>> undo_;
  const std::vector<int>* incumbent_ = nullptr;
  double best_cost_ = 0.0;
  std::vector<int> best_;
  bool found_ = false;
  bool aborted_ = false;
  int64_t nodes_ = 0;
};

}  // namespace

bool exactPartition(const Subproblem& sub,
                    std::vector<int>& parts,
                    int64_t node_budget,
                    ExactStats& stats)
{
  if (sub.num_movable < 2 || sub.k < 2
      || static_cast<int>(parts.size()) < sub.num_movable) {
    return false;
  }
  Search search(sub, node_budget);
  bool feasible = false;
  const double incumbent = search.evaluate(parts, feasible);
  const double bound
      = feasible ? incumbent : std::numeric_limits<double>::infinity();
  if (search.run(parts, bound, stats)) {
    for (int v = 0; v < sub.num_movable; ++v) {
      parts[v] = search.best()[v];
    }
    stats.improved = true;
    return true;
  }
  return false;
}

}  // namespace mcp
