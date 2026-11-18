#!/usr/bin/env bash
# Builds the engine FastPart runs on -- the pinned, unpatched upstream
# Mt-KaHyPar (commit 6271a58, the one FastPart/scripts/setup_mtkahypar.sh
# pins) with its Python module -- and links it where FastPart expects it
# (FastPart/external/mt-kahypar), so that both FastPart's own CLI and the
# min-cut placer's driver (fastpart_driver.py) find it.
#
#   tools/fastpart/build.sh [dest]     default dest: /tmp/$USER/mt-kahypar
#                                      (local disk; NFS builds are slow)
#
# Then (with the openroad-env active, e.g. through setup_openroad_env.sh run):
#   python3 -m pip install numpy scipy        # FastPart's spectral seed
#   mincut_placement ... -engine fastpart \
#       -fastpart_path tools/fastpart/fastpart_driver.py -fastpart_time 5
#
# Differences to FastPart's setup script: the conda toolchain of
# openroad-env is used (its Python 3.12 is the one the module is built
# for), and pthread is linked explicitly because the conda linker's
# --as-needed drops it on this RHEL 8 glibc, which breaks the link of the
# MtKaHyPar binary.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
dest="${1:-/tmp/$USER/mt-kahypar}"
pin=6271a58cfc6492cb97a34502fa9040cab0e64092

if [ ! -d "$dest" ]; then
  git clone -q https://github.com/kahypar/mt-kahypar.git "$dest"
fi
cd "$dest"
git fetch -q origin "$pin" 2>/dev/null || true
git checkout -q "$pin"
git submodule update --init --recursive -q

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=ON -DKAHYPAR_PYTHON=ON -DKAHYPAR_DOWNLOAD_TBB=ON \
  -DKAHYPAR_DISABLE_HWLOC=ON \
  -DPYTHON_EXECUTABLE="$(command -v python3)" \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,--no-as-needed -lpthread" \
  -DCMAKE_SHARED_LINKER_FLAGS="-Wl,--no-as-needed -lpthread" \
  -DCMAKE_MODULE_LINKER_FLAGS="-Wl,--no-as-needed -lpthread"
cmake --build build --parallel "$(nproc)" --target mtkahypar_python MtKaHyPar

mkdir -p "$root/FastPart/external"
ln -sfn "$dest" "$root/FastPart/external/mt-kahypar"
echo "python module: $dest/build/python"
echo "linked as:     $root/FastPart/external/mt-kahypar"
