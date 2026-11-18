#!/usr/bin/env python3
"""Per-level attribution of the from-scratch loss (E8).

    exp/seedlevels.py results/e8_seedlevels [--gpl results/e0_baselines] [--png out.png]

For every design prints HPWL after legalization as a function of the number
of seeded levels L (variants L00, L01, ..., Lall), the ratio to the fully
seeded run and to gpl, and the marginal loss of each level: the HPWL gained
by seeding level L as well, i.e. HPWL(L) - HPWL(L+1); a large value means
the from-scratch decision at level L is what costs wirelength.
"""

import glob
import json
import os
import re
import statistics
import sys


def load(folder, gpl_folder):
    data = {}
    for path in glob.glob(os.path.join(folder, "*.json")):
        if path.endswith((".eval.json", ".metrics.json", ".mcp.json")):
            continue
        name = os.path.basename(path)[: -len(".json")]
        design, variant, seed = name.split("__")
        with open(path) as f:
            run = json.load(f)
        hpwl = run.get("final", {}).get("hpwl")
        if hpwl is None:
            continue
        data.setdefault(design, {}).setdefault(variant, []).append(hpwl)
    gpl = {}
    if gpl_folder:
        for path in glob.glob(os.path.join(gpl_folder, "*__gpl_default__*.json")):
            if path.endswith((".eval.json", ".metrics.json", ".mcp.json")):
                continue
            design = os.path.basename(path).split("__")[0]
            with open(path) as f:
                gpl.setdefault(design, []).append(json.load(f)["final"]["hpwl"])
    return data, {d: statistics.fmean(v) for d, v in gpl.items()}


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    folder = argv[1]
    gpl_folder = argv[argv.index("--gpl") + 1] if "--gpl" in argv else None
    png = argv[argv.index("--png") + 1] if "--png" in argv else None
    data, gpl = load(folder, gpl_folder)

    curves = {}
    for design in sorted(data):
        variants = data[design]
        levels = sorted(
            int(m.group(1))
            for v in variants
            for m in [re.fullmatch(r"L(\d+)", v)]
            if m
        )
        means = {L: statistics.fmean(variants["L%02d" % L]) for L in levels}
        full = statistics.fmean(variants["Lall"]) if "Lall" in variants else None
        base = means[levels[0]]
        print(design)
        print(
            "  %5s %10s %8s %8s %8s   %s"
            % ("L", "hpwl", "/scratch", "/seeded", "/gpl", "marginal gain of seeding level L")
        )
        for i, L in enumerate(levels):
            nxt = means[levels[i + 1]] if i + 1 < len(levels) else full
            gain = (means[L] - nxt) if nxt is not None else 0.0
            print(
                "  %5d %10.0f %8.3f %8.3f %8s   %+8.0f (%+5.1f %%)"
                % (
                    L,
                    means[L],
                    means[L] / base,
                    means[L] / full if full else float("nan"),
                    "%.3f" % (means[L] / gpl[design]) if design in gpl else "-",
                    gain,
                    100.0 * gain / base,
                )
            )
        if full is not None:
            print(
                "  %5s %10.0f %8.3f %8.3f %8s"
                % (
                    "all",
                    full,
                    full / base,
                    1.0,
                    "%.3f" % (full / gpl[design]) if design in gpl else "-",
                )
            )
        curves[design] = (levels, [means[L] / base for L in levels], full / base if full else None)

    if png:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt

        fig, ax = plt.subplots(figsize=(7, 4))
        for design, (levels, ratios, full) in curves.items():
            line, = ax.plot(levels, ratios, marker="o", label=design)
            if full is not None:
                ax.axhline(full, color=line.get_color(), ls=":", lw=1)
        ax.set_xlabel("levels driven by the gpl seed (L)")
        ax.set_ylabel("HPWL after dpl / from scratch")
        ax.set_title("E8: seeded levels < L, from scratch from L on (dotted: all levels seeded)")
        ax.grid(alpha=0.3)
        ax.legend()
        fig.tight_layout()
        fig.savefig(png, dpi=130)
        print("wrote", png)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
