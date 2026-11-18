# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

or_integration_tests(
  "mcp"
  TESTS
    mincut_place01
    mincut_place02
    mincut_place03
    mincut_place04
    mincut_place05
    mincut_place06
    mincut_place07
)

if(ENABLE_TESTS)
  add_subdirectory(cpp)
endif()
