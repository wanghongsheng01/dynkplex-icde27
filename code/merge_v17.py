#!/usr/bin/env python3
"""Replace, in results/v13-v16, every record of a run that executed a cache sweep by its kplex15 re-run
from results/v17 (run_v17.sh). A record is matched on (file, graph, k, stream, seed, tag), in file order.
v13/hot_cache.jsonl seeds 4-5 are copies of the v14 runs and take the v17/v14 re-runs. Replaced records,
and the trace and latency dumps they produced, are moved to results/superseded_kplex14/ with the same
relative paths. Idempotent: a second call finds nothing left to replace. Usage: python3 merge_v17.py"""
import collections
import filecmp
import glob
import json
import os
import shutil

R = "../results"
N = f"{R}/v17"
O = f"{R}/superseded_kplex14"


def key(r):
    return (os.path.basename(r["graph"]), r["k"], r["stream"], r["seed"], r.get("tag"))


def swept(r):
    return r.get("cache_builds", 0) > 0 and r.get("del_global", 0) > 0 and r.get("tag") != "lazymc"


def rows(f):
    return [json.loads(l) for l in open(f) if l.startswith("{")]


new = collections.defaultdict(lambda: collections.defaultdict(list))  # target file -> key -> records
for f in sorted(glob.glob(f"{N}/v1[3-6]/**/*.jsonl", recursive=True)):
    rel = os.path.relpath(f, N)
    for r in rows(f):
        new[rel][key(r)].append(r)
        if rel == "v14/hot_cache.jsonl":
            new["v13/hot_cache.jsonl"][key(r)].append(r)

total = 0
for rel, byk in sorted(new.items()):
    tgt = f"{R}/{rel}"
    out, old, used = [], [], collections.Counter()
    for l in open(tgt):
        if not l.startswith("{"):
            out.append(l)
            continue
        r = json.loads(l)
        k = key(r)
        if r.get("kplex") != 15 and swept(r) and used[k] < len(byk.get(k, [])):
            nr = dict(byk[k][used[k]], kplex=15)
            used[k] += 1
            out.append(json.dumps(nr) + "\n")
            old.append(l)
        else:
            out.append(l)
    left = sum(len(v) for v in byk.values()) - sum(used.values())
    stale = sum(1 for l in out if l.startswith("{") and swept(json.loads(l)) and json.loads(l).get("kplex") != 15)
    print(f"{rel}: replaced {len(old)}, unused new {left}, swept old left {stale}")
    total += len(old)
    if old:
        os.makedirs(os.path.dirname(f"{O}/{rel}"), exist_ok=True)
        open(f"{O}/{rel}", "a").writelines(old)
        open(tgt, "w").writelines(out)
print("total replaced", total)

# per-update traces and latency dumps written by the re-runs
moved = 0
for d in ("traces", "lat"):
    for f in sorted(glob.glob(f"{N}/v16/{d}/*")):
        tgt = f"{R}/v16/{d}/{os.path.basename(f)}"
        if os.path.exists(tgt) and filecmp.cmp(f, tgt, shallow=False):
            continue
        if os.path.exists(tgt):
            os.makedirs(f"{O}/v16/{d}", exist_ok=True)
            shutil.move(tgt, f"{O}/v16/{d}/{os.path.basename(f)}")
        shutil.copy2(f, tgt)
        moved += 1
print("dumps copied", moved)
