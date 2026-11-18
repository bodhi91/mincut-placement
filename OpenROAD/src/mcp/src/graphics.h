// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <vector>

#include "observer.h"
#include "odb/geom.h"
#include "web/core.h"

// Renders the region decomposition in the GUI.

namespace mcp {

class Graphics : public web::Renderer, public Observer
{
 public:
  Graphics();

  // Observer
  void levelDone(int level, const std::vector<odb::Rect>& regions) override;

  // Renderer
  void drawObjects(web::Painter& painter) override;

  static bool guiActive();

 private:
  std::vector<odb::Rect> regions_;
};

}  // namespace mcp
