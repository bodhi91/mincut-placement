// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <vector>

#include "odb/geom.h"

// Interface through which the placer notifies debug graphics without
// depending on the GUI (keeps mcp_lib free of GUI dependencies for unit
// tests).

namespace mcp {

class Observer
{
 public:
  virtual ~Observer() = default;

  // Called after every level with the rectangles of all regions of the
  // next level.
  virtual void levelDone(int level, const std::vector<odb::Rect>& regions) = 0;
};

}  // namespace mcp
