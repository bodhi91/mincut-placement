// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

// Application startup: OpenROAD uses this to register the mcp Tcl
// commands.  The MinCutPlacer object itself is created in OpenRoad::init
// after the partition manager it depends on.

#pragma once

#include "tcl.h"

namespace mcp {

void initMinCutPlacer(Tcl_Interp* tcl_interp);

}  // namespace mcp
