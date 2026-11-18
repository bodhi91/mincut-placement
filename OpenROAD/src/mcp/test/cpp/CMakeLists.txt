# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

include(openroad)

add_executable(TestAssignment TestAssignment.cpp)
target_link_libraries(TestAssignment
        mcp_lib
        GTest::gtest
        GTest::gtest_main
)
target_include_directories(TestAssignment PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
gtest_discover_tests(TestAssignment
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
