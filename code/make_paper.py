#!/usr/bin/env python3
"""Generate the paper's tables, figures and number macros from the result files.
Usage: python3 make_paper.py   (run from 14_graph/code; writes into ../paper)"""
import collections
import glob
import json
import math
import os
import statistics

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

R = "../results"
P = "../paper"


def ratio(x):
    return f"{x:.1f}" if x >= 10 else f"{x:.2f}"

NAME = {"email-Enron": "Enron", "ca-AstroPh": "AstroPh", "soc-Epinions1": "Epinions", "sx-mathoverflow": "MathOverflow",
        "sx-askubuntu": "AskUbuntu", "com-dblp.ungraph": "DBLP", "com-youtube.ungraph": "YouTube", "wiki-Talk": "WikiTalk",
        "soc-pokec-relationships": "Pokec", "soc-LiveJournal1": "LiveJournal"}
ORDER = list(NAME)


def gname(path):
    return os.path.basename(path).replace(".txt", "")


def load(pattern):
    rows = []
    for f in sorted(glob.glob(pattern)):
        for line in open(f):
            line = line.strip()
            if line.startswith("{"):
                rows.append(json.loads(line))
    return rows


def fmt_us(x):
    if x < 10:
        return f"{x:.1f}"
    if x < 1000:
        return f"{x:.0f}"
    return f"{x/1000:.1f}k" if x < 1e5 else f"{x/1000:.0f}k"


def fmt_ms(x):
    if x < 1:
        return f"{x:.2f}"
    if x < 100:
        return f"{x:.1f}"
    return f"{x:,.0f}"


def fmt_x(x):
    if x >= 1e6:
        return f"{x/1e6:.1f}M"
    if x >= 1e4:
        return f"{x/1e3:.0f}k"
    if x >= 100:
        return f"{x:,.0f}"
    return f"{x:.1f}"


V = os.environ.get("KPLEX_RESULTS", f"{R}/v13")


def median_run(rows):
    rows = sorted(rows, key=lambda r: r["dyn_avg_us"])
    return dict(rows[len(rows) // 2], reps=len(rows), lo=rows[0]["dyn_avg_us"], hi=rows[-1]["dyn_avg_us"])


cells = collections.defaultdict(list)  # (stream, variant, graph, k) -> runs
for f in glob.glob(f"{V}/*_*.jsonl"):
    stream, variant = os.path.basename(f)[:-6].split("_", 1)
    if stream in ("lj", "lazymc"):
        continue
    if stream == "lazymcx":  # LazyMC with MC-BRB whole-graph refreshes
        for r in load(f):
            cells[(r["stream"], "lazymcx", gname(r["graph"]), 1)].append(r)
        continue
    for r in load(f):
        cells[(stream, variant, gname(r["graph"]), r["k"])].append(r)
for f in glob.glob(f"{V}/lazymc_*.jsonl"):
    for r in load(f):
        cells[(r["stream"], "lazymc", gname(r["graph"]), 1)].append(r)
for v in ("lazymc", "full", "basicx", "bxc"):
    for r in load(f"{V}/lj_{v}.jsonl"):
        cells[(r["stream"], v, gname(r["graph"]), 1)].append(r)
grid = collections.defaultdict(dict)  # (stream, variant) -> {(graph,k): median run}
for (s_, v, g, k), rows in cells.items():
    grid[(s_, v)][(g, k)] = median_run(rows)
allrows = [r for rows in cells.values() for r in rows]

# ---------------------------------------------------------------- numbers
runs = len(allrows)
checkpoints = sum(r["samples"] for r in allrows)
mism = sum(r["mismatch"] for r in allrows)
invalid = sum(r["invalid"] for r in allrows)
TYP = ("rand", "delins", "temporal", "tinc")
typ_full = [r for (s_, v), d in grid.items() if v == "full" and s_ in TYP for r in d.values()]
sp = [r["speedup_vs_st"] for r in typ_full if r["speedup_vs_st"] > 0]
p50 = [r["dyn_p50_us"] for r in typ_full]
hot_full = grid.get(("hot", "full"), {})
hot_sp = [r["speedup_vs_st"] for r in hot_full.values() if r["speedup_vs_st"] > 0]
bx = [grid[("hot", "basicx")][c]["dyn_avg_us"] / r["dyn_avg_us"] for c, r in hot_full.items() if c in grid.get(("hot", "basicx"), {})]
bxp = [grid[("hot", "bxc")][c]["dyn_avg_us"] / r["dyn_avg_us"] for c, r in hot_full.items() if c in grid.get(("hot", "bxc"), {}) and not r.get("truncated")]
typ_bxp = [grid[(s_, "bxc")][c]["dyn_avg_us"] / r["dyn_avg_us"] for (s_, v), d in grid.items() if v == "full" and s_ in TYP
           for c, r in d.items() if c in grid.get((s_, "bxc"), {})]
typ_bx = [grid[(s_, "basicx")][c]["dyn_avg_us"] / r["dyn_avg_us"] for (s_, v), d in grid.items() if v == "full" and s_ in TYP
          for c, r in d.items() if c in grid.get((s_, "basicx"), {})]


def med(v):
    return statistics.median(v) if v else float("nan")


lzr = [grid[(s_, "lazymc")][(g, 1)]["dyn_avg_us"] / grid[(s_, "full")][(g, 1)]["dyn_avg_us"]
       for (s_, v), d in grid.items() if v == "lazymc" for (g, k) in d if (g, 1) in grid.get((s_, "full"), {}) and s_ == "hot"]
nums = {
    "NumRuns": f"{runs:,}",
    "NumCheckpoints": f"{checkpoints:,}",
    "NumMismatch": str(mism),
    "NumInvalid": str(invalid),
    "NumTypicalUpdate": f"{max(p50):.1f}\\,$\\mu$s" if p50 else "TBD",
    "NumSpeedupST": f"{fmt_x(min(sp))}--{fmt_x(max(sp))}$\\times$" if sp else "TBD",
    "NumSpeedupSTmed": f"{fmt_x(med(sp))}$\\times$" if sp else "TBD",
    "NumSpeedupHot": f"{fmt_x(min(hot_sp))}--{fmt_x(max(hot_sp))}$\\times$" if hot_sp else "TBD",
    "NumHotVsBasicX": f"{min(bx):.1f}--{max(bx):.0f}$\\times$" if bx else "TBD",
    "NumTypVsBasicXmed": f"{med(typ_bx):.1f}$\\times$" if typ_bx else "TBD",
    "NumHotVsBasicXp": f"{min(bxp):.1f}--{max(bxp):.0f}$\\times$" if bxp else "TBD",
    "NumTypVsBasicXpmed": f"{med(typ_bxp):.1f}$\\times$" if typ_bxp else "TBD",
    "NumHotVsLazyMC": f"{min(lzr):.0f}--{max(lzr):.0f}$\\times$" if lzr else "TBD",
}
keep = {}
if os.path.exists(f"{P}/numbers.tex"):
    for line in open(f"{P}/numbers.tex"):
        if line.startswith("\\newcommand{\\ArtifactURL}") or line.startswith("\\newcommand{\\AIAck}"):
            keep[line.split("}")[0]] = line
with open(f"{P}/numbers.tex", "w") as f:
    f.write("% generated by ../code/make_paper.py -- do not edit by hand\n")
    for k, v in nums.items():
        f.write(f"\\newcommand{{\\{k}}}{{{v}}}\n")
    for line in keep.values():
        f.write(line)
print(nums)

# ---------------------------------------------------------------- Table: datasets
stat = load(f"{R}/external/crosscheck.jsonl")
ds = {}
for r in stat:
    g = gname(r["graph"])
    ds.setdefault(g, {"n": r["n"], "m": r["m"], "om": {}})
    ds[g]["om"][r["k"]] = r["omega"]
for r in allrows:
    if r["stream"] in ("rand", "delins"):  # initial graph = the full graph
        ds.setdefault(gname(r["graph"]), {"n": r["n"], "m": r["m"], "om": {}})["om"].setdefault(r["k"], r["omega0"])
for r in load(f"{R}/external/lj_static.jsonl"):
    ds.setdefault(gname(r["graph"]), {"n": r["n"], "m": r["m"], "om": {}})["om"][r["k"]] = r["omega"]
kind = {"email-Enron": "communication", "ca-AstroPh": "collaboration", "soc-Epinions1": "trust", "sx-mathoverflow": "Q\\&A$^\\dagger$",
        "sx-askubuntu": "Q\\&A$^\\dagger$", "com-dblp.ungraph": "collaboration", "com-youtube.ungraph": "social",
        "wiki-Talk": "communication", "soc-pokec-relationships": "social", "soc-LiveJournal1": "social"}
L = ["\\begin{table}[t]", "\\centering", "\\caption{Datasets (SNAP~\\cite{leskovec2014snap}). $m$ counts undirected edges (for temporal graphs, distinct pairs over all time); $\\omega_k$ is the maximum $k$-plex size of the full graph; $^\\dagger$: temporal.}",
     "\\label{tab:data}", "\\footnotesize", "\\setlength{\\tabcolsep}{3pt}", "\\begin{tabular}{lrrlrrr}", "\\toprule",
     "Graph & $n$ & $m$ & Type & $\\omega_1$ & $\\omega_2$ & $\\omega_3$\\\\", "\\midrule"]
for g in ORDER:
    if g not in ds:
        continue
    d = ds[g]
    om = [str(d["om"].get(k, "--")) for k in (1, 2, 3)]
    L.append(f"{NAME[g]} & {d['n']:,} & {d['m']:,} & {kind[g]} & {' & '.join(om)}\\\\")
L += ["\\bottomrule", "\\end{tabular}", "\\end{table}"]
open(f"{P}/tables/datasets.tex", "w").write("\n".join(L) + "\n")

# ---------------------------------------------------------------- Table: static engines
T = collections.defaultdict(dict)
for r in stat:
    T[(gname(r["graph"]), r["k"])][r.get("engine", "ours")] = r.get("wall_s", r.get("time"))
T[("wiki-Talk", 3)]["kplexs"] = None
for r in load(f"{R}/external/lj_static.jsonl") + load(f"{R}/external/pokec_static.jsonl"):
    T[(gname(r["graph"]), r["k"])][r["engine"]] = r["wall_s"]
L = ["\\begin{table}[t]", "\\centering",
     "\\caption{Static exact solvers: wall time in seconds (single thread). All solvers return the same $\\omega_k$ on every instance; ``--'' = not applicable, $>$2h = stopped after two hours. ST in the rest of the paper is MC-BRB ($k=1$) and kPEX ($k\\ge2$).}",
     "\\label{tab:static}", "\\small", "\\setlength{\\tabcolsep}{3pt}", "\\begin{tabular}{lrrrrrr}", "\\toprule",
     "Graph & MC-BRB & PMC & \\multicolumn{3}{c}{kPlexS / kPlexT / kPEX} & \\\\",
     " & $k{=}1$ & $k{=}1$ & $k{=}2$ & & $k{=}3$ & \\\\", "\\midrule"]
L = ["\\begin{table}[t]", "\\centering",
     "\\caption{Static exact solvers: wall time (s), single thread. All solvers that finished agree on $\\omega_k$ for every instance; on LiveJournal only ST was run. kPlexS is not exact for $k=1$ and is omitted there; $>$2h: stopped after two hours. ST denotes MC-BRB ($k=1$) and kPEX ($k\\ge2$).}",
     "\\label{tab:static}", "\\footnotesize", "\\setlength{\\tabcolsep}{2.2pt}", "\\begin{tabular}{l rr rrr rrr}", "\\toprule",
     " & \\multicolumn{2}{c}{$k=1$} & \\multicolumn{3}{c}{$k=2$} & \\multicolumn{3}{c}{$k=3$}\\\\",
     "\\cmidrule(lr){2-3}\\cmidrule(lr){4-6}\\cmidrule(lr){7-9}",
     "Graph & BRB & PMC & kPlexS & kPlexT & kPEX & kPlexS & kPlexT & kPEX\\\\", "\\midrule"]


def ft(x):
    if x is None:
        return "$>$2h"
    return f"{x:.3f}" if x < 1 else (f"{x:.2f}" if x < 10 else f"{x:,.0f}")


for g in ORDER:
    if (g, 1) not in T and (g, 2) not in T:
        continue
    c = []
    for k, engs in ((1, ("mcbrb", "pmc")), (2, ("kplexs", "kplext", "kpex")), (3, ("kplexs", "kplext", "kpex"))):
        for e in engs:
            c.append(ft(T[(g, k)][e]) if e in T[(g, k)] else "--")
    L.append(f"{NAME[g]} & " + " & ".join(c) + "\\\\")
L += ["\\bottomrule", "\\end{tabular}", "\\end{table}"]
open(f"{P}/tables/static.tex", "w").write("\n".join(L) + "\n")

# ---------------------------------------------------------------- Table: main results
SN = {"rand": "rand", "delins": "del-ins", "temporal": "window", "tinc": "t-insert", "hot": "advers."}
L = ["\\begin{table*}[t]", "\\centering",
     "\\caption{Main results (median of three seeds; WikiTalk and Pokec one seed). Avg: average update time of CertKP ($\\mu$s); Max: slowest update (ms); ST: time of the best static solver at the checkpoints, as reported by the solver (ms); $\\times$ST: ST/Avg; $\\times$BX$^+$: average update time of BasicX$^+$ (filters, exact core numbers, repair, and the official solver on the $(\\omega-k)$-core for every remaining harmful deletion) divided by Avg. $^\\dagger$: run stopped by the 20-minute cap; Avg, ST and the ratios cover the updates processed. All runs matched the static optimum at every checkpoint.}",
     "\\label{tab:main}", "\\footnotesize", "\\setlength{\\tabcolsep}{2.4pt}",
     "\\begin{tabular}{ll" + "rrrrr" * 3 + "}", "\\toprule",
     " & & \\multicolumn{5}{c}{$k=1$} & \\multicolumn{5}{c}{$k=2$} & \\multicolumn{5}{c}{$k=3$}\\\\",
     "\\cmidrule(lr){3-7}\\cmidrule(lr){8-12}\\cmidrule(lr){13-17}",
     "Graph & Stream & Avg & Max & ST & $\\times$ST & $\\times$BX$^+$ & Avg & Max & ST & $\\times$ST & $\\times$BX$^+$ & Avg & Max & ST & $\\times$ST & $\\times$BX$^+$\\\\", "\\midrule"]
for s_ in ("temporal", "tinc", "rand", "delins", "hot"):
    full, bxg = grid.get((s_, "full"), {}), grid.get((s_, "bxc"), {})
    gs = [g for g in ORDER if g != "soc-LiveJournal1" and any((g, k) in full for k in (1, 2, 3))]
    for g in gs:
        c = []
        for k in (1, 2, 3):
            r, b = full.get((g, k)), bxg.get((g, k))
            if r is None:
                c += ["--"] * 5
                continue
            c += [fmt_us(r["dyn_avg_us"]) + ("$^\\dagger$" if r.get("truncated") else ""), fmt_ms(r["dyn_max_ms"]), fmt_ms(r["st_avg_ms"]), fmt_x(r["speedup_vs_st"]),
                  f"{b['dyn_avg_us'] / r['dyn_avg_us']:.1f}" if b else "--"]
        L.append(f"{NAME[g]} & {SN[s_]} & " + " & ".join(c) + "\\\\")
    if gs:
        L.append("\\midrule")
if L[-1] == "\\midrule":
    L.pop()
L += ["\\bottomrule", "\\end{tabular}", "\\end{table*}"]
open(f"{P}/tables/main.tex", "w").write("\n".join(L) + "\n")

# ---------------------------------------------------------------- Table: LazyMC (k = 1)
L = ["\\begin{table}[t]", "\\centering",
     "\\caption{CertKP vs.\\ LazyMC~\\cite{sun2022lazymc} ($k=1$, same search engine and core maintenance; median of three seeds, LiveJournal one). Avg update time ($\\mu$s); Hits: harmful deletions (CertKP); Glob: whole-graph searches of LazyMC / cache searches of CertKP; LazyMC$^{\\mathrm{X}}$: LazyMC with MC-BRB for its whole-graph searches (adversarial stream; on LiveJournal both columns use MC-BRB). CertKP on WikiTalk and Pokec: one seed.}",
     "\\label{tab:lazymc}", "\\footnotesize", "\\setlength{\\tabcolsep}{2pt}", "\\begin{tabular}{llrrrrrr}", "\\toprule",
     "Graph & Stream & LazyMC & LazyMC$^{\\mathrm{X}}$ & CertKP & Ratio & Hits & Glob\\\\", "\\midrule"]
for s_ in ("temporal", "tinc", "rand", "delins", "hot"):
    lzg, full = grid.get((s_, "lazymc"), {}), grid.get((s_, "full"), {})
    for g in ORDER:
        if (g, 1) not in lzg or (g, 1) not in full:
            continue
        a, b = lzg[(g, 1)], full[(g, 1)]
        x = grid.get((s_, "lazymcx"), {}).get((g, 1))
        L.append(f"{NAME[g]} & {SN[s_]} & {fmt_us(a['dyn_avg_us'])} & {fmt_us(x['dyn_avg_us']) if x else '--'} & {fmt_us(b['dyn_avg_us'])} & {ratio(a['dyn_avg_us']/b['dyn_avg_us'])}$\\times$ & {b['del_hit']} & {a['del_global']}/{b['del_global']}\\\\")
L += ["\\bottomrule", "\\end{tabular}", "\\end{table}"]
open(f"{P}/tables/lazymc.tex", "w").write("\n".join(L) + "\n")

# ---------------------------------------------------------------- figures
plt.rcParams.update({"font.size": 8, "font.family": "serif", "axes.linewidth": 0.6, "pdf.fonttype": 42})
C = {"ST": "#7f7f7f", "BasicX": "#bdbdbd", "BasicX+": "#969696", "LazyMC": "#d95f02", "Basic": "#d9d9d9", "+Repair": "#9ecae1", "+Cache": "#4292c6", "CertKP": "#08519c"}

# Fig 1: adversarial stream, k = 1
gs = [g for g in ("email-Enron", "soc-Epinions1", "com-youtube.ungraph") if (g, 1) in grid.get(("hot", "lazymc"), {}) and (g, 1) in hot_full]
if gs:
    fig, ax = plt.subplots(figsize=(3.4, 2.0))
    series = (("ST (MC-BRB)", [hot_full[(g, 1)]["st_avg_ms"] * 1000 for g in gs], C["ST"]),
              ("BasicX", [grid[("hot", "basicx")].get((g, 1), {}).get("dyn_avg_us", float("nan")) for g in gs], C["BasicX"]),
              ("BasicX+", [grid[("hot", "bxc")].get((g, 1), {}).get("dyn_avg_us", float("nan")) for g in gs], C["BasicX+"]),
              ("LazyMC", [grid[("hot", "lazymc")][(g, 1)]["dyn_avg_us"] for g in gs], C["LazyMC"]),
              ("CertKP", [hot_full[(g, 1)]["dyn_avg_us"] for g in gs], C["CertKP"]))
    w = 0.16
    for i, (lab, vals, col) in enumerate(series):
        bars = ax.bar([j + (i - 2) * w for j in range(len(gs))], vals, w, label=lab, color=col)
        for bx_, v in zip(bars, vals):
            if not math.isnan(v):
                ax.text(bx_.get_x() + bx_.get_width() / 2, v * 1.15, f"{v/1000:.0f}" if v >= 1000 else f"{v/1000:.1f}", ha="center", va="bottom", fontsize=5.5)
    ax.set_yscale("log")
    ax.set_ylim(50, 1e6)
    ax.set_xticks(range(len(gs)))
    ax.set_xticklabels([NAME[g] for g in gs])
    ax.set_ylabel("avg update time ($\\mu$s)")
    # legend above the axes, never over bars or labels
    ax.legend(frameon=False, ncol=3, loc="lower center", bbox_to_anchor=(0.5, 1.0), fontsize=6.5, handlelength=1.2, columnspacing=1.0)
    for sp_ in ("top", "right"):
        ax.spines[sp_].set_visible(False)
    fig.tight_layout(pad=0.2)
    fig.savefig(f"{P}/figs/fig1_motivation.pdf")
    plt.close(fig)

# Fig: ablation on the adversarial stream (seed 3)
abl = {}
for v in ("basic", "repair", "cache", "full", "bxc"):
    for (s_, vv, g, k), rows in cells.items():
        if s_ == "hot" and vv == v:
            rr = [r for r in rows if r["seed"] == 3]
            if rr:
                abl[(v, g, k)] = rr[0]
cl = [(g, k) for g in ("email-Enron", "soc-Epinions1", "com-youtube.ungraph") for k in (1, 2, 3)
      if all((v, g, k) in abl for v in ("basic", "repair", "cache", "full"))]
if cl:
    fig, ax = plt.subplots(figsize=(7.0, 2.0))
    w = 0.16
    for i, (v, lab) in enumerate((("bxc", "BasicX+"), ("basic", "Basic"), ("repair", "+Repair"), ("cache", "+Cache"), ("full", "CertKP"))):
        ax.bar([j + (i - 2) * w for j in range(len(cl))], [abl[(v, g, k)]["speedup_vs_st"] if (v, g, k) in abl else float("nan") for g, k in cl], w, label=lab, color=C[lab],
               edgecolor="#666" if v == "bxc" else None, linewidth=0.4)
    ax.set_yscale("log")
    ax.axhline(1, color="k", lw=0.5, ls="--")
    ax.set_xticks(range(len(cl)))
    ax.set_xticklabels([f"{NAME[g]}\nk={k}" for g, k in cl], fontsize=7)
    ax.set_ylabel("speedup over ST")
    ax.legend(frameon=False, ncol=5, loc="lower center", bbox_to_anchor=(0.5, 1.0), fontsize=7)
    for sp_ in ("top", "right"):
        ax.spines[sp_].set_visible(False)
    fig.tight_layout(pad=0.2)
    fig.savefig(f"{P}/figs/fig_ablation.pdf")
    plt.close(fig)

# Table: core maintenance
cb = load(f"{R}/lazymc_order/corebench.jsonl")
CB = collections.defaultdict(dict)
for r in cb:
    CB[(gname(r["graph"]), r["stream"])][r["core_alg"]] = r
L = ["\\begin{table}[t]", "\\centering", "\\caption{Core maintenance alone: average time per update ($\\mu$s), order-based (ours, after~\\cite{zhang2017ordercore,guo2024ordercore}) vs.\\ traversal-based~\\cite{sariyuce2013streaming}.}",
     "\\label{tab:core}", "\\footnotesize", "\\begin{tabular}{llrrr}", "\\toprule", "Graph & Stream & Order & Traversal & Ratio\\\\", "\\midrule"]
for (g, s), d in sorted(CB.items(), key=lambda x: (ORDER.index(x[0][0]), x[0][1])):
    if "order" in d and "trav" in d:
        a, b = d["order"]["avg_us"], d["trav"]["avg_us"]
        L.append(f"{NAME[g]} & {SN.get(s, s)} & {a:.1f} & {b:,.0f} & {b/a:,.0f}$\\times$\\\\")
L += ["\\bottomrule", "\\end{tabular}", "\\end{table}"]
open(f"{P}/tables/core.tex", "w").write("\n".join(L) + "\n")
print("runs", runs, "checkpoints", checkpoints, "mismatch", mism, "invalid", invalid)
