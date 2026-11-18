// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

// Python interface for "openroad -python": exposes MinCutPlacer and
// Options directly.

%module mcp_py

%{
#include "mcp/MinCutPlacer.h"
#include "ord/OpenRoad.hh"
%}

%include "../../Exception-py.i"
%include <std_string.i>

%include "mcp/MinCutPlacer.h"
