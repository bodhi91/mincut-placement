// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "Capacity.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace mcp {

namespace {

// No child receives less than this fraction of the region's usable
// area, so that an (almost) empty child never degenerates into a sliver
// that the cut-line search cannot realize.
constexpr double kMinChildFraction = 0.005;

}  // namespace

Capacity::Capacity(const CapacityPolicy& policy) : policy_(policy)
{
}

bool Capacity::needsPull() const
{
  return policy_.reserve == ReservePolicy::kMinLocal
         || policy_.reserve == ReservePolicy::kMixed;
}

bool Capacity::needsDemand() const
{
  return policy_.reserve == ReservePolicy::kCongestion
         || policy_.reserve == ReservePolicy::kMixed;
}

bool Capacity::needsMass() const
{
  return policy_.reserve == ReservePolicy::kAcg;
}

void Capacity::massBounds(double cell_area,
                          double usable_area,
                          const std::vector<double>& usable,
                          const std::vector<ChildHint>& hints,
                          std::vector<double>& upper,
                          std::vector<double>& lower) const
{
  const int k = static_cast<int>(usable.size());
  double total_mass = 0.0;
  for (const ChildHint& h : hints) {
    total_mass += std::max(0.0, h.mass);
  }
  if (static_cast<int>(hints.size()) != k || total_mass <= 0.0) {
    bounds(cell_area, usable_area, usable, upper, lower);
    return;
  }
  const double S = cell_area;
  // Tolerance from the whitespace above the target density, as for
  // minlocal (bounds()), between the policy's tolerance (the floor; the
  // ACG paper's choice is a tight one) and its cap.  E21c: sparse
  // regions want the balance free (aes: 1.0), dense ones a floor around
  // 0.5 (ibex); a single fixed value serves neither.
  double tol = policy_.tolerance;
  const double own = usable_area > 0.0 ? S / usable_area : 1.0;
  const double d = density(own, nullptr);
  if (d > own && own > 0.0) {
    const double cap = std::max(policy_.max_tolerance, policy_.tolerance);
    tol = std::clamp((d - own) / own, policy_.tolerance, cap);
  }
  upper.assign(k, 0.0);
  lower.assign(k, 0.0);
  double sum_upper = 0.0;
  for (int q = 0; q < k; ++q) {
    const double share = S * std::max(0.0, hints[q].mass) / total_mass;
    upper[q] = std::min(share * (1.0 + tol), usable[q]);
    lower[q] = share * (1.0 - tol);
    sum_upper += upper[q];
  }
  if (sum_upper < S) {
    sum_upper = 0.0;
    for (int q = 0; q < k; ++q) {
      upper[q] = usable[q];
      sum_upper += upper[q];
    }
  }
  for (int q = 0; q < k; ++q) {
    const double forced = S - (sum_upper - upper[q]);
    lower[q] = std::max(0.0, std::max(lower[q], forced));
    lower[q] = std::min(lower[q], upper[q]);
  }
}

double Capacity::density(double own, bool* too_low) const
{
  if (too_low != nullptr) {
    *too_low = false;
  }
  if (policy_.density > 0.0) {
    if (policy_.density >= own) {
      return policy_.density;
    }
    if (too_low != nullptr) {
      *too_low = true;
    }
  }
  return own;
}

void Capacity::bounds(double cell_area,
                      double usable_area,
                      const std::vector<double>& usable,
                      std::vector<double>& upper,
                      std::vector<double>& lower) const
{
  const int k = static_cast<int>(usable.size());
  const double S = cell_area;
  const double U = usable_area;
  upper.assign(k, 0.0);
  lower.assign(k, 0.0);
  if (U <= 0.0) {
    return;
  }

  const double own = S / U;
  const double d = density(own, nullptr);

  // Uniform and congestion: the proportional share with the user's
  // tolerance; a child may fill up to the target density of its planned
  // box when the user asked for one.  Minlocal and mixed: the tolerance
  // grows with the whitespace above the target density, up to the cap,
  // and the planned box is no limit since the cut line follows the
  // result.
  const bool whitespace_tolerance = needsPull() && d > own && own > 0.0;
  double tol = policy_.tolerance;
  if (whitespace_tolerance) {
    const double cap = std::max(policy_.max_tolerance, policy_.tolerance);
    tol = std::clamp((d - own) / own, policy_.tolerance, cap);
  }

  double sum_upper = 0.0;
  for (int q = 0; q < k; ++q) {
    const double Uq = usable[q];
    const double share = S * Uq / U;
    double up;
    if (whitespace_tolerance) {
      up = std::min(share * (1.0 + tol), S);
    } else {
      up = std::max(share * (1.0 + tol), Uq * d);
      up = std::min(up, Uq);
    }
    upper[q] = up;
    lower[q] = share * (1.0 - tol);
    sum_upper += up;
  }
  if (sum_upper < S) {
    // Overfull region: physical capacity is the only bound left.
    sum_upper = 0.0;
    for (int q = 0; q < k; ++q) {
      upper[q] = usable[q];
      sum_upper += upper[q];
    }
  }
  // Lower bounds forced by the other children's capacities.
  for (int q = 0; q < k; ++q) {
    const double forced = S - (sum_upper - upper[q]);
    lower[q] = std::max(0.0, std::max(lower[q], forced));
    lower[q] = std::min(lower[q], upper[q]);
  }
}

std::vector<double> Capacity::targets(double usable_area,
                                      const std::vector<double>& cell_area,
                                      const std::vector<ChildHint>& hints) const
{
  const int k = static_cast<int>(cell_area.size());
  double S = 0.0;
  for (double a : cell_area) {
    S += a;
  }
  const double U = usable_area;
  if (!adaptive() || static_cast<int>(hints.size()) != k || U <= 0.0 || S <= 0.0
      || S >= U) {
    return cell_area;
  }
  const double d = density(S / U, nullptr);
  const double excess = U - S / d;
  if (d <= 0.0 || excess <= 0.0) {
    return cell_area;
  }

  // Normalized distribution rule for the excess.
  std::vector<double> rule(k, 0.0);
  double sum_dist = 0.0;
  double sum_demand = 0.0;
  double sum_free = 0.0;  // acg: child area outside the mass rectangle
  for (const ChildHint& h : hints) {
    sum_dist += std::max(0.0, h.distance);
    sum_demand += std::max(0.0, h.demand);
    sum_free += std::max(0.0, h.box_area - h.mass);
  }
  for (int q = 0; q < k; ++q) {
    const double dist = sum_dist > 0.0
                            ? std::max(0.0, hints[q].distance) / sum_dist
                            : 1.0 / k;
    const double dem = sum_demand > 0.0
                           ? std::max(0.0, hints[q].demand) / sum_demand
                           : 1.0 / k;
    switch (policy_.reserve) {
      case ReservePolicy::kMinLocal:
        rule[q] = dist;
        break;
      case ReservePolicy::kCongestion:
        rule[q] = dem;
        break;
      case ReservePolicy::kMixed:
        rule[q] = policy_.blend * dist + (1.0 - policy_.blend) * dem;
        break;
      case ReservePolicy::kAcg:
        // The whitespace goes where the analytical mass is not.
        rule[q] = sum_free > 0.0
                      ? std::max(0.0, hints[q].box_area - hints[q].mass)
                            / sum_free
                      : 1.0 / k;
        break;
      case ReservePolicy::kUniform:
        rule[q] = 1.0 / k;
        break;
    }
  }

  std::vector<double> targets(k, 0.0);
  const double floor = kMinChildFraction * U;
  for (int q = 0; q < k; ++q) {
    targets[q] = std::max(cell_area[q] / d + rule[q] * excess, floor);
  }
  return targets;
}

ReservePolicy reservePolicyFromString(const std::string& name)
{
  if (name == "uniform") {
    return ReservePolicy::kUniform;
  }
  if (name == "minlocal") {
    return ReservePolicy::kMinLocal;
  }
  if (name == "congestion") {
    return ReservePolicy::kCongestion;
  }
  if (name == "mixed") {
    return ReservePolicy::kMixed;
  }
  if (name == "acg") {
    return ReservePolicy::kAcg;
  }
  throw std::invalid_argument("unknown reserve policy: " + name);
}

const char* reservePolicyToString(ReservePolicy policy)
{
  switch (policy) {
    case ReservePolicy::kUniform:
      return "uniform";
    case ReservePolicy::kMinLocal:
      return "minlocal";
    case ReservePolicy::kCongestion:
      return "congestion";
    case ReservePolicy::kMixed:
      return "mixed";
    case ReservePolicy::kAcg:
      return "acg";
  }
  return "unknown";
}

}  // namespace mcp
