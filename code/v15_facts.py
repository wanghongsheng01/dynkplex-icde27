#!/usr/bin/env python3
"""Print the quantities quoted from the review-requested runs in ../results/v15 (see run_v15.sh), next to
the matching v13 runs. Same median-run rule as paper_facts.py. Usage: python3 v15_facts.py  (from 14_graph/code)"""
import collections
import contextlib
import glob
import io
import os
import re

with contextlib.redirect_stdout(io.StringIO()):  # paper_facts prints its own report on import
    from paper_facts import load, g, med_run, grid

W = "../results/v15"
G3 = ("email-Enron", "soc-Epinions1", "com-youtube.ungraph")


def by_cell(rows):
    d = collections.defaultdict(list)
    for r in rows:
        d[(g(r), r["k"])].append(r)
    return {key: med_run(rr) for key, rr in d.items()}


def check(rows, tag):
    bad = [(g(r), r["k"], r["seed"]) for r in rows if r["mismatch"] or r["invalid"]]
    tr = [(g(r)[:8], r["k"], r["seed"]) for r in rows if r.get("truncated")]
    print(f"  [{tag}] runs {len(rows)} mismatch/invalid {bad} truncated {tr}")


print("== LazyMC+ (adversarial, k = 1, median of three seeds)")
lp = load(f"{W}/lazymcplus_hot.jsonl")
check(lp, "lazymc+")
for (gg, k), r in sorted(by_cell(lp).items()):
    f = grid[("hot", "full")].get((gg, 1))
    x = grid[("hot", "lazymcx")].get((gg, 1))
    print(f"  {gg[:10]}: LazyMC+ {r['dyn_avg_us']:.0f} LazyMC-X {x['dyn_avg_us'] if x else float('nan'):.0f} CertKP {f['dyn_avg_us']:.1f}"
          f" LazyMC+/CertKP {r['dyn_avg_us'] / f['dyn_avg_us']:.2f} refresh/hit {r['del_global'] / max(1, r['del_hit']):.2f}")

print("\n== budget constant c (adversarial, seed 3; c = 1 from v13)")
base = {(g(r), r["k"]): r for r in load("../results/v13/hot_full.jsonl") if r["seed"] == 3}
bc = {}
for f in sorted(glob.glob(f"{W}/budget_c*.jsonl")):
    c = re.search(r"budget_c([0-9.]+)\.jsonl", f).group(1)
    rows = load(f)
    check(rows, f"c={c}")
    for r in rows:
        bc[(float(c), g(r), r["k"])] = r
for gg in G3:
    for k in (2, 3):
        b = base.get((gg, k))
        if not b:
            continue
        row = " ".join(f"c={c}:{bc[(c, gg, k)]['dyn_avg_us'] / b['dyn_avg_us']:.2f}(fb {bc[(c, gg, k)]['fallbacks']})"
                       for c in (0.25, 0.5, 2.0, 4.0) if (c, gg, k) in bc)
        print(f"  {gg[:8]} k={k}: c=1 avg {b['dyn_avg_us']:.0f} fb {b['fallbacks']} | time relative to c=1: {row}")

print("\n== k = 4")
for s in ("hotk4", "randk4"):
    F = by_cell(load(f"{W}/{s}_full.jsonl"))
    B = by_cell(load(f"{W}/{s}_bxc.jsonl"))
    check(load(f"{W}/{s}_full.jsonl") + load(f"{W}/{s}_bxc.jsonl"), s)
    for key in sorted(F):
        if key in B:
            f, b = F[key], B[key]
            print(f"  {s} {key[0][:8]}: CertKP {f['dyn_avg_us']:.1f} BX+ {b['dyn_avg_us']:.1f} ratio {b['dyn_avg_us'] / f['dyn_avg_us']:.2f}"
                  f" vsST {f['speedup_vs_st']:.1f} hits {f['del_hit']} omega {f['omega0']}->{f['omega_end']} p50 {f['dyn_p50_us']}")

print("\n== short windows (temporal, 20,000 updates, one run)")
for w in ("0.05", "0.2"):
    F = by_cell(load(f"{W}/win{w}_full.jsonl"))
    B = by_cell(load(f"{W}/win{w}_bxc.jsonl"))
    L = by_cell(load(f"{W}/lazymc_win{w}.jsonl"))
    check(load(f"{W}/win{w}_full.jsonl") + load(f"{W}/win{w}_bxc.jsonl") + load(f"{W}/lazymc_win{w}.jsonl"), f"win{w}")
    for key in sorted(F):
        f, b, l = F[key], B.get(key), L.get((key[0], 1))
        print(f"  w={w} {key[0][:8]} k={key[1]}: CertKP {f['dyn_avg_us']:.1f} BX+ {b['dyn_avg_us'] if b else float('nan'):.1f}"
              f" ratio {b['dyn_avg_us'] / f['dyn_avg_us'] if b else float('nan'):.2f} hits {f['del_hit']} vsST {f['speedup_vs_st']:.0f}"
              + (f" | LazyMC {l['dyn_avg_us']:.1f} ratio {l['dyn_avg_us'] / f['dyn_avg_us']:.2f}" if l and key[1] == 1 else ""))

print("\n== adversarial stream on WikiTalk and Pokec (one run)")
F = by_cell(load(f"{W}/hotbig_full.jsonl"))
B = by_cell(load(f"{W}/hotbig_bxc.jsonl"))
L = by_cell(load(f"{W}/lazymc_hotbig.jsonl"))
check(load(f"{W}/hotbig_full.jsonl") + load(f"{W}/hotbig_bxc.jsonl") + load(f"{W}/lazymc_hotbig.jsonl"), "hotbig")
for key in sorted(F):
    f, b, l = F[key], B.get(key), L.get((key[0], 1))
    print(f"  {key[0][:8]} k={key[1]}: CertKP {f['dyn_avg_us']:.1f} BX+ {b['dyn_avg_us'] if b else float('nan'):.1f}"
          f" ratio {b['dyn_avg_us'] / f['dyn_avg_us'] if b else float('nan'):.2f} hits {f['del_hit']} vsST {f['speedup_vs_st']:.1f} omega {f['omega0']}->{f['omega_end']}"
          + (f" | LazyMC {l['dyn_avg_us']:.0f} ratio {l['dyn_avg_us'] / f['dyn_avg_us']:.1f}" if l and key[1] == 1 else ""))

print("\n== peak memory (maximum resident set size, CertKP, k = 1)")
for f in sorted(glob.glob(f"{W}/mem_full_*.time")):
    t = open(f).read()
    m = re.search(r"(\d+)\s+maximum resident set size", t)
    if m:
        print(f"  {os.path.basename(f)[9:-5]}: {int(m.group(1)) / 2**30:.2f} GiB")
