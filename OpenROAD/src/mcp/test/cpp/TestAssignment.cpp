// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

// solveTransport against brute force on small random instances: the
// rounded flow solution must be feasible whenever an integral feasible
// assignment exists, and its cost must be within the rounding slack
// (the k - 1 split cells) of the integral optimum; on instances with
// unit weights (no splits) it must be optimal.

#include <cstdint>
#include <limits>
#include <random>
#include <vector>

#include "Assignment.h"
#include "gtest/gtest.h"

namespace mcp {
namespace {

struct Brute
{
  int64_t cost = std::numeric_limits<int64_t>::max();
  bool feasible = false;
};

Brute bruteForce(const AssignmentProblem& p)
{
  const int n = static_cast<int>(p.weight.size());
  const int k = p.k;
  Brute best;
  std::vector<int> part(n, 0);
  std::vector<int64_t> load(k, 0);
  for (;;) {
    std::fill(load.begin(), load.end(), 0);
    int64_t cost = 0;
    for (int i = 0; i < n; ++i) {
      load[part[i]] += p.weight[i];
      cost += p.weight[i] * p.cost[static_cast<size_t>(i) * k + part[i]];
    }
    bool ok = true;
    for (int q = 0; q < k; ++q) {
      ok = ok && load[q] >= p.lower[q] && load[q] <= p.upper[q];
    }
    if (ok && cost < best.cost) {
      best.cost = cost;
      best.feasible = true;
    }
    int i = 0;
    while (i < n && ++part[i] == k) {
      part[i] = 0;
      ++i;
    }
    if (i == n) {
      break;
    }
  }
  return best;
}

AssignmentProblem randomProblem(std::mt19937& rng,
                                int n,
                                int k,
                                bool unit_weights)
{
  AssignmentProblem p;
  p.k = k;
  std::uniform_int_distribution<int64_t> w(1, unit_weights ? 1 : 5);
  std::uniform_int_distribution<int64_t> c(0, 20);
  int64_t total = 0;
  for (int i = 0; i < n; ++i) {
    p.weight.push_back(w(rng));
    total += p.weight.back();
    for (int q = 0; q < k; ++q) {
      p.cost.push_back(c(rng));
    }
  }
  // Bounds around the proportional share.
  const double share = static_cast<double>(total) / k;
  std::uniform_real_distribution<double> tol(0.1, 0.6);
  for (int q = 0; q < k; ++q) {
    const double t = tol(rng);
    p.lower.push_back(static_cast<int64_t>(share * (1.0 - t)));
    p.upper.push_back(static_cast<int64_t>(share * (1.0 + t)) + 1);
  }
  return p;
}

TEST(Assignment, UnitWeightsAreOptimal)
{
  std::mt19937 rng(7);
  for (int trial = 0; trial < 200; ++trial) {
    const int k = (trial % 2 == 0) ? 2 : 4;
    const int n = 4 + trial % 6;
    const AssignmentProblem p = randomProblem(rng, n, k, true);
    const Brute brute = bruteForce(p);
    AssignmentResult result;
    solveTransport(p, result);
    if (!brute.feasible) {
      continue;
    }
    EXPECT_TRUE(result.feasible) << "trial " << trial;
    EXPECT_EQ(result.cost, brute.cost) << "trial " << trial;
    EXPECT_EQ(result.split_cells, 0);
  }
}

TEST(Assignment, WeightedWithinRoundingSlack)
{
  std::mt19937 rng(11);
  int checked = 0;
  for (int trial = 0; trial < 300; ++trial) {
    const int k = (trial % 3 == 0) ? 4 : 2;
    const int n = 4 + trial % 5;
    const AssignmentProblem p = randomProblem(rng, n, k, false);
    const Brute brute = bruteForce(p);
    AssignmentResult result;
    solveTransport(p, result);
    if (!brute.feasible) {
      continue;
    }
    ++checked;
    EXPECT_LE(result.split_cells, k - 1);
    if (result.feasible) {
      // At most (k - 1) split cells of weight <= 5 and cost <= 20 each
      // can be rounded the wrong way, plus the repair moves.
      const int64_t slack
          = static_cast<int64_t>(k - 1 + result.repair_moves) * 5 * 20;
      EXPECT_GE(result.cost, brute.cost) << "trial " << trial;
      EXPECT_LE(result.cost, brute.cost + slack) << "trial " << trial;
    }
  }
  EXPECT_GT(checked, 100);
}

TEST(Assignment, InfeasibleUpperFallsBackToNearest)
{
  AssignmentProblem p;
  p.k = 2;
  p.weight = {3, 3, 3};
  p.cost = {1, 5, 5, 1, 2, 2};
  p.lower = {0, 0};
  p.upper = {2, 2};  // total 9 does not fit
  AssignmentResult result;
  EXPECT_FALSE(solveTransport(p, result));
  EXPECT_FALSE(result.feasible);
  EXPECT_EQ(result.part, (std::vector<int>{0, 1, 0}));
}

TEST(Assignment, LowerBoundsForceMoves)
{
  // Everything wants child 0, but child 1 must receive at least 4.
  AssignmentProblem p;
  p.k = 2;
  p.weight = {2, 2, 2, 2};
  p.cost = {0, 1, 0, 2, 0, 3, 0, 4};
  p.lower = {0, 4};
  p.upper = {8, 8};
  AssignmentResult result;
  EXPECT_TRUE(solveTransport(p, result));
  EXPECT_TRUE(result.feasible);
  EXPECT_EQ(result.part, (std::vector<int>{1, 1, 0, 0}));
  EXPECT_EQ(result.cost, 2 * 1 + 2 * 2);
}

}  // namespace
}  // namespace mcp
