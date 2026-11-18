#!/usr/bin/env bash
#
# Runs an experiment grid (designs x variants x seeds) and writes one metrics
# JSON per run.
#
#   exp/run.sh exp/e0_smoke.cfg [-j N] [--force] [--dry-run] [--keep-db] [--keep-def]
#
# The .cfg is a bash fragment that sets:
#   EXP        experiment name; results go to results/$EXP/
#   DESIGNS    list of checkpoints in bench/designs (e.g. "gcd_nangate45")
#   SEEDS      list of seeds
#   THREADS    threads per run (default 1)
#   DPO        1 = run improve_placement after detailed_placement in the
#              evaluation (MCP_DPO; default 0)
#   VARIANTS   array of "name|gpl|<mode>", "name|capo|<CapoTest1 args>[|<pad sites>]"
#              (CAPO = path to CapoTest1.exe; bench/capo.tcl) or "name|mincut|<mincut_placement args>"
#              or "name|mincut|<args>|<post>|<pre>" (<post>: gpl or incremental,
#              a global_placement pass on the min-cut result; <pre>: gpl_init or gpl, a
#              seed placement before it; see bench/mincut.tcl; "" or none = off)
#              (<mode>: default timing_driven routability_driven skip_nesterov nopad krow)
#   MEM_MIN_GB a run is started only when this much memory is available
#              (default 40; global_route needs tens of GB on wide floorplans)
#   MEM_MAX_GB address-space cap per run (ulimit -v; default 120)
# Per run artifacts: <design>__<variant>__s<seed>.{json,log,eval.json,
# metrics.json,mcp.json,time}; <prefix>.lock/ while a run is in flight (so
# that several hosts can share one grid; the .json records the host).  Placed databases go to the scratch directory
# ($MCP_SCRATCH, default $ROOT/scratch/$EXP, never /tmp) and are deleted unless
# --keep-db.  --keep-def saves the legal placement (after detailed_placement,
# before DPO, the primary metric) as <prefix>.def.gz next to the .json (the
# solution archive).  Existing .json results are skipped (resumable) unless
# --force.
# A run whose global_route crashed keeps its placement metrics ("PART",
# final.grt_wl null in the .json; the .stdout is kept).

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OPENROAD="${OPENROAD:-$ROOT/OpenROAD/build/bin/openroad}"
# setup_openroad_env.sh wraps the binary in the project's conda environment
# when present; a plain installation runs the binary directly.
if [[ -x "$ROOT/setup_openroad_env.sh" ]]; then
  RUNNER=("$ROOT/setup_openroad_env.sh" run)
else
  RUNNER=()
fi
TIME_BIN="${TIME_BIN:-/usr/bin/time}"

usage() { sed -n '2,20p' "$0"; exit 1; }

[[ $# -ge 1 ]] || usage
CFG="$1"; shift
JOBS=1; FORCE=0; DRY=0; KEEP_DB=0; KEEP_DEF=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    -j) JOBS="$2"; shift 2 ;;
    --force) FORCE=1; shift ;;
    --dry-run) DRY=1; shift ;;
    --keep-db) KEEP_DB=1; shift ;;
    --keep-def) KEEP_DEF=1; shift ;;
    *) usage ;;
  esac
done

[[ -f "$CFG" ]] || { echo "no such config: $CFG" >&2; exit 1; }
THREADS=1
DPO=0
VARIANTS=()
# shellcheck disable=SC1090
source "$CFG"
: "${EXP:?EXP not set in $CFG}" "${DESIGNS:?DESIGNS not set}" "${SEEDS:?SEEDS not set}"
[[ ${#VARIANTS[@]} -gt 0 ]] || { echo "VARIANTS empty in $CFG" >&2; exit 1; }

RESULTS="$ROOT/results/$EXP"
SCRATCH="${MCP_SCRATCH:-$ROOT/scratch/$EXP}"
mkdir -p "$RESULTS" "$SCRATCH"

run_one() {
  local prefix="$RESULTS/${1}__${2}__s${5}"
  lock_held=0
  run_one_locked "$@"
  local rc=$?
  # Release only a lock this invocation took (a --dry-run or a second
  # runner on the same host must not drop another run's lock).
  [[ $lock_held -eq 1 ]] && rm -rf "$prefix.lock"
  return $rc
}

run_one_locked() {
  local design="$1" vname="$2" kind="$3" vargs="$4" seed="$5"
  local post="${6:-none}" pre="${7:-none}"
  local prefix="$RESULTS/${design}__${vname}__s${seed}"
  local db="$SCRATCH/${design}__${vname}__s${seed}.db"
  if [[ $FORCE -eq 0 && -f "$prefix.json" ]]; then
    echo "skip  $(basename "$prefix") (done)"
    return 0
  fi
  if [[ ! -f "$ROOT/bench/designs/$design/floorplan.db" ]]; then
    echo "skip  $(basename "$prefix") (no checkpoint for $design)"
    return 0
  fi
  # Lock (atomic mkdir on the shared results folder): several hosts may
  # work on one grid at the same time (E25: host-A and host-B); a run in flight
  # elsewhere has no .json yet.  A stale lock (host crash) must be removed
  # by hand: rmdir <prefix>.lock.
  if [[ $DRY -eq 0 ]] && ! mkdir "$prefix.lock" 2>/dev/null; then
    echo "skip  $(basename "$prefix") (locked: in flight on $(cat "$prefix.lock/host" 2>/dev/null || echo another host))"
    return 0
  fi
  if [[ $DRY -eq 0 ]]; then
    lock_held=1
    hostname -s > "$prefix.lock/host"
  fi
  local script knobs
  local -a envs=(MCP_DESIGN="$design" MCP_SEED="$seed" MCP_THREADS="$THREADS"
                 MCP_OUT="$prefix" MCP_DB_OUT="$db" MCP_DPO="$DPO")
  [[ $KEEP_DEF -eq 1 ]] && envs+=(MCP_DEF_OUT="$prefix.def")
  case "$kind" in
    gpl)
      script="$ROOT/bench/gpl_baseline.tcl"
      envs+=(MCP_MODE="$vargs")
      knobs=("mode=$vargs")
      ;;
    mincut)
      script="$ROOT/bench/mincut.tcl"
      envs+=(MCP_ARGS="$vargs -scratch_dir $SCRATCH" MCP_POST="$post"
             MCP_PRE="$pre" MCP_SCRATCH="$SCRATCH")
      # <pre> = capo: Capo as the seed (bench/mincut.tcl), with the Capo
      # arm's environment (CAPO; MCP_CAPO_ARGS / _PAD / _SOFT_BLOCKAGES
      # exported by the cfg).
      [[ "$pre" == capo ]] && envs+=(MCP_CAPO="${CAPO:?CAPO (path to CapoTest1.exe) not set}")
      knobs=("args=$vargs" "post=$post" "pre=$pre")
      ;;
    capo)
      script="$ROOT/bench/capo.tcl"
      # <post> field of the variant = padding sites per side (MCP_CAPO_PAD)
      [[ "$post" == none ]] && post=0
      envs+=(MCP_CAPO="${CAPO:?CAPO (path to CapoTest1.exe) not set}" MCP_CAPO_ARGS="$vargs"
             MCP_CAPO_PAD="${post:-0}" MCP_SCRATCH="$SCRATCH")
      knobs=("args=$vargs" "pad=${post:-0}")
      ;;
    pl)
      # <vargs> = path of a .pl (exp/fold.py output) with @DESIGN@ and
      # @SEED@ placeholders; bench/pl_place.tcl loads it on the checkpoint
      # and the standard evaluation follows (E27 Phase 4).
      script="$ROOT/bench/pl_place.tcl"
      local plfile="${vargs//@DESIGN@/$design}"
      plfile="${plfile//@SEED@/$seed}"
      if [[ ! -f "$plfile" ]]; then
        echo "skip  $(basename "$prefix") (no $plfile)"
        return 0
      fi
      envs+=(MCP_PL="$plfile" MCP_VARIANT="$vname")
      knobs=("pl=$plfile")
      ;;
    *) echo "unknown variant kind '$kind' in $vname" >&2; return 1 ;;
  esac
  if [[ $DRY -eq 1 ]]; then
    echo "run   $(basename "$prefix")"
    return 0
  fi
  # Memory gate: global_route needs tens of GB on the wide k-row floorplans
  # and the machine is shared; wait until MEM_MIN_GB (cfg, default 40) of
  # memory is available before starting another run.
  local avail
  while avail=$(awk '/MemAvailable/ {printf "%d", $2/1048576}' /proc/meminfo) \
        && [[ $avail -lt ${MEM_MIN_GB:-40} ]]; do
    sleep 60
  done
  echo "run   $(basename "$prefix")"
  rm -f "$prefix".{json,log,eval.json,metrics.json,mcp.json,time}
  # MEM_MAX_GB (cfg, default 120): address-space cap per run, so that a
  # global_route that keeps growing (130+ GB on a 2-row swerv) dies with
  # bad_alloc -- leaving the partial eval -- instead of taking the machine.
  ( ulimit -v $(( ${MEM_MAX_GB:-120} * 1024 * 1024 )) 2>/dev/null
    exec env "${envs[@]}" "$TIME_BIN" -f "%e %M" -o "$prefix.time" \
      "${RUNNER[@]}" "$OPENROAD" -no_splash -exit \
        -log "$prefix.log" -metrics "$prefix.metrics.json" "$script" \
        > "$prefix.stdout" 2>&1 )
  local rc=$?
  if [[ ! -f "$prefix.eval.json" ]]; then
    echo "FAIL  $(basename "$prefix") (exit $rc, see $prefix.stdout)"
    return 1
  fi
  # mcp_eval saves the placement metrics before global_route; a run whose
  # routing crashed (exit != 0, "routing" field in the eval) is kept as a
  # partial result (no grt_wl / t_grt / timing) and its stdout is kept.
  local partial=0
  if grep -q '"routing"' "$prefix.eval.json"; then
    partial=1
    echo "PART  $(basename "$prefix") (placed; routing failed, exit $rc, see $prefix.stdout)"
  elif [[ $rc -ne 0 ]]; then
    echo "FAIL  $(basename "$prefix") (exit $rc, see $prefix.stdout)"
    return 1
  fi
  python3 "$ROOT/bench/collect.py" "$prefix" "$prefix.json" \
    --knobs "variant=$vname" "kind=$kind" "${knobs[@]}" "threads=$THREADS" \
            "dpo=$DPO" "host=$(hostname -s)" \
    || { echo "FAIL  collect $(basename "$prefix")"; return 1; }
  [[ $partial -eq 1 ]] || rm -f "$prefix.stdout"
  [[ $KEEP_DB -eq 1 ]] || rm -f "$db"
  [[ -f "$prefix.def" ]] && gzip -f "$prefix.def"
  return 0
}

fails=0
for design in $DESIGNS; do
  for variant in "${VARIANTS[@]}"; do
    IFS='|' read -r vname kind vargs post pre <<< "$variant"
    post="${post:-none}"
    pre="${pre:-none}"
    for seed in $SEEDS; do
      if [[ $JOBS -gt 1 ]]; then
        while [[ $(jobs -rp | wc -l) -ge $JOBS ]]; do
          wait -n || fails=$((fails + 1))
        done
        run_one "$design" "$vname" "$kind" "$vargs" "$seed" "$post" "$pre" &
      else
        run_one "$design" "$vname" "$kind" "$vargs" "$seed" "$post" "$pre" \
          || fails=$((fails + 1))
      fi
    done
  done
done
if [[ $JOBS -gt 1 ]]; then
  while [[ $(jobs -rp | wc -l) -gt 0 ]]; do
    wait -n || fails=$((fails + 1))
  done
fi

echo "results in $RESULTS ($fails failures)"
[[ $fails -eq 0 ]]
