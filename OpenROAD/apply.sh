#!/usr/bin/env bash
#
# Reproduces the OpenROAD build with the min-cut placer:
#   1. clones upstream OpenROAD at BASE_COMMIT (or uses an existing clone),
#   2. applies integration.patch and copies src/mcp into it,
#   3. configures and builds (Release).
#
#   OpenROAD/apply.sh [clone dir]        default clone dir: ./openroad
#
# Environment:
#   MCP_MTKAHYPAR_DIR  Mt-KaHyPar install with include/ and lib/ (built by
#                      tools/mtkahypar/build.sh); enables -kway_refine_levels
#                      (RB+k).  Unset: the option warns and does nothing.
#   CMAKE_ARGS         extra arguments for the configure step.
#   JOBS               parallel build jobs (default: nproc).
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
dest="${1:-$PWD/openroad}"
commit="$(cat "$here/BASE_COMMIT")"

if [[ ! -d "$dest/.git" ]]; then
  git clone https://github.com/The-OpenROAD-Project/OpenROAD.git "$dest"
fi
cd "$dest"
git fetch --quiet origin "$commit" || true
git checkout --quiet "$commit"
git submodule update --init --recursive

if git apply --check "$here/integration.patch" 2>/dev/null; then
  git apply "$here/integration.patch"
else
  echo "integration.patch already applied or does not apply cleanly; continuing" >&2
fi
rm -rf src/mcp
cp -r "$here/src/mcp" src/mcp

args=(-DCMAKE_BUILD_TYPE=Release)
[[ -n "${MCP_MTKAHYPAR_DIR:-}" ]] && args+=(-DMCP_MTKAHYPAR_DIR="$MCP_MTKAHYPAR_DIR")
# shellcheck disable=SC2206
[[ -n "${CMAKE_ARGS:-}" ]] && args+=($CMAKE_ARGS)
cmake -S . -B build "${args[@]}"
cmake --build build -j"${JOBS:-$(nproc)}"
echo "built $dest/build/bin/openroad"
