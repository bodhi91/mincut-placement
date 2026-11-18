#!/usr/bin/env bash
# Builds Mt-KaHyPar with the process-mapping features that a stock
# 1.6.2 binary built with the defaults lacks (its PROVENANCE.md shows
# KAHYPAR_ENABLE_STEINER_TREE_METRIC=OFF and
# KAHYPAR_ENABLE_GRAPH_PARTITIONING_FEATURES=OFF, so `-o steiner_tree
# -g <target graph>` fails with "Cannot cast NULLPTR_HYPERGRAPH to
# STATIC_GRAPH").  The X0 / X1 k-way experiments map a level's hypergraph
# onto the grid of its regions with the Steiner-tree objective, which needs
# both, plus the large-k features for k in the thousands.
#
#   tools/mtkahypar/build.sh [src]   default src: /tmp/$USER/mt-kahypar-src
#                                    (local disk; the clone has many files
#                                    and the project NFS is slow on those;
#                                    the X5 rebuild used scratch/mtkahypar)
#   -> tools/mtkahypar/bin/MtKaHyPar, tools/mtkahypar/lib/libmtkahypar.so*
#
# Patches in tools/mtkahypar/patches/ are applied on top of the commit (the
# greedy-mapping stack overflow of X5, see the patch header).
#
# Same upstream commit as the installed binary (v1.6.2-9-gc95e871f) and the
# same toolchain (gcc 12.2.0, boost 1.81.0, oneTBB 2021.11.0, hwloc 2.4.1,
# cmake 4.4.0), taken by path because the modulefiles conflict with the
# login environment.  libstdc++ of gcc 12 is put on the rpath so the binary
# runs outside the conda env too.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
src="${1:-/tmp/$USER/mt-kahypar-src}"
commit=c95e871f5276843071c8ade752733075991469c3

# Toolchain used for the paper's build: GCC 12.2, CMake 4.4, Boost 1.81,
# oneTBB 2021.11, hwloc 2.4.1.  Point these at your installations.
gcc_root="${GCC_ROOT:?set GCC_ROOT to a GCC >= 12 installation}"
cmake_bin="${CMAKE_BIN:-cmake}"
boost_root="${BOOST_ROOT:?set BOOST_ROOT (Boost >= 1.81)}"
tbb_dir="${TBB_DIR:?set TBB_DIR to <oneTBB>/lib64/cmake/TBB}"
hwloc_root="${HWLOC_ROOT:?set HWLOC_ROOT (hwloc 2.x)}"

if [[ ! -d "$src/.git" ]]; then
  git clone --recursive https://github.com/kahypar/mt-kahypar "$src"
fi
cd "$src"
git fetch --quiet origin
git checkout --quiet "$commit"
git submodule update --init --recursive --quiet

# Local fixes on top of the upstream commit (tools/mtkahypar/patches/*.patch,
# each with its reason in the header); re-applied from a clean checkout.
git checkout --quiet -- .
for p in "$here"/patches/*.patch; do
  [[ -e "$p" ]] || continue
  echo "applying $(basename "$p")"
  patch -p1 --forward --silent -i "$p"
done

export CC="$gcc_root/bin/gcc" CXX="$gcc_root/bin/g++"
export LD_LIBRARY_PATH="$gcc_root/lib64:${LD_LIBRARY_PATH:-}"
"$cmake_bin" -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DKAHYPAR_ENABLE_ARCH_COMPILE_OPTIMIZATIONS=OFF \
  -DKAHYPAR_ENABLE_EXPERIMENTAL_FEATURES=OFF \
  -DKAHYPAR_ENABLE_EXTENDED_INSTRUCTIONS=OFF \
  -DKAHYPAR_ENABLE_GRAPH_PARTITIONING_FEATURES=ON \
  -DKAHYPAR_ENABLE_HEAVY_COARSENING_ASSERTIONS=OFF \
  -DKAHYPAR_ENABLE_HEAVY_INITIAL_PARTITIONING_ASSERTIONS=OFF \
  -DKAHYPAR_ENABLE_HEAVY_PREPROCESSING_ASSERTIONS=OFF \
  -DKAHYPAR_ENABLE_HEAVY_REFINEMENT_ASSERTIONS=OFF \
  -DKAHYPAR_ENABLE_HIGHEST_QUALITY_FEATURES=OFF \
  -DKAHYPAR_ENABLE_LARGE_K_PARTITIONING_FEATURES=ON \
  -DKAHYPAR_ENABLE_SOED_METRIC=OFF \
  -DKAHYPAR_ENABLE_STEINER_TREE_METRIC=ON \
  -DKAHYPAR_ENABLE_TESTING=OFF \
  -DKAHYPAR_ENABLE_THREAD_PINNING=OFF \
  -DBOOST_ROOT="$boost_root" \
  -DTBB_DIR="$tbb_dir" \
  -DCMAKE_PREFIX_PATH="$hwloc_root" \
  -DCMAKE_EXE_LINKER_FLAGS="-pthread -L$gcc_root/lib64 -Wl,-rpath,$gcc_root/lib64 -Wl,--disable-new-dtags"
"$cmake_bin" --build build --target MtKaHyPar mtkahypar -j "${JOBS:-8}"

mkdir -p "$here/bin" "$here/lib"
cp build/mt-kahypar/application/MtKaHyPar "$here/bin/MtKaHyPar"
# Install the library by rename, never by overwriting in place: openroad
# processes of running experiments have it mapped, and rewriting the mapped
# file crashes them (X5, Nov 17: four in-flight runs died at the cp).
for f in build/lib/libmtkahypar.so*; do
  if [[ -L "$f" ]]; then
    ln -sfn "$(readlink "$f")" "$here/lib/$(basename "$f")"
  else
    cp "$f" "$here/lib/$(basename "$f").new"
    mv -f "$here/lib/$(basename "$f").new" "$here/lib/$(basename "$f")"
  fi
done
echo "built $here/bin/MtKaHyPar"

# The mapping-refinement driver (improve_mapping.cpp) against the library.
"$CXX" -O2 -std=c++17 -I "$src/include" "$here/improve_mapping.cpp" \
  -L "$here/lib" -lmtkahypar -L "$gcc_root/lib64" -pthread \
  -Wl,-rpath,"$here/lib" -Wl,-rpath,"$gcc_root/lib64" -Wl,--disable-new-dtags \
  -o "$here/bin/improve_mapping"
echo "built $here/bin/improve_mapping"

# The source and build tree (~600 MB in /tmp) are only needed to rebuild;
# the binaries, the library and the driver have been copied out.  Keep them
# with KEEP_SRC=1.
if [[ -z "${KEEP_SRC:-}" ]]; then
  rm -rf "$src"
  echo "removed $src (set KEEP_SRC=1 to keep it)"
fi
