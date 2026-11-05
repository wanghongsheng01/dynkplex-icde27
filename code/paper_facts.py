#!/usr/bin/env python3
"""Print every quantity quoted in the text of the experiments section, computed from the raw results
(same median-run rule as make_paper.py). Usage: python3 paper_facts.py  (from 14_graph/code)"""
import collections
import glob
import json
import os
import statistics as S

V = os.environ.get("KPLEX_RESULTS", "../results/v13")
TYP = ("rand", "delins", "temporal", "tinc")


def load(f):
    return [json.loads(l) for l in open(f) if l.startswith("{")] if os.path.exists(f) else []


def g(r):
    return os.path.basename(r["graph"]).replace(".txt", "")


def med_run(rows):
    rows = sorted(rows, key=lambda r: r["dyn_avg_us"])
    return rows[len(rows) // 2]


cells = collections.defaultdict(list)
for f in glob.glob(f"{V}/*_*.jsonl"):
    stream, var = os.path.basename(f)[:-6].split("_", 1)
    if stream == "lj":
        for r in load(f):
            cells[(r["stream"], var, "soc-LiveJournal1", 1)].append(r)
        continue
    if stream in ("lazymc", "lazymcx"):
        var, stream = stream, None
    for r in load(f):
        cells[(r["stream"], var, g(r), r["k"])].append(r)
grid = collections.defaultdict(dict)
for (s, v, gg, k), rows in cells.items():
    grid[(s, v)][(gg, k)] = med_run(rows)
allr = [r for rows in cells.values() for r in rows]


def rng(xs, f="%.1f"):
    xs = sorted(xs)
    return (f + "--" + f + " (median " + f + ", n=%d)") % (xs[0], xs[-1], S.median(xs), len(xs)) if xs else "n/a"


print("runs", len(allr), "checkpoints", sum(r["samples"] for r in allr), "mismatch", sum(r["mismatch"] for r in allr),
      "invalid", sum(r["invalid"] for r in allr))
print("truncated runs:", sorted({(s, v, gg, k) for (s, v, gg, k), rows in cells.items() if any(r.get("truncated") for r in rows)}))
print("truncated updates range:", sorted(r["updates"] for r in allr if r.get("truncated")))

print("\n== typical streams (CertKP)")
T = [(s, c, r) for (s, v), d in grid.items() if v == "full" and s in TYP for c, r in d.items()]
print("max p50 us", max(r["dyn_p50_us"] for _, _, r in T), "  speedup vs ST", rng([r["speedup_vs_st"] for _, _, r in T], "%.0f"))
print("cells avg<=10us:", sum(r["dyn_avg_us"] <= 10 for _, _, r in T), "of", len(T))
print("slowest cells (avg us):", sorted(((round(r["dyn_avg_us"]), s, c) for s, c, r in T), reverse=True)[:5])
print("smallest speedups:", sorted((round(r["speedup_vs_st"]), s, c) for s, c, r in T)[:4])
print("ST ms range", min(r["st_avg_ms"] for _, _, r in T), max(r["st_avg_ms"] for _, _, r in T))
for base in ("basicx", "bxc"):
    nohit, hit = [], []
    for s, c, r in T:
        b = grid.get((s, base), {}).get(c)
        if not b:
            continue
        (hit if r["del_hit"] > 0 or b["del_hit"] > 0 else nohit).append(b["dyn_avg_us"] / r["dyn_avg_us"])
    print(f"{base}/CertKP: no-hit cells {rng(nohit)} ; hit cells {rng(hit)} ; all median {S.median(nohit + hit) if nohit + hit else 'n/a'}")
core_share = [r["core_s"] / r["dyn_total_s"] for s, c, r in T if r["del_hit"] == 0 and r["dyn_total_s"] > 0 and r["core_s"] > 0]
print("core-maintenance share of CertKP time in no-hit cells", rng(core_share, "%.2f"))
print("hits per typical run", min(r["del_hit"] for _, _, r in T), max(r["del_hit"] for _, _, r in T))
for gg in ("soc-pokec-relationships", "soc-LiveJournal1", "wiki-Talk"):
    print(gg, "p50 us", sorted({round(r["dyn_p50_us"], 2) for s, c, r in T if c[0] == gg}),
          "core us/update", sorted({round(r["core_s"] / max(1, r["updates"]) * 1e6, 1) for s, c, r in T if c[0] == gg}))

print("\n== tails (CertKP, ST = solver-reported)")
for s in TYP + ("hot",):
    d = [r for c, r in grid.get((s, "full"), {}).items() if not r.get("truncated") and r["st_avg_ms"] > 0]
    if d:
        print(s, "max/ST", rng([r["dyn_max_ms"] / r["st_avg_ms"] for r in d], "%.2f"), " p99/ST", rng([r["dyn_p99_us"] / 1e3 / r["st_avg_ms"] for r in d], "%.3f"))
for s in TYP + ("hot",):
    F = {(g(r), r["k"], r["seed"]): r for r in load(f"{V}/{s}_full.jsonl")}
    B = {(g(r), r["k"], r["seed"]): r for r in load(f"{V}/{s}_bxc.jsonl")}
    x = [F[key]["dyn_max_ms"] / B[key]["dyn_max_ms"] for key in F if key in B and not F[key].get("truncated") and not B[key].get("truncated") and B[key]["dyn_max_ms"] > 0]
    if x:
        print(s, "CertKP max / BasicX+ max (paired by seed)", rng(x, "%.2f"))

print("\n== adversarial stream")
H = grid.get(("hot", "full"), {})
for k in (1, 2, 3):
    for base in ("st", "basicx", "bxc", "basic"):
        xs = []
        for (gg, kk), r in H.items():
            if kk != k or gg == "com-dblp.ungraph" or r.get("truncated"):
                continue
            if base == "st":
                xs.append(r["speedup_vs_st"])
            else:
                b = grid.get(("hot", base), {}).get((gg, k))
                if b:
                    xs.append(b["dyn_avg_us"] / r["dyn_avg_us"])
        print(f"k={k} CertKP vs {base} (Enron/Epinions/YouTube): {rng(xs)}")
for (gg, k), r in sorted(H.items()):
    b = grid.get(("hot", "bxc"), {}).get((gg, k), {})
    print(f"  {gg} k={k}: avg {r['dyn_avg_us']:.0f}us ST {r['st_avg_ms']:.1f}ms xST {r['speedup_vs_st']:.1f} hits {r['del_hit']} glob {r['del_global']} "
          f"fb {r['fallbacks']} swap {r['del_swap']} builds {r['cache_builds']} sub {r['cache_sub']} subinv {r['cache_sub_inv']} "
          f"max {r['dyn_max_ms']:.0f}ms trunc {r.get('truncated')} upd {r['updates']} | BX+ {b.get('dyn_avg_us', float('nan')):.0f}us core_n/glob {b.get('coresub_n', 0) / max(1, b.get('del_global', 1)):.0f}")
print("updates/s CertKP adversarial:", {f"{gg[:8]}{k}": round(1e6 / r["dyn_avg_us"]) for (gg, k), r in H.items()})

print("\n== LazyMC (k=1)")
for s in TYP + ("hot",):
    for (gg, k), a in sorted(grid.get((s, "lazymc"), {}).items()):
        b = grid.get((s, "full"), {}).get((gg, 1))
        x = grid.get((s, "lazymcx"), {}).get((gg, 1))
        if b:
            print(f"  {s} {gg}: LazyMC {a['dyn_avg_us']:.1f} LazyMC-X {x['dyn_avg_us'] if x else float('nan'):.1f} CertKP {b['dyn_avg_us']:.1f} ratio {a['dyn_avg_us'] / b['dyn_avg_us']:.2f}"
                  f" ratioX {x['dyn_avg_us'] / b['dyn_avg_us'] if x else float('nan'):.2f} refresh/hit {a['del_global'] / max(1, a['del_hit']):.2f} ST {a['st_avg_ms']:.1f}")
for v in ("full", "bxc", "basicx", "lazymc"):
    for r in cells.get(("rand", v, "soc-LiveJournal1", 1), []) + cells.get(("delins", v, "soc-LiveJournal1", 1), []):
        print(f"  LJ {v} {r['stream']}: avg {r['dyn_avg_us']:.1f} p50 {r['dyn_p50_us']} max {r['dyn_max_ms']:.1f} init {r['init_s']:.2f} hits {r['del_hit']} glob {r['del_global']} fb {r['fallbacks']} builds {r['cache_builds']} shrink {r['del_shrink']} st {r['st_avg_ms']:.0f} stw {r['st_wall_ms']:.0f} capped {r['m_capped']}")

print("\n== ablation (adversarial, seed 3)")
ab = {}
for v in ("basic", "repair", "cache", "full", "bxc", "basicx"):
    for r in load(f"{V}/hot_{v}.jsonl"):
        if r["seed"] == 3:
            ab[(v, g(r), r["k"])] = r
G3 = ("email-Enron", "soc-Epinions1", "com-youtube.ungraph")
for k in (1, 2, 3):
    for gg in G3:
        f = {v: ab.get((v, gg, k)) for v in ("basic", "repair", "cache", "full", "bxc", "basicx")}
        if all(f.values()):
            print(f"  {gg[:8]} k={k}: " + " ".join(f"{v}={f[v]['speedup_vs_st']:.1f}" for v in f) +
                  f" | repair succ {f['repair']['del_swap'] / max(1, f['repair']['del_hit']):.2f} cache/repair {f['cache']['speedup_vs_st'] / f['repair']['speedup_vs_st']:.1f}"
                  f" full/cache {f['full']['speedup_vs_st'] / f['cache']['speedup_vs_st']:.2f} basic/bxc(avg) {f['bxc']['dyn_avg_us'] / f['basic']['dyn_avg_us']:.2f}"
                  f" sweeps share {1 - f['full']['cache_sub_inv'] / max(1, f['full']['cache_sub']):.3f}")

print("\n== initialisation (s), seed 3, rand")
for gg in ("com-youtube.ungraph", "wiki-Talk", "soc-pokec-relationships"):
    for k in (1, 2, 3):
        row = {v: grid.get(("rand", v), {}).get((gg, k), {}).get("init_s") for v in ("full", "bxc", "basicx")}
        print(f"  {gg} k={k}: {row}")
for f in sorted(glob.glob(f"{V}/p1/*.jsonl")):
    for r in load(f):
        print("  P=1", os.path.basename(f), f"avg {r['dyn_avg_us']:.2f} p50 {r['dyn_p50_us']} init {r['init_s']:.1f} core us/upd {r['core_s'] / max(1, r['updates']) * 1e6:.2f} trunc {r.get('truncated')}")
