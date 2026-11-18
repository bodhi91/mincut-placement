#!/usr/bin/env python3
"""FastPart driver for the min-cut placer's FastPartEngine.

Server mode (what the placer uses):

    fastpart_driver.py --serve SOCKET --lanes N

One long-lived process with N persistent single-threaded Mt-KaHyPar workers
(the lanes); every region is one connection on the Unix socket carrying a
JSON line {hgr, fix, k, seed, lanes, time, upper, lower, warm, stall, out}
and answered with the result as one JSON line.  Requests run concurrently
(the placer partitions the regions of a wave in parallel), each over the
lanes it asked for; the engine imports and the worker start-up are paid
once per placement instead of once per region, which is what makes a
budget of a fraction of a second per leaf-level region meaningful.

One-shot mode (debugging):

    fastpart_driver.py HGraphFile FixFile Nparts Seed Threads Time
                       --upper u0,u1,.. [--lower l0,l1,..] [--warm file]
                       [--stall N] [--out file] [--json file]

HGraphFile is in hMETIS format ("E V 11"); FixFile has one line per
vertex, -1 (movable) or the block the vertex is fixed to (the placer's
terminals).  --upper / --lower bound the movable weight of every block.
The partition (one block per vertex, fixed vertices included) is written
to --out (default HGraphFile.part.Nparts).

What runs is FastPart's default mode (fastpart/controller.py: the
fixed-budget portfolio controller over the pinned, unpatched Mt-KaHyPar)
re-expressed for one placer subproblem:

  * the template roster (preset x mode x V-cycles x eps-variant) and the
    success-weighted chooser are FastPart's; thread counts are clipped to
    the lanes the placer grants this region (sub.threads), identical
    templates after clipping merge their weights;
  * every finished run passes through balance repair and the evaluator
    into a feasible-only pool (cap 12, one survivor per family);
  * warm V-cycle follow-ons on the incumbent during exploration and a
    highest-quality polish in the tail, as in FastPart; the tail starts
    at 80 % of the budget as in FastPart;
  * two extensions the placer needs: fixed vertices (terminals, passed to
    Mt-KaHyPar as fixed vertices and never moved by the repair) and
    per-block two-sided bounds instead of the symmetric eps window
    (the children of a region have different capacities);
  * --warm admits the placer's initial partition (the seed assignment)
    as the first pool member and the first warm follow-on;
  * --stall N stops the exploration after N consecutive runs that did not
    improve the incumbent (0 = FastPart's fixed budget).

The --kep refinement stages (AL-FM, C&C) are not run: they need an ILP
backend and budgets of minutes.

Environment: FASTPART_ROOT (default ../../FastPart relative to this
file) for the presets, MTKAHYPAR_PYTHON (default
$FASTPART_ROOT/external/mt-kahypar/build/python) for the engine module;
MCP_FASTPART_TRACE=<file> appends one JSON line per request in server mode
(tools/fastpart/trace.py tabulates it).
"""

from __future__ import annotations

import argparse
import json
import math
import os

# The spectral seed's numpy/scipy must not spawn a BLAS thread pool: the
# instances are tiny, the placer already runs several drivers in parallel
# (one per region of a wave), and numpy's bundled OpenBLAS is built with
# MAX_THREADS=64, which is a known hazard on hosts with more cores.
for _var in ("OPENBLAS_NUM_THREADS", "OMP_NUM_THREADS", "MKL_NUM_THREADS",
             "NUMEXPR_NUM_THREADS"):
    os.environ.setdefault(_var, "1")

import itertools  # noqa: E402
import random
import selectors
import socket
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
FASTPART_ROOT = Path(os.environ.get("FASTPART_ROOT", HERE.parent.parent / "FastPart"))
ENGINE_PYTHON = Path(os.environ.get(
    "MTKAHYPAR_PYTHON", FASTPART_ROOT / "external/mt-kahypar/build/python"))
PRESETS = FASTPART_ROOT / "presets"


# ---------------------------------------------------------------- hypergraph

@dataclass
class Hypergraph:
    n: int
    m: int
    pins: list[list[int]]
    edge_weight: list[int]
    vertex_weight: list[int]


def read_hgr(path: str) -> Hypergraph:
    with open(path) as f:
        lines = [ln for ln in (raw.strip() for raw in f) if ln and not ln.startswith("%")]
    head = lines[0].split()
    m, n = int(head[0]), int(head[1])
    fmt = head[2] if len(head) > 2 else "0"
    has_ew = fmt in ("1", "11")
    has_vw = fmt in ("10", "11")
    pins, ew = [], []
    for ln in lines[1:1 + m]:
        parts = ln.split()
        if has_ew:
            ew.append(int(parts[0]))
            parts = parts[1:]
        else:
            ew.append(1)
        pins.append([int(x) - 1 for x in parts])
    vw = [int(lines[1 + m + v].split()[0]) for v in range(n)] if has_vw else [1] * n
    return Hypergraph(n, m, pins, ew, vw)


def read_fixed(path: str, n: int) -> list[int]:
    if path in ("", "-"):
        return [-1] * n
    with open(path) as f:
        fixed = [int(x) for x in f.read().split()]
    if len(fixed) != n:
        raise SystemExit(f"fixed file has {len(fixed)} entries, hypergraph {n} vertices")
    return fixed


# ----------------------------------------------------------------- evaluator

def block_weights(hg: Hypergraph, labels: list[int], fixed: list[int], k: int) -> list[int]:
    """Movable weight per block (fixed vertices do not count)."""
    w = [0] * k
    for v, b in enumerate(labels):
        if fixed[v] < 0:
            w[b] += hg.vertex_weight[v]
    return w


def cut(hg: Hypergraph, labels: list[int]) -> int:
    total = 0
    for e, ps in enumerate(hg.pins):
        first = labels[ps[0]]
        for v in ps[1:]:
            if labels[v] != first:
                total += hg.edge_weight[e]
                break
    return total


def violation(w: list[int], lower: list[int], upper: list[int]) -> int:
    return sum(max(0, x - u, l - x) for x, l, u in zip(w, lower, upper))


def evaluate(hg, labels, fixed, k, lower, upper):
    w = block_weights(hg, labels, fixed, k)
    return cut(hg, labels), w, violation(w, lower, upper)


# -------------------------------------------------------------------- repair

def incidence(hg: Hypergraph) -> list[list[int]]:
    inc: list[list[int]] = [[] for _ in range(hg.n)]
    for e, ps in enumerate(hg.pins):
        for v in ps:
            inc[v].append(e)
    return inc


def repair(hg, labels, fixed, k, lower, upper, inc, max_rounds=None) -> list[int]:
    """FastPart's balance repair with per-block windows and fixed vertices:
    while a block is over its upper or under its lower bound, bulk-move the
    cheapest boundary vertices (exact cut gain) from the worst offender to
    the block with the most room, interior vertices when the boundary is
    exhausted.  Fixed vertices never move."""
    labels = list(labels)
    for v in range(hg.n):
        if fixed[v] >= 0:
            labels[v] = fixed[v]
    w = block_weights(hg, labels, fixed, k)
    if violation(w, lower, upper) == 0:
        return labels
    pin = [[0] * k for _ in range(hg.m)]
    for e, ps in enumerate(hg.pins):
        for v in ps:
            pin[e][labels[v]] += 1

    def gain(v, a, b):
        g = 0
        for e in inc[v]:
            c = pin[e]
            spans = sum(1 for x in c if x > 0)
            after = spans - (1 if c[a] == 1 else 0) + (1 if c[b] == 0 else 0)
            g += hg.edge_weight[e] * ((1 if spans > 1 else 0) - (1 if after > 1 else 0))
        return g

    def apply(v, b):
        a = labels[v]
        for e in inc[v]:
            pin[e][a] -= 1
            pin[e][b] += 1
        w[a] -= hg.vertex_weight[v]
        w[b] += hg.vertex_weight[v]
        labels[v] = b

    rounds = max_rounds if max_rounds is not None else 6 * k
    for _ in range(rounds):
        if violation(w, lower, upper) == 0:
            break
        over = max(range(k), key=lambda b: (w[b] - upper[b], w[b]))
        under = max(range(k), key=lambda b: (lower[b] - w[b], upper[b] - w[b]))
        if over == under:
            break
        need = max(w[over] - upper[over], lower[under] - w[under], 1)
        boundary = set()
        for e, c in enumerate(pin):
            if sum(1 for x in c if x > 0) > 1:
                for v in hg.pins[e]:
                    if labels[v] == over and fixed[v] < 0:
                        boundary.add(v)
        cand = sorted(boundary, key=lambda v: -gain(v, over, under))
        if not cand:
            cand = [v for v in range(hg.n) if labels[v] == over and fixed[v] < 0]
        moved = 0
        for v in cand:
            if moved >= need:
                break
            apply(v, under)
            moved += hg.vertex_weight[v]
    return labels


# --------------------------------------------------------------------- roster

@dataclass
class Template:
    name: str
    config: str
    weight: float
    eps_scale: float = 1.0
    relax: float = 1.0
    vcycles: int = 0

    def key(self):
        return (self.config, self.eps_scale, self.relax, self.vcycles)


@dataclass
class Stats:
    tried: int = 0
    valid: int = 0
    best: int | None = None

    def score(self, w: float) -> float:
        if self.tried == 0:
            return w * 1.5
        return w * (0.25 + self.valid / self.tried)


def roster(k: int, spectral_ok: bool) -> list[Template]:
    """FastPart's roster (controller.roster) with the thread dimension
    removed: every engine run is single-threaded here, so templates that
    differed only in their thread count merge (weights added)."""
    if k == 2:
        t = [
            Template("q_rb_s", "quality_rb.ini", 5.0),
            Template("q_rb_m", "quality_rb.ini", 4.0),
            Template("q_rb_l", "quality_rb.ini", 2.0),
            Template("d_rb_s", "default_rb.ini", 1.0),
            Template("dq_rb_s", "deterministic_quality_rb.ini", 1.0),
            Template("q_rb_vc1", "quality_rb.ini", 0.8, vcycles=1),
            Template("q_rb_vc2", "quality_rb.ini", 0.5, vcycles=2),
            Template("q_direct_l", "quality_direct.ini", 0.6),
            Template("q_direct_vc1", "quality_direct.ini", 0.6, vcycles=1),
            Template("hq_direct_l", "highest_quality_direct.ini", 0.3),
            Template("q_rb_xl", "quality_rb.ini", 1.0),
            Template("q_direct_xl", "quality_direct.ini", 0.5),
            Template("q_rb_rebal_s", "quality_rebal_rb.ini", 1.5),
            Template("q_rb_i40_m", "quality_rb_i40.ini", 1.5),
            Template("spec_seed", "@spectral", 0.8),
        ]
    else:
        es = 1.0 / (k - 1)
        t = [
            Template("q_direct_m", "quality_direct.ini", 4.0),
            Template("q_direct_l", "quality_direct.ini", 3.0),
            Template("q_deep_m", "quality_deep.ini", 2.0),
            Template("d_direct_m", "default_direct.ini", 1.0),
            Template("dq_direct_m", "deterministic_quality_direct.ini", 0.8),
            Template("q_direct_vc1", "quality_direct.ini", 0.8, vcycles=1),
            Template("q_direct_vc2", "quality_direct.ini", 0.7, vcycles=2),
            Template("q_rb_m", "quality_rb.ini", 0.5),
            Template("hq_direct_l", "highest_quality_direct.ini", 0.3),
            Template("q_direct_m_epsmin", "quality_direct.ini", 0.8, eps_scale=es),
            Template("q_deep_m_epsmin", "quality_deep.ini", 0.4, eps_scale=es),
            Template("q_direct_xl", "quality_direct.ini", 2.0),
            Template("q_direct_xl_vc1", "quality_direct.ini", 0.6, vcycles=1),
            Template("q_direct_xl_vc2", "quality_direct.ini", 0.5, vcycles=2),
            Template("q_rebal_direct_m", "quality_rebal_direct.ini", 0.8),
            Template("q_rebal_deep_m", "quality_rebal_deep.ini", 0.4),
            Template("q_direct_i40_m", "quality_direct_i40.ini", 1.2),
            Template("q_rb_i40_m", "quality_rb_i40.ini", 0.6),
            Template("spec_seed", "@spectral", 1.0),
        ]
    merged: dict[tuple, Template] = {}
    for x in t:
        if x.config == "@spectral" and not spectral_ok:
            continue
        if x.key() in merged:
            merged[x.key()].weight += x.weight
        else:
            merged[x.key()] = Template(x.name, x.config, x.weight, x.eps_scale,
                                       x.relax, x.vcycles)
    return list(merged.values())


# ----------------------------------------------------------------------- pool

@dataclass
class Candidate:
    labels: list[int]
    cut: int
    family: str
    template: str


@dataclass
class Pool:
    cap: int = 12
    members: list[Candidate] = field(default_factory=list)

    def admit(self, c: Candidate) -> None:
        self.members.append(c)
        if len(self.members) > self.cap:
            by_family: dict[str, list[Candidate]] = {}
            for x in sorted(self.members, key=lambda c: c.cut):
                by_family.setdefault(x.family, []).append(x)
            keep = [v[0] for v in by_family.values()]
            rest = sorted((x for v in by_family.values() for x in v[1:]),
                          key=lambda c: c.cut)
            self.members = (keep + rest)[: self.cap]

    @property
    def best(self) -> Candidate | None:
        return min(self.members, key=lambda c: c.cut) if self.members else None


# --------------------------------------------------------------------- worker

def worker_main() -> int:
    """One lane: a single-threaded Mt-KaHyPar process that executes JSON
    commands read from stdin.  "load" sets the instance (hypergraph, fixed
    vertices, block targets), "run" partitions or improves and replies
    "done <job id> <cut|fail> [message]" on stdout, "quit" exits.  One
    hypergraph object is kept per preset family (their hypergraph types
    differ) until the next load."""
    sys.path.insert(0, str(ENGINE_PYTHON))
    import mtkahypar  # noqa: E402

    mtk = mtkahypar.initialize(1, False)
    inst: dict = {}
    hgs: dict[str, object] = {}
    ctxs: dict[str, object] = {}

    def context(config: str):
        if config not in ctxs:
            ctxs[config] = mtk.context_from_file(str(PRESETS / config))
        return ctxs[config]

    def hypergraph(config: str, ctx):
        if config not in hgs:
            hg = mtk.hypergraph_from_file(inst["hgr"], ctx, mtkahypar.FileFormat.HMETIS)
            if inst["has_fixed"]:
                hg.add_fixed_vertices(inst["fixed"], inst["k"])
            hgs[config] = hg
        return hgs[config]

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        job = json.loads(line)
        cmd = job.get("cmd", "run")
        if cmd == "quit":
            break
        if cmd == "load":
            hgs.clear()
            fixed = job["fixed"]
            inst = dict(hgr=job["hgr"], k=job["k"], n=job["n"], fixed=fixed,
                        has_fixed=any(f >= 0 for f in fixed),
                        targets=job["targets"], total=job["total"])
            print(f"loaded {job['id']}", flush=True)
            continue
        jid = job["id"]
        try:
            k, total, targets = inst["k"], inst["total"], inst["targets"]
            ctx = context(job["config"])
            mtkahypar.set_seed(int(job["seed"]))
            # Upper bounds only (the engine knows no lower bound).  The
            # perfect share of a block is proportional to its bound (the
            # children of a region differ in capacity); FastPart's relaxed
            # exploration / eps-shrink variants scale the slack above it.
            factor = job.get("relax", 1.0) * job.get("eps_scale", 1.0)
            sum_u = max(1, sum(targets))
            tb = []
            for u in targets:
                share = total * u / sum_u
                tb.append(max(int(math.floor(share + (u - share) * factor + 1e-9)),
                              int(math.ceil(share - 1e-9))))
            eps_rel = max(1e-4, max(tb) * k / max(1.0, total) - 1.0)
            ctx.set_partitioning_parameters(k, eps_rel, mtkahypar.Objective.CUT)
            ctx.set_individual_target_block_weights(tb)
            ctx.logging = False
            hg = hypergraph(job["config"], ctx)
            warm = job.get("warm")
            if warm:
                labels = [int(x) for x in open(warm).read().split()]
                phg = hg.create_partitioned_hypergraph(ctx, k, labels)
                phg.improve_partition(ctx, max(1, int(job.get("vcycles", 0))))
            else:
                ctx.num_vcycles = int(job.get("vcycles", 0))
                phg = hg.partition(ctx)
            out = [phg.block_id(v) for v in range(inst["n"])]
            Path(job["out"]).write_text("\n".join(map(str, out)) + "\n")
            print(f"done {jid} {phg.cut()}", flush=True)
        except Exception as exc:  # noqa: BLE001 - report, keep the lane alive
            print(f"done {jid} fail {type(exc).__name__}: {exc}", flush=True)
    return 0


class Lane:
    """A worker process owned by the controller; survives across requests
    in server mode.  `pending` is the id of the job in flight (None when
    idle); replies to other ids are stale and dropped."""

    _ids = itertools.count(1)
    _lock = threading.Lock()

    def __init__(self):
        self.proc = None
        self.buf = b""
        self.pending: int | None = None
        self.start()

    def start(self):
        self.proc = subprocess.Popen(
            [sys.executable, str(Path(__file__).resolve()), "--worker"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.buf = b""
        self.pending = None

    @classmethod
    def next_id(cls) -> int:
        with cls._lock:
            return next(cls._ids)

    def send(self, msg: dict):
        self.proc.stdin.write((json.dumps(msg) + "\n").encode())
        self.proc.stdin.flush()

    def fileno(self):
        return self.proc.stdout.fileno()

    def read_lines(self) -> list[list[str]]:
        """Non-blocking drain of complete reply lines (after select)."""
        chunk = os.read(self.fileno(), 65536)
        if not chunk:
            return [["eof"]]
        self.buf += chunk
        out = []
        while b"\n" in self.buf:
            raw, self.buf = self.buf.split(b"\n", 1)
            out.append(raw.decode(errors="replace").split(maxsplit=3))
        return out

    def settle(self, timeout: float):
        """Make the lane idle for the next request: wait for an in-flight
        job, restart the process when it does not finish in time."""
        if self.pending is None and self.proc.poll() is None:
            return
        deadline = time.time() + timeout
        sel = selectors.DefaultSelector()
        sel.register(self, selectors.EVENT_READ)
        while self.pending is not None and time.time() < deadline:
            if self.proc.poll() is not None:
                break
            for _ in sel.select(timeout=max(0.0, deadline - time.time())):
                for parts in self.read_lines():
                    if parts and parts[0] == "done" and len(parts) > 1 \
                            and parts[1] == str(self.pending):
                        self.pending = None
        sel.close()
        if self.pending is not None or self.proc.poll() is not None:
            self.kill()
            self.start()

    def kill(self):
        try:
            self.proc.kill()
            self.proc.wait(timeout=2.0)
        except Exception:  # noqa: BLE001
            pass

    def quit(self):
        try:
            self.send(dict(cmd="quit"))
            self.proc.wait(timeout=2.0)
        except Exception:  # noqa: BLE001
            self.kill()


# ----------------------------------------------------------------- controller

def spectral_available() -> bool:
    try:
        import numpy  # noqa: F401
        import scipy  # noqa: F401
    except ImportError:
        return False
    return (FASTPART_ROOT / "fastpart" / "spectral.py").exists()


def spectral_seed(hg: Hypergraph, fixed, k, lower, upper, seed, dims):
    """FastPart's spectral seed (fastpart/spectral.py) on this instance;
    fixed vertices are stamped afterwards and the repair takes care of
    the window."""
    if str(FASTPART_ROOT) not in sys.path:
        sys.path.insert(0, str(FASTPART_ROOT))
    import warnings
    from fastpart import spectral  # noqa: E402
    from fastpart.hgr import Hypergraph as FpHypergraph  # noqa: E402

    fhg = FpHypergraph(n=hg.n, m=hg.m, pins=hg.pins, edge_weight=hg.edge_weight,
                       vertex_weight=hg.vertex_weight)
    W = sum(hg.vertex_weight)
    eps_pct = max(0.0, 100.0 * (max(upper) / max(1, W) - 1.0 / k))
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        labels = spectral.kmeans_seed(fhg, k, eps_pct, dims=dims, seed=seed)
    if labels is None:
        return None
    for v in range(hg.n):
        if fixed[v] >= 0:
            labels[v] = fixed[v]
    return labels


@dataclass
class Request:
    hgr: str
    fix: str
    k: int
    seed: int
    time: float
    upper: list[int]
    lower: list[int]
    warm: str | None = None
    stall: int = 0
    out: str | None = None
    workdir: str | None = None
    keep: bool = False
    verbose: bool = False


def solve(a: Request, lanes: list[Lane], spectral_ok: bool) -> dict:
    """FastPart's default-mode controller on one subproblem, over the given
    (idle) lanes.  Returns the result dictionary; the partition is written
    to a.out (fixed vertices included)."""
    t0 = time.time()
    deadline = t0 + a.time
    hg = read_hgr(a.hgr)
    k = a.k
    fixed = read_fixed(a.fix, hg.n)
    upper = list(a.upper)
    lower = list(a.lower) if a.lower else [0] * k
    if len(upper) != k or len(lower) != k:
        return dict(ok=False, reason="--upper/--lower need one value per block")
    fixed_w = [0] * k
    for v in range(hg.n):
        if fixed[v] >= 0:
            fixed_w[fixed[v]] += hg.vertex_weight[v]
    movable = [v for v in range(hg.n) if fixed[v] < 0]
    W = sum(hg.vertex_weight[v] for v in movable)
    # Infeasible windows (the plan may be tighter than the integer
    # weights allow) are widened to the nearest feasible one.
    if sum(upper) < W:
        upper = [u + (W - sum(upper) + k - 1) // k for u in upper]
    inc = incidence(hg)
    out_path = a.out or f"{a.hgr}.part.{k}"
    wd = Path(a.workdir or Path(a.hgr).parent)
    wd.mkdir(parents=True, exist_ok=True)
    tag = f"fp{Lane.next_id()}_"
    log_lines: list[str] = []

    def log(msg):
        log_lines.append(msg)
        if a.verbose:
            print(f"[{time.time()-t0:6.2f}s] {msg}", flush=True)

    pool = Pool()
    fallback: tuple[int, int, list[int]] | None = None  # (violation, cut, labels)
    runs = 0
    stall = 0

    def ingest(labels, family, template) -> int | None:
        nonlocal fallback, stall
        if len(labels) != hg.n:
            return None
        labels = repair(hg, labels, fixed, k, lower, upper, inc)
        c, w, viol = evaluate(hg, labels, fixed, k, lower, upper)
        if viol == 0:
            before = pool.best.cut if pool.best else None
            pool.admit(Candidate(labels, c, family, template))
            log(f"{template}: cut={c} weights={w} (best={pool.best.cut})")
            stall = 0 if before is None or c < before else stall + 1
            return c
        if fallback is None or (viol, c) < fallback[:2]:
            fallback = (viol, c, labels)
        log(f"{template}: cut={c} weights={w} infeasible by {viol}")
        stall += 1
        return None

    if a.warm:
        warm0 = [int(x) for x in open(a.warm).read().split()]
        ingest(warm0, "warm", "initial")

    # Trivial instances: nothing to explore.
    if len(movable) <= 1 or hg.m == 0:
        labels = [f if f >= 0 else 0 for f in fixed]
        labels = repair(hg, labels, fixed, k, lower, upper, inc)
        Path(out_path).write_text("\n".join(map(str, labels)) + "\n")
        return dict(ok=True, cut=cut(hg, labels), feasible=True, runs=0, trivial=True,
                    wall_s=round(time.time() - t0, 3))

    use_spectral = spectral_ok and len(movable) >= 3 * k
    templates = roster(k, use_spectral)
    stats = {t.name: Stats() for t in templates}
    rng = random.Random(a.seed)

    # Lanes: load the instance into every worker.
    targets = [u + fw for u, fw in zip(upper, fixed_w)]
    total = sum(hg.vertex_weight)
    load_id = Lane.next_id()
    for ln in lanes:
        ln.send(dict(cmd="load", id=load_id, hgr=a.hgr, k=k, n=hg.n, fixed=fixed,
                     targets=targets, total=total))
    sel = selectors.DefaultSelector()
    for i, ln in enumerate(lanes):
        sel.register(ln, selectors.EVENT_READ, i)
    busy: dict[int, tuple] = {}      # lane index -> (job id, template, out file, family)
    seq = 0

    def submit(li, t: Template, family, warm=None, vcycles=None, seed=None):
        nonlocal seq
        seq += 1
        jid = Lane.next_id()
        out = str(wd / f"{tag}r{seq}.part")
        job = dict(id=jid, config=t.config, seed=seed if seed is not None else a.seed + seq,
                   vcycles=t.vcycles if vcycles is None else vcycles,
                   eps_scale=t.eps_scale, relax=t.relax, out=out, warm=warm)
        lanes[li].send(job)
        lanes[li].pending = jid
        busy[li] = (jid, t, out, family)

    def reap(timeout):
        """Collect finished jobs; returns the number reaped."""
        nonlocal runs
        n = 0
        for key, _ in sel.select(timeout=timeout):
            li = key.data
            ln = lanes[li]
            for parts in ln.read_lines():
                if parts and parts[0] == "eof":
                    # The worker died; replace it (the job is lost).
                    sel.unregister(ln)
                    busy.pop(li, None)
                    ln.kill()
                    ln.start()
                    ln.send(dict(cmd="load", id=load_id, hgr=a.hgr, k=k, n=hg.n,
                                 fixed=fixed, targets=targets, total=total))
                    sel.register(ln, selectors.EVENT_READ, li)
                    break
                if len(parts) < 3 or parts[0] != "done" or li not in busy \
                        or parts[1] != str(busy[li][0]):
                    continue
                jid, t, out, family = busy.pop(li)
                ln.pending = None
                runs += 1
                n += 1
                st = stats.setdefault(t.name, Stats())
                st.tried += 1
                if parts[2] == "fail":
                    log(f"{t.name}.s{seq}: engine failed: "
                        f"{parts[3].strip() if len(parts) > 3 else ''}")
                    continue
                try:
                    labels = [int(x) for x in open(out).read().split()]
                except (OSError, ValueError):
                    continue
                c = ingest(labels, family, f"{t.name}.j{jid}")
                if c is not None:
                    st.valid += 1
                    if st.best is None or c < st.best:
                        st.best = c
        return n

    def write_warm(labels, name):
        p = wd / (tag + name)
        p.write_text("\n".join(map(str, labels)) + "\n")
        return str(p)

    # ---- exploration: keep every lane busy until the tail ---------------
    explore_end = t0 + 0.80 * a.time
    warm_interval = max(0.5, 0.1 * a.time)
    warm_round = 0
    quality_direct = Template("warm", "quality_direct.ini", 0.0, vcycles=2)
    polish_t = Template("final-polish", "highest_quality_direct.ini", 0.0, vcycles=2)
    if a.warm and pool.best is not None:
        # The seed assignment is the first thing to improve.
        submit(0, quality_direct, "followon",
               warm=write_warm(pool.best.labels, "warm0.part"), seed=a.seed + 7000)
    stopped = False
    while time.time() < explore_end:
        if a.stall > 0 and stall >= a.stall and runs >= len(templates):
            stopped = True
            break
        free = [i for i in range(len(lanes)) if i not in busy]
        for li in free:
            due = int((time.time() - t0) / warm_interval)
            if pool.best is not None and warm_round < due:
                warm_round = due
                submit(li, quality_direct, "followon",
                       warm=write_warm(pool.best.labels, f"warm{warm_round}.part"),
                       seed=a.seed + 7000 + warm_round)
                continue
            weights = [stats[x.name].score(x.weight) for x in templates]
            t = rng.choices(templates, weights=weights)[0]
            if t.config == "@spectral":
                seq_seed = a.seed + seq + 1
                labels = spectral_seed(hg, fixed, k, lower, upper, seq_seed,
                                       dims=3 if seq % 2 else 4)
                if labels is None:
                    stats[t.name].tried += 1
                    continue
                labels = repair(hg, labels, fixed, k, lower, upper, inc)
                # spectral seed -> warm V-cycles, as in spectral_runner.py
                submit(li, Template(t.name, "quality_direct.ini", 0.0, vcycles=2),
                       "global", warm=write_warm(labels, f"spec{seq+1}.part"),
                       seed=seq_seed)
                continue
            submit(li, t, "global")
        reap(timeout=min(0.02, max(0.0, explore_end - time.time())))

    # Drain in-flight runs (bounded by the tail).
    while busy and time.time() < deadline - 0.05 * a.time:
        reap(timeout=0.02)
    # A budget shorter than one engine run must still yield a partition:
    # wait for the first in-flight run (the budget is advisory, a
    # fallback to another engine is not).
    grace = time.time() + 60.0
    while busy and pool.best is None and fallback is None and time.time() < grace:
        reap(timeout=0.05)

    # ---- tail: polish the incumbent with highest quality ----------------
    if pool.best is not None and not busy and time.time() < deadline - 0.1 * a.time:
        submit(0, polish_t, "followon",
               warm=write_warm(pool.best.labels, "final.part"), seed=a.seed + 99)
        while busy and time.time() < deadline:
            reap(timeout=0.02)
    sel.close()

    best = pool.best
    if best is not None:
        labels, c, feasible = best.labels, best.cut, True
        family, template = best.family, best.template
    elif fallback is not None:
        _, c, labels = fallback
        feasible, family, template = False, "fallback", "least-violation"
    else:
        return dict(ok=False, reason="no partition produced", log=log_lines)
    Path(out_path).write_text("\n".join(map(str, labels)) + "\n")
    res = dict(ok=True, cut=c, feasible=feasible, family=family, template=template,
               runs=runs, pool_size=len(pool.members), lanes=len(lanes),
               wall_s=round(time.time() - t0, 3), budget_s=a.time,
               stopped_early=stopped, n=hg.n, movable=len(movable),
               block_weights=block_weights(hg, labels, fixed, k),
               upper=upper, lower=lower,
               templates={n: dict(tried=s.tried, valid=s.valid, best=s.best)
                          for n, s in stats.items() if s.tried})
    if not a.keep:
        for f in wd.glob(tag + "*.part"):
            try:
                f.unlink()
            except OSError:
                pass
    return res


# --------------------------------------------------------------------- server

def _alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def serve(sock_path: str, num_lanes: int, verbose: bool, parent: int = 0) -> int:
    """Long-lived mode for the placer: `num_lanes` persistent workers, one
    request per connection (a JSON line with the fields of Request plus
    "lanes"), handled concurrently; the reply is the result as one JSON
    line.  {"cmd": "quit"} stops the server."""
    spectral_ok = spectral_available()
    lanes = [Lane() for _ in range(max(1, num_lanes))]
    free: list[Lane] = list(lanes)
    cv = threading.Condition()
    stop = threading.Event()
    # MCP_FASTPART_TRACE=<file>: append one JSON line per request (analysis
    # of what the portfolio does inside the placer; tools/fastpart/trace.py).
    trace_path = os.environ.get("MCP_FASTPART_TRACE", "")
    trace_lock = threading.Lock()

    def acquire(n: int) -> list[Lane]:
        n = max(1, min(n, len(lanes)))
        with cv:
            while len(free) < n and not stop.is_set():
                cv.wait(timeout=0.5)
            got = free[:n]
            del free[:n]
            return got

    def release(got: list[Lane]):
        for ln in got:
            ln.settle(timeout=5.0)
        with cv:
            free.extend(got)
            cv.notify_all()

    def handle(conn: socket.socket):
        try:
            data = b""
            while not data.endswith(b"\n"):
                chunk = conn.recv(65536)
                if not chunk:
                    break
                data += chunk
            req = json.loads(data.decode() or "{}")
            if req.get("cmd") == "quit":
                stop.set()
                conn.sendall(b'{"ok": true}\n')
                return
            n = int(req.pop("lanes", 1))
            t_wait = time.time()
            got = acquire(n)
            t_wait = time.time() - t_wait
            try:
                res = solve(Request(**{k: v for k, v in req.items()
                                       if k in Request.__dataclass_fields__}),
                            got, spectral_ok)
            except Exception as exc:  # noqa: BLE001
                res = dict(ok=False, reason=f"{type(exc).__name__}: {exc}")
            finally:
                release(got)
            if verbose:
                print(json.dumps(res), flush=True)
            if trace_path:
                # One line per request: what the placer asked and what the
                # portfolio did with it (level, size, budget, runs, winner).
                line = dict(level=req.get("level"), region=req.get("region"),
                            k=req.get("k"), lanes=n, wait_s=round(t_wait, 3),
                            warm=bool(req.get("warm")))
                line.update({key: res.get(key) for key in (
                    "ok", "n", "movable", "budget_s", "wall_s", "runs", "cut",
                    "feasible", "family", "template", "stopped_early", "trivial",
                    "pool_size", "templates")})
                with trace_lock, open(trace_path, "a") as tf:
                    tf.write(json.dumps(line) + "\n")
            conn.sendall((json.dumps(res) + "\n").encode())
        except (OSError, ValueError) as exc:
            try:
                conn.sendall((json.dumps(dict(ok=False, reason=str(exc))) + "\n").encode())
            except OSError:
                pass
        finally:
            conn.close()

    try:
        os.unlink(sock_path)
    except FileNotFoundError:
        pass
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(sock_path)
    srv.listen(64)
    srv.settimeout(0.5)
    print(f"fastpart server: {len(lanes)} lanes on {sock_path}", flush=True)
    threads: list[threading.Thread] = []
    while not stop.is_set():
        try:
            conn, _ = srv.accept()
        except socket.timeout:
            threads = [t for t in threads if t.is_alive()]
            if parent and not _alive(parent):
                stop.set()      # the placer is gone; do not outlive it
            continue
        th = threading.Thread(target=handle, args=(conn,), daemon=True)
        th.start()
        threads.append(th)
    for th in threads:
        th.join(timeout=10.0)
    for ln in lanes:
        ln.quit()
    srv.close()
    try:
        os.unlink(sock_path)
    except FileNotFoundError:
        pass
    return 0


def main() -> int:
    if len(sys.argv) > 1 and sys.argv[1] == "--worker":
        return worker_main()
    ap = argparse.ArgumentParser(prog="fastpart_driver")
    ap.add_argument("--serve", metavar="SOCKET", default=None,
                    help="server mode: listen on this Unix socket (see FastPartEngine)")
    ap.add_argument("--lanes", type=int, default=1, help="server mode: persistent workers")
    ap.add_argument("--parent", type=int, default=0,
                    help="server mode: exit when this process is gone")
    ap.add_argument("hgr", metavar="HGraphFile", nargs="?")
    ap.add_argument("fix", metavar="FixFile", nargs="?",
                    help="-1 per movable vertex, block per fixed; '-' for none")
    ap.add_argument("k", metavar="Nparts", type=int, nargs="?")
    ap.add_argument("seed", metavar="Seed", type=int, nargs="?")
    ap.add_argument("threads", metavar="Threads", type=int, nargs="?", help="engine lanes")
    ap.add_argument("time", metavar="Time", type=float, nargs="?", help="wall budget in seconds")
    ap.add_argument("--upper", default="", help="per-block upper bounds on the movable weight")
    ap.add_argument("--lower", default="", help="per-block lower bounds (default 0)")
    ap.add_argument("--warm", default=None, help="initial partition to improve (one block per vertex)")
    ap.add_argument("--stall", type=int, default=0,
                    help="stop after this many runs without improvement (0 = fixed budget)")
    ap.add_argument("--out", default=None)
    ap.add_argument("--json", default=None)
    ap.add_argument("--workdir", default=None)
    ap.add_argument("--keep", action="store_true", help="keep the per-run partition files")
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()
    if a.serve:
        return serve(a.serve, a.lanes, a.verbose, a.parent)
    if a.hgr is None or a.k is None or a.time is None or not a.upper:
        ap.error("HGraphFile FixFile Nparts Seed Threads Time --upper are required")
    req = Request(hgr=a.hgr, fix=a.fix or "-", k=a.k, seed=a.seed or 0, time=a.time,
                  upper=[int(math.ceil(float(x))) for x in a.upper.split(",")],
                  lower=[int(math.floor(float(x))) for x in a.lower.split(",")] if a.lower else [],
                  warm=a.warm, stall=a.stall, out=a.out, workdir=a.workdir, keep=a.keep,
                  verbose=a.verbose)
    lanes = [Lane() for _ in range(max(1, a.threads or 1))]
    try:
        res = solve(req, lanes, spectral_available())
    finally:
        for ln in lanes:
            ln.quit()
    print(json.dumps(res))
    if a.json:
        Path(a.json).write_text(json.dumps(res, indent=1) + "\n")
    return 0 if res.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
