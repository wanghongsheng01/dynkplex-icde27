#!/usr/bin/env python3
"""Print the quantities quoted from the second round of review-requested runs (../results/v16, run_v16.sh
and run_v16b.sh). Usage: python3 v16_facts.py [section ...]  (from 14_graph/code)"""
import collections
import glob
import json
import os
import statistics as S
import sys

W = "../results/v16"


def load(f):
    return [json.loads(l) for l in open(f) if l.startswith("{")] if os.path.exists(f) else []


def g(r):
    return os.path.basename(r["graph"]).replace(".txt", "").replace(".gz", "")


def med_run(rows):
    rows = sorted(rows, key=lambda r: r["dyn_avg_us"])
    return rows[len(rows) // 2]


def by(rows, *keys):
    d = collections.defaultdict(list)
    for r in rows:
        d[tuple(r[k] if k != "graph" else g(r) for k in keys)].append(r)
    return d


def check(rows, name):
    bad = [(g(r), r["k"], r.get("tag")) for r in rows if r["mismatch"] or r["invalid"]]
    tr = [(g(r)[:10], r["k"], r.get("tag")) for r in rows if r.get("truncated")]
    print(f"  [{name}] runs {len(rows)} mismatch/invalid {bad} truncated {tr}")


def rng(xs, f="%.2f"):
    xs = sorted(xs)
    return (f + "-" + f) % (xs[0], xs[-1]) if xs else "-"


want = set(sys.argv[1:])


def sec(name):
    on = not want or name in want
    if on:
        print(f"\n== {name}")
    return on


if sec("exact"):
    groups = collections.defaultdict(dict)
    for f in glob.glob(f"{W}/traces/*"):
        gg, s, k, v = os.path.basename(f).rsplit("_", 3)
        groups[(gg, s, k)][v] = open(f).read().split()
    cmp_ = [(key, v) for key, d in groups.items() for v in d if v != "full"]
    bad = [(key, v) for key, v in cmp_ if groups[key][v] != groups[key]["full"]]
    rows = load(f"{W}/exact.jsonl")
    print(f"  trace groups {len(groups)}, comparisons with CertKP {len(cmp_)}, disagreements {len(bad)}, "
          f"updates per method {sum(len(d['full']) for d in groups.values())}, invalid {sum(r['invalid'] for r in rows)}")
    rows = load(f"{W}/everyst.jsonl")
    check(rows, "every-update ST")
    print(f"  updates checked against the static solver: {sum(r['samples'] for r in rows)}")

if sec("region"):
    R = {m: {(g(r), r["k"], r["stream"]): r for r in load(f"{W}/region_{m}.jsonl")} for m in ("base", "twohop", "core", "nonadj")}
    check(sum((list(d.values()) for d in R.values()), []), "region")
    rat = collections.defaultdict(list)
    for key, b in sorted(R["base"].items()):
        line = f"  {key[0][:10]} {key[2]:8s} k={key[1]}: X mean/p99 base {b['x_mean']}/{b['x_p99']} avg {b['dyn_avg_us']:.1f}"
        for m in ("twohop", "core", "nonadj"):
            o = R[m].get(key)
            if o:
                line += f" | {m} X {o['x_mean']}/{o['x_p99']} avg {o['dyn_avg_us']:.1f}"
                if b["x_mean"] > 0:
                    rat[m + "_x"].append(o["x_mean"] / b["x_mean"])
                rat[m + "_t"].append(o["dyn_avg_us"] / b["dyn_avg_us"])
        print(line)
    for m, v in rat.items():
        print(f"  {m}: ratio to two-seed {rng(v)} median {S.median(v):.2f}")

if sec("hotrev"):
    for f in ("0.333", "0.667", "0.889"):
        rows = load(f"{W}/hotrev_{f}.jsonl")
        check(rows, f"hotrev insfrac {f}")
        d = by(rows, "graph", "k", "tag")
        for gg in sorted({x[0] for x in d}):
            for k in (1, 2):
                a, b = d.get((gg, k, "full")), d.get((gg, k, "bxc"))
                if a and b:
                    a, b = a[0], b[0]
                    print(f"  f={f} {gg[:8]} k={k}: ratio BX+/CertKP {b['dyn_avg_us'] / a['dyn_avg_us']:.2f} hits {a['del_hit']} grow {a['ins_grow']} shrink {a['del_shrink']}"
                          f" builds {a['cache_builds']} sub {a['cache_sub']} revoked {a['cache_sub_inv']} fb {a['fallbacks']} hits/level {a['del_hit'] / max(1, a['del_shrink']):.1f}")

if sec("takedown"):
    rows = load(f"{W}/takedown.jsonl")
    check(rows, "takedown")
    d = by(rows, "graph", "k", "tag")
    for (gg, k, t), rr in sorted(d.items()):
        if t == "full":
            b = d.get((gg, k, "bxc"), [None])[0]
            l = d.get((gg, k, "lazymc"), [None])[0]
            r = rr[0]
            print(f"  {gg[:8]} k={k}: CertKP {r['dyn_avg_us']:.1f} hits {r['del_hit']} omega {r['omega0']}->{r['omega_end']} vsST {r['speedup_vs_st']:.0f}"
                  + (f" | BX+ ratio {b['dyn_avg_us'] / r['dyn_avg_us']:.2f}" if b else "") + (f" | LazyMC ratio {l['dyn_avg_us'] / r['dyn_avg_us']:.2f}" if l else ""))

if sec("budget2"):
    rows = load(f"{W}/budget2.jsonl")
    check(rows, "budget2")
    for r in sorted(rows, key=lambda r: (g(r), r["k"], r["stream"], r["tag"])):
        print(f"  {g(r)[:10]} {r['stream']:8s} k={r['k']} {r['tag']:6s}: avg {r['dyn_avg_us']:.1f} max {r['dyn_max_ms']:.0f} ms fb {r['fallbacks']} trunc {r['truncated']} upd {r['updates']}")

if sec("densest"):
    v13 = {(g(r), r["k"]): r for r in load("../results/v13/hot_full.jsonl") if r["seed"] == 3}
    for r in load(f"{W}/densest.jsonl"):
        o = v13.get((g(r), r["k"]))
        print(f"  {g(r)[:10]} k={r['k']}: x ST with {r['samples']} checkpoints {r['speedup_vs_st']:.1f}" + (f" vs {o['samples']} checkpoints {o['speedup_vs_st']:.1f}" if o else "") + f" mismatch {r['mismatch']}")

if sec("approx"):
    for r in load(f"{W}/approx.jsonl"):
        print(f"  {g(r)[:10]} {r['stream']:6s} k={r['k']}: avg {r['dyn_avg_us']:.1f} suboptimal {r['subopt']}/{r['samples']} checkpoints, gap max {r['gap_max']} mean {r['gap_sum'] / max(1, r['subopt']):.1f}")

if sec("thresh"):
    for d in ("1", "3"):
        rows = load(f"{W}/thresh_{d}.jsonl")
        print(f"  [d={d}] alert errors {sum(r['alert_bad'] for r in rows)} over {sum(r['samples'] for r in rows)} checkpoints")
        dd = by(rows, "graph", "k", "tag")
        for (gg, k, t), rr in sorted(dd.items()):
            if t == "full" and (gg, k, "bxc") in dd:
                a, b = rr[0], dd[(gg, k, "bxc")][0]
                print(f"  d={d} {gg[:8]} k={k}: CertKP {a['dyn_avg_us']:.1f} BX+ {b['dyn_avg_us']:.1f} ratio {b['dyn_avg_us'] / a['dyn_avg_us']:.2f} hits {a['del_hit']}")

if sec("k5"):
    rows = load(f"{W}/k5.jsonl")
    check(rows, "k5")
    for s in ("hot", "rand"):
        d = by([r for r in rows if r["stream"] == s], "graph", "tag")
        for (gg, t), rr in sorted(d.items()):
            if t == "full" and (gg, "bxc") in d:
                a, b = med_run(rr), med_run(d[(gg, "bxc")])
                pair = sorted(x["dyn_avg_us"] / y["dyn_avg_us"] for x in d[(gg, "bxc")] for y in rr if x["seed"] == y["seed"])
                print(f"  {s} {gg[:8]}: CertKP {a['dyn_avg_us']:.1f} BX+ {b['dyn_avg_us']:.1f} ratio {b['dyn_avg_us'] / a['dyn_avg_us']:.2f} paired {rng(pair)} vsST {a['speedup_vs_st']:.1f} p50 {a['dyn_p50_us']}")

if sec("seeds"):
    old = collections.defaultdict(list)
    for v in ("full", "bxc"):
        for s in ("hot", "rand", "delins"):
            for r in load(f"../results/v13/{s}_{v}.jsonl"):
                old[(s, v, g(r), r["k"])].append(r)
    for r in load("../results/v13/lazymc_hot.jsonl") + load("../results/v13/lazymc_rand.jsonl") + load("../results/v13/lazymc_delins.jsonl"):
        old[(r["stream"], "lazymc", g(r), 1)].append(r)
    new = collections.defaultdict(list)
    for s in ("hot", "rand", "delins"):
        rows = load(f"{W}/seeds_{s}.jsonl")
        check(rows, f"seeds {s}")
        for r in rows:
            new[(s, r["tag"], g(r), r["k"])].append(r)
    allr = {key: old.get(key, []) + new[key] for key in new}
    print("  seeds per cell:", sorted({len(v) for v in allr.values()}))
    for s in ("hot", "rand", "delins"):
        for k in (1, 2, 3):
            pr, cells = [], 0
            for (ss, v, gg, kk), rr in allr.items():
                if ss != s or v != "full" or kk != k or (s, "bxc", gg, k) not in allr:
                    continue
                bb = {x["seed"]: x for x in allr[(s, "bxc", gg, k)]}
                p = [bb[x["seed"]]["dyn_avg_us"] / x["dyn_avg_us"] for x in rr if x["seed"] in bb]
                cells += 1
                q = sorted(p)
                print(f"  {s} k={k} {gg[:8]}: seeds {len(p)} BX+/CertKP median {S.median(p):.2f} IQR {q[len(q) // 4]:.2f}-{q[3 * len(q) // 4]:.2f} min-max {rng(p)}"
                      f" hits {rng([x['del_hit'] for x in rr], '%d')}")
            # in-process lower bound for BasicX+: subtract the export/load part of its external calls
    rows = load(f"{W}/seeds_hot.jsonl")
    d = collections.defaultdict(list)
    for r in rows:
        d[(r["tag"], g(r), r["k"], r["seed"])] = r
    for k in (1, 2, 3):
        rr = []
        for (t, gg, kk, seed), b in d.items():
            if t == "bxc" and kk == k and ("full", gg, k, seed) in d:
                a = d[("full", gg, k, seed)]
                inproc = (b["dyn_total_s"] - (b["ext_wall_s"] - b["ext_own_s"])) / b["updates"] * 1e6
                rr.append(inproc / a["dyn_avg_us"])
        if rr:
            print(f"  in-process BasicX+ bound (solver-reported time only), adversarial k={k}: BX+/CertKP {rng(rr)} median {S.median(rr):.2f}")
    for s in ("hot", "rand", "delins"):
        cells = [(gg, k) for (ss, v, gg, k) in allr if ss == s and v == "full"]
        if s != "hot":
            cnt = sum(1 for (gg, k) in cells if (s, "bxc", gg, k) in allr and S.median(
                [x["dyn_avg_us"] for x in allr[(s, "bxc", gg, k)]]) / S.median([x["dyn_avg_us"] for x in allr[(s, "full", gg, k)]]) >= 0.95)
            print(f"  {s}: cells on par (>=0.95x) or faster with 10 seeds: {cnt} of {len(cells)}")

if sec("long"):
    for r in load(f"{W}/long_hot.jsonl") + load(f"{W}/long_rand.jsonl"):
        w = r.get("win_us", [])
        print(f"  {g(r)[:10]} {r['stream']} k={r['k']} {r['tag']:12s}: upd {r['updates']} avg {r['dyn_avg_us']:.2f} mism {r['mismatch']} hits {r['del_hit']} shrink {r['del_shrink']} builds {r['cache_builds']}"
              f" | window avg first/last {w[0] if w else '-'}/{w[-1] if w else '-'} min-max {rng(w) if w else '-'} | fwd {r.get('win_fwd', [])[:3]}..{r.get('win_fwd', [])[-3:]} trunc {r['truncated']}")

if sec("twin"):
    for f in sorted(glob.glob(f"{W}/twin_*.jsonl")):
        Wsec = int(f.split("_")[-1][:-6])
        rows = load(f)
        check(rows, os.path.basename(f))
        d = by(rows, "graph", "k", "tag")
        for (gg, k, t), rr in sorted(d.items()):
            if t != "full":
                continue
            a = rr[0]
            b = d.get((gg, k, "bxc"), [None])[0]
            l = d.get((gg, 1, "lazymc"), [None])[0] if k == 1 else None
            per10k = a["del_hit"] / max(1, a["updates"]) * 1e4
            print(f"  W={Wsec // 86400}d {gg[:12]} k={k}: upd {a['updates']} hits {a['del_hit']} ({per10k:.0f}/10^4) omega {a['omega0']} avg {a['dyn_avg_us']:.2f} vsST {a['speedup_vs_st']:.0f}"
                  + (f" | BX+ {b['dyn_avg_us'] / a['dyn_avg_us']:.2f}x" if b else "") + (f" | LazyMC {l['dyn_avg_us'] / a['dyn_avg_us']:.2f}x" if l else ""))

if sec("queue"):
    # FIFO queue replay of the 1-week window streams at their timestamps, sped up by s
    for f in sorted(glob.glob(f"{W}/lat/*_604800_*")):
        ts, lat = [], []
        for line in open(f):
            a, b = line.split()
            ts.append(int(a)); lat.append(float(b) * 1e-6)
        if not ts:
            continue
        out = []
        for sp in (1, 1e3, 1e5, 1e6):
            free, delays, backlog, mx = 0.0, [], [], 0
            t0 = ts[0]
            for t, l in zip(ts, lat):
                arr = (t - t0) / sp
                start = max(arr, free)
                delays.append(start - arr)
                free = start + l
            delays.sort()
            out.append(f"x{sp:.0e}: p99 wait {delays[int(0.99 * (len(delays) - 1))] * 1e3:.3f} ms max {delays[-1] * 1e3:.1f} ms")
        span = (ts[-1] - ts[0]) / 86400
        print(f"  {os.path.basename(f)} ({len(ts)} updates over {span:.1f} days): " + "; ".join(out))

for name in ("dblpfull", "lj", "p1", "fidelity", "orkut"):
    if sec(name):
        rows = load(f"{W}/{name}.jsonl")
        check(rows, name)
        for r in rows:
            t10 = r.get("tail10", [])
            print(f"  {g(r)[:10]} {r['stream']:6s} k={r['k']} {r['tag']:7s} seed {r['seed']}: upd {r['updates']} avg {r['dyn_avg_us']:.2f} p50 {r['dyn_p50_us']} max {r['dyn_max_ms']:.0f}ms hits {r['del_hit']}"
                  f" glob {r['del_global']} fb {r['fallbacks']} vsST {r['speedup_vs_st']:.1f} ins {r['ins']} insrefresh {r['ins_seeded']} trunc {r['truncated']} run_s {r['run_s']}"
                  + (f" | slowest [tot, search, fb, ext, budget] {t10[0]}" if t10 else ""))
