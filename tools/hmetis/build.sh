#!/usr/bin/env bash
# Builds the 32-bit hMETIS driver against the SpecPart copy of libhmetis.a
# (hMETIS 1.5 is only available as an i386 archive, so it cannot be linked
# into the 64-bit openroad binary; the placer drives it through files).
#
#   tools/hmetis/build.sh            -> tools/hmetis/hmetis_driver
#   mincut_placement -engine hmetis -hmetis_path tools/hmetis/hmetis_driver
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
lib="${HMETIS_LIB:-/path/to/libhmetis.a}"
g++ -O2 -m32 -std=c++17 "$here/hmetis_driver.cpp" "$lib" -o "$here/hmetis_driver"
echo "built $here/hmetis_driver"
