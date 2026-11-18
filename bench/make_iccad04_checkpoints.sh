#!/usr/bin/env bash
# Converts the ICCAD04 Bookshelf benchmarks (iccad04/IBM/ibmXX, the RePlAce
# repository's modified set) into harness checkpoints
# bench/designs/ibmXX_iccad04/{floorplan.db,vars.tcl,stats.json}:
#   bench/bookshelf2lefdef.py  Bookshelf -> LEF/DEF (scratch, deleted after)
#   bench/make_iccad04_checkpoints.tcl   LEF/DEF -> floorplan.db
# Macros are fixed at RePlAce's positions (iccad04/IBM/ibmXX/ibmXX.macros.pl,
# from bench/pb_macros.py); a design without that file keeps its macros
# unplaced and is skipped with a message.
#
#   bench/make_iccad04_checkpoints.sh [ibm01 ibm02 ...]     default: all 18
#   OPENROAD=<binary> overrides the default build-kway snapshot.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OPENROAD="${OPENROAD:-$ROOT/OpenROAD/build/bin/openroad}"
SCRATCH="/tmp/$USER/iccad04_lefdef"
designs=("$@")
[[ ${#designs[@]} -gt 0 ]] || designs=($(cd "$ROOT/iccad04/IBM" && ls -d ibm* | sort))
mkdir -p "$SCRATCH"
for d in "${designs[@]}"; do
  src="$ROOT/iccad04/IBM/$d"
  macros="$src/$d.macros.pl"
  args=()
  if [[ -f "$macros" ]]; then
    args+=(--macros "$macros")
  elif grep -q . <(awk 'NF>=3 && $1 !~ /^(UCLA|#|Num)/ && NF==3 && $3>16' "$src/$d.nodes" | head -1); then
    echo "$d: no $d.macros.pl and the design has multi-row nodes; skipped"
    continue
  fi
  python3 "$ROOT/bench/bookshelf2lefdef.py" "$src/$d.aux" "$SCRATCH/$d" "${args[@]}" || { echo "$d: conversion failed"; continue; }
  MCP_ICCAD04_SRC="$SCRATCH/$d" MCP_DESIGN="${d}_iccad04" \
    "$ROOT/setup_openroad_env.sh" run "$OPENROAD" -no_splash -exit \
    "$ROOT/bench/make_iccad04_checkpoints.tcl" 2>&1 | grep -E "^${d}_iccad04:|checkpoint written|ERROR|WARNING ODB-1220" | grep -v "ODB-1220"
  rm -rf "$SCRATCH/$d"
done
rmdir "$SCRATCH" 2>/dev/null || true
