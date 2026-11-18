// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

// Capacity policy of a split: the balance bounds handed to the
// partitioner and the share of the region's free area every child
// receives when the cut lines are shifted after partitioning.
//
// A region of usable area U with cell area S has whitespace W = U - S.
// The uniform policy keeps every child at the region's utilization
// (child q gets free area proportional to its cell area S_q); this is
// the classic min-cut behavior and spreads the cells over the whole
// core.  The adaptive policies give child q only the reserve it needs
// to hold its cells at the target density d (S_q / d in total) and
// distribute the excess E = U - S / d by a rule: away from the external
// pull of the region (minlocal), proportionally to a routing-demand
// surrogate (congestion), or a blend (mixed).  They also widen the
// balance tolerance of the partitioner in proportion to the excess, the
// idea behind Capo's whitespace-derived tolerances (Caldwell, Kahng,
// Markov; Roy and Markov).

#pragma once

#include <vector>

#include "mcp/MinCutPlacer.h"  // ReservePolicy

namespace mcp {

struct CapacityPolicy
{
  ReservePolicy reserve = ReservePolicy::kMinLocal;
  // Target utilization of every region (0 = derive from the region's
  // own utilization, i.e. uniform whitespace).  The adaptive policies
  // move only the whitespace above this density.
  double density = 0.0;
  // Two-sided tolerance on each child's proportional share of the cell
  // area; the minimum tolerance for the adaptive policies.
  double tolerance = 0.05;
  // Upper limit of the whitespace-derived tolerance (adaptive policies).
  double max_tolerance = 1.0;
  // mixed: weight of the pull-distance rule; 1 - blend for demand.
  double blend = 0.5;
};

// Per-child inputs of the adaptive policies, computed by the caller for
// the planned child boxes.
struct ChildHint
{
  // Distance from the child center to the region's external pull
  // (along the cut axis for a bisection, Manhattan for a quadrisection).
  double distance = 0.0;
  // Routing-demand surrogate integrated over the child box.
  double demand = 0.0;
  // acg: area of the analytical mass rectangle (the region's cell area
  // at the target density, centered at the cells' center of mass) that
  // falls into the child box, and the child's usable area.
  double mass = 0.0;
  double box_area = 0.0;
};

class Capacity
{
 public:
  explicit Capacity(const CapacityPolicy& policy);

  const CapacityPolicy& policy() const { return policy_; }
  bool adaptive() const { return policy_.reserve != ReservePolicy::kUniform; }
  bool needsPull() const;
  bool needsDemand() const;
  bool needsMass() const;

  // Target density for a region of utilization `own`: the policy's
  // density when it can hold the cells, otherwise `own`.  *too_low is
  // set when a positive policy density had to be raised.
  double density(double own, bool* too_low) const;

  // Balance bounds for the partitioner.  usable[q] is the usable area
  // of planned child q; upper/lower receive one entry per child.
  void bounds(double cell_area,
              double usable_area,
              const std::vector<double>& usable,
              std::vector<double>& upper,
              std::vector<double>& lower) const;

  // acg: balance bounds from the mass hints -- child q's share of the
  // cell area is its share of the mass rectangle, with the policy's
  // tolerance (Analytic Constraint Generation: the balance ratio comes
  // from the analytical solution, the tolerance stays tight).  Falls
  // back to bounds() when the hints carry no mass.
  void massBounds(double cell_area,
                  double usable_area,
                  const std::vector<double>& usable,
                  const std::vector<ChildHint>& hints,
                  std::vector<double>& upper,
                  std::vector<double>& lower) const;

  // Weights of the children for the cut-line shift: child q should
  // receive the fraction weights[q] / sum(weights) of the region's
  // usable area.  cell_area[q] is the realized cell area; hints may be
  // empty (uniform: weights are the cell areas, so the children end up
  // at the same utilization).
  std::vector<double> targets(double usable_area,
                              const std::vector<double>& cell_area,
                              const std::vector<ChildHint>& hints) const;

 private:
  CapacityPolicy policy_;
};

}  // namespace mcp
