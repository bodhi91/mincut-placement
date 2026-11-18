// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "graphics.h"

#include <vector>

#include "odb/geom.h"
#include "web/core.h"

namespace mcp {

Graphics::Graphics()
{
  web::Gui::get()->registerRenderer(this);
}

void Graphics::levelDone(int level, const std::vector<odb::Rect>& regions)
{
  regions_ = regions;
  auto gui = web::Gui::get();
  gui->redraw();
  gui->pause();
}

void Graphics::drawObjects(web::Painter& painter)
{
  painter.setPen(web::Painter::kYellow, /*cosmetic=*/true);
  painter.setBrush(web::Painter::kTransparent);
  for (const odb::Rect& rect : regions_) {
    painter.drawRect(rect);
  }
}

/* static */
bool Graphics::guiActive()
{
  return web::Gui::enabled();
}

}  // namespace mcp
