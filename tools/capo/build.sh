#!/usr/bin/env bash
# Builds Capo 10.5 (UMpack, University of Michigan) from the public mirror
# and produces CapoTest1.exe, the Bookshelf driver that bench/capo.tcl runs.
#
#   tools/capo/build.sh [dest]        default dest: /tmp/$USER/UMpack (local
#                                     disk; the build is 60 MB and slow on NFS)
#
# Then:
#   export LD_LIBRARY_PATH=<dest>/lib
#   CAPO=<dest>/Capo/CapoTest1.exe exp/run.sh exp/e14_capo.cfg -j 4
#
# The mirror (i-markov/UMpack-32-June2026, commit 3ca0e33 at the time of
# writing) is the 2012 UMpack distribution modernized for C++20 / gcc 13;
# the system gcc 9 does not build it, so the conda gcc of openroad-env is
# used.  Only the Bookshelf path exists in this mirror: the LEF/DEF parser
# (ParserLEFDEF, DB, MetaPlacer) of the original UMpack is not included.
# `perl config` is interactive; the answers are: build yes, optimization
# yes, memory-saving no, -march=native yes, regression-only no, test no.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
dest="${1:-/tmp/$USER/UMpack}"
if [[ ! -d "$dest/.git" ]]; then
  git clone --depth 1 https://github.com/i-markov/UMpack-32-June2026 "$dest"
fi
cd "$dest"
printf 'y\ny\nn\ny\nn\nn\n' > /tmp/umpack_answers.$$
"$root/setup_openroad_env.sh" run bash -c \
  'export PATH=".:$PATH"; perl config < '"/tmp/umpack_answers.$$"' && cd Capo && ./makeOpt-gnu CapoTest1.exe'
rm -f /tmp/umpack_answers.$$
echo "built $dest/Capo/CapoTest1.exe (LD_LIBRARY_PATH=$dest/lib)"
