# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

include("openroad")

find_package(LEMON NAMES LEMON lemon REQUIRED)
find_package(Eigen3 REQUIRED)

swig_lib(NAME         mcp
         NAMESPACE    mcp
         I_FILE       src/mcp.i
         SCRIPTS      src/mcp.tcl
         SWIG_INCLUDES ${ODB_HOME}/src/swig/common
                       ${ODB_HOME}/src/swig/tcl
                       ${ODB_HOME}/include
)

# mcp_lib holds the placer without GUI dependencies; unit tests link it.
add_library(mcp_lib
  src/Assignment.cpp
  src/Capacity.cpp
  src/Decomposer.cpp
  src/EndCase.cpp
  src/Engine.cpp
  src/ExactPartition.cpp
  src/Feedback.cpp
  src/Ironing.cpp
  src/KwayRefine.cpp
  src/BoundarySweep.cpp
  src/MinCutPlacer.cpp
  src/NetlistModel.cpp
  src/Optimal.cpp
  src/Oracle.cpp
  src/PlacementArea.cpp
  src/RegionTree.cpp
  src/SubproblemBuilder.cpp
)

target_link_libraries(mcp_lib
  PUBLIC
    odb
    par_lib
  PRIVATE
    utl_lib
    stt_lib
    Eigen3::Eigen
)

target_include_directories(mcp_lib
  PUBLIC
    include
  PRIVATE
    src
    ${LEMON_INCLUDE_DIRS}
)

# Optional: Mt-KaHyPar as a library for the whole-level k-way refinement
# (X1b, KwayRefine.cpp).  MCP_MTKAHYPAR_DIR points at a directory with
# include/mtkahypar.h and lib/libmtkahypar.so (tools/mtkahypar in this
# project, built by its build.sh).  Without it the option
# -kway_refine_levels warns and does nothing.
set(MCP_MTKAHYPAR_DIR "" CACHE PATH "Mt-KaHyPar install (include/, lib/) for -kway_refine_levels")
if (MCP_MTKAHYPAR_DIR)
  target_compile_definitions(mcp_lib PRIVATE MCP_HAVE_MTKAHYPAR)
  target_include_directories(mcp_lib PRIVATE ${MCP_MTKAHYPAR_DIR}/include)
  target_link_libraries(mcp_lib PRIVATE ${MCP_MTKAHYPAR_DIR}/lib/libmtkahypar.so)
  set_property(TARGET mcp_lib APPEND PROPERTY
    INTERFACE_LINK_OPTIONS "-Wl,-rpath,${MCP_MTKAHYPAR_DIR}/lib")
  message(STATUS "mcp: k-way refinement with Mt-KaHyPar from ${MCP_MTKAHYPAR_DIR}")
endif()

# mcp adds the Tcl/GUI layer; openroad links this.
target_sources(mcp
  PRIVATE
    src/MakeMinCutPlacer.cpp
    src/graphics.cpp
)

target_include_directories(mcp
  PUBLIC
    include
  PRIVATE
    src
)

target_link_libraries(mcp
  PRIVATE
    ${TCL_LIBRARY}
    mcp_lib
    gui_core
)

messages(
  TARGET mcp
)

################################################################

if (Python3_FOUND AND BUILD_PYTHON)
  swig_lib(NAME          mcp_py
           NAMESPACE     mcp
           LANGUAGE      python
           I_FILE        src/mcp-py.i
           SWIG_INCLUDES ${ODB_HOME}/src/swig/common
                         ${ODB_HOME}/src/swig/python
           SCRIPTS       ${CMAKE_CURRENT_BINARY_DIR}/mcp_py.py
  )

  target_link_libraries(mcp_py
    PUBLIC
      mcp_lib
      gui_core
      odb
  )
endif()

if(ENABLE_TESTS)
  add_subdirectory(test)
endif()
