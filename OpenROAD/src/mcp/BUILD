# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("//bazel:python_wrap_cc.bzl", "python_wrap_cc", "python_wrap_library")
load("//bazel:tcl_encode_or.bzl", "tcl_encode")
load("//bazel:tcl_wrap_cc.bzl", "tcl_wrap_cc")
load("//test:regression.bzl", "messages_txt")

package(
    default_visibility = ["//:__subpackages__"],
    features = ["layering_check"],
)

cc_library(
    name = "mcp_impl",
    srcs = [
        "src/Decomposer.cpp",
        "src/EndCase.cpp",
        "src/Engine.cpp",
        "src/MinCutPlacer.cpp",
        "src/NetlistModel.cpp",
        "src/PlacementArea.cpp",
        "src/RegionTree.cpp",
        "src/SubproblemBuilder.cpp",
    ],
    hdrs = [
        "include/mcp/MinCutPlacer.h",
        "src/Decomposer.h",
        "src/EndCase.h",
        "src/Engine.h",
        "src/NetlistModel.h",
        "src/PlacementArea.h",
        "src/Region.h",
        "src/RegionTree.h",
        "src/SubproblemBuilder.h",
        "src/observer.h",
    ],
    includes = [
        "include",
        "src",
    ],
    visibility = [
        "//src/mcp:__subpackages__",
    ],
    deps = [
        "//src/odb/src/db",
        "//src/par",
        "//src/stt",
        "//src/utl",
    ],
)

cc_library(
    name = "mcp",
    hdrs = [
        "include/mcp/MinCutPlacer.h",
    ],
    includes = [
        "include",
    ],
    deps = [
        ":mcp_impl",
        "//src/odb/src/db",
        "//src/utl",
    ],
)

cc_library(
    name = "ui",
    srcs = [
        "src/MakeMinCutPlacer.cpp",
        "src/graphics.cpp",
        "src/graphics.h",
        ":swig",
        ":tcl",
    ],
    hdrs = [
        "include/mcp/MakeMinCutPlacer.h",
    ],
    includes = [
        "include",
        "src",
    ],
    deps = [
        ":mcp_impl",
        "//:ord",
        "//src/odb/src/db",
        "//src/utl",
        "//src/web:core",
        "@boost.stacktrace",
        "@tcl_lang//:tcl",
    ],
)

tcl_encode(
    name = "tcl",
    srcs = [
        "src/mcp.tcl",
    ],
    char_array_name = "mcp_tcl_inits",
    namespace = "mcp",
)

tcl_wrap_cc(
    name = "swig",
    srcs = [
        "src/mcp.i",
        "//:error_swig",
    ],
    module = "mcp",
    namespace_prefix = "mcp",
    root_swig_src = "src/mcp.i",
    swig_includes = [
        "src/mcp/src",
    ],
    deps = [
        "//src/odb:swig",
    ],
)

python_wrap_cc(
    name = "swig-py",
    srcs = [
        "include/mcp/MinCutPlacer.h",
        "src/mcp-py.i",
        "//:error_swig-py",
    ],
    module = "mcp_py",
    root_swig_src = "src/mcp-py.i",
    swig_includes = [
        "include",
        "src",
    ],
)

exports_files(["README.md"])

messages_txt(
    visibility = ["//docs:__pkg__"],
)

python_wrap_library(
    name = "py_wrap",
    includes = ["src"],
    deps = [":mcp"],
)
