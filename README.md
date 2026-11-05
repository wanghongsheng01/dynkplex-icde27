# CertKP: Maintaining Maximum k-Plexes Exactly in Large Dynamic Graphs — artifact

This repository contains the implementation of CertKP, the reimplemented LazyMC baseline, all
experiment scripts, the raw result files behind every table and figure of the paper, and the
script that regenerates the tables and figures.

## Contents
- `code/kplex.cpp` — single-file C++17 implementation (CertKP, its ablation variants, LazyMC,
  order-based core maintenance, the interface to external static solvers, test modes).
- `code/run_v10.sh` — the full experiment grid of the paper (three seeds; ablation; LazyMC;
  LiveJournal). `code/make_paper.py` regenerates `paper/tables/*.tex`, `paper/figs/*.pdf` and
  `paper/numbers.tex` from `results/`.
- `scripts/fetch_solvers.sh` — fetches and builds the official static solvers at the commits used
  in the paper (MC-BRB, PMC, kPlexS, kPlexT, kPEX). They are not redistributed here because of their
  licences (kPEX has none; MC-BRB and kPlexT are GPL-3). Shipped binaries are never used.
- `scripts/fetch_data.sh` — downloads the SNAP graphs.
- `results/` — JSON lines, one per run (see the field list below); `results/external/crosscheck.md`
  compares the optimum sizes returned by all static solvers.

## Build and run
```
g++ -O3 -march=native -std=c++17 -o code/kplex code/kplex.cpp     # or clang++
bash scripts/fetch_solvers.sh external
bash scripts/fetch_data.sh
cd code && ./kplex brute 2000 1                                   # exhaustive check, small graphs
KPLEX_EXT=mcbrb KPLEX_REPAIR=1 KPLEX_CACHE=1 KPLEX_CORE=1 KPLEX_WARM=1 \
  ./kplex dyn ../data/email-Enron.txt 1 hot 2000 3 200
```
Modes: `dyn <graph> <k> <rand|delins|temporal|tinc|hot> <Q> <seed> <checkpoint_every> [window_frac]`,
`ext <graph> <k>` (one static solve), `brute <trials> <seed>`, `corebench`, `corebrute`.
Variants are selected by environment variables: `KPLEX_REPAIR`, `KPLEX_CACHE`, `KPLEX_CORE`,
`KPLEX_WARM` (CertKP = all four), `KPLEX_EXT_GLOBAL` (BasicX), `KPLEX_ALGO=lazymc`; the static engine
by `KPLEX_EXT=mcbrb|pmc|kplexs|kplext|kpex`; the time budget by `KPLEX_BUDGET` (default 1). A run stops
after `KPLEX_RUN_CAP_S` seconds (default 1200) and then reports `truncated: 1` with the number of
updates processed; this happens only where the static solver itself becomes slow on the updated graph.

## Large graphs
`scripts/run_large.sh [orkut|friendster|all]` runs the static solvers, CertKP, BasicX and LazyMC on
com-Orkut and com-Friendster on a Linux server (>= 64 GB / >= 128 GB RAM); results go to `results/large/`.

## Result fields (selection)
`dyn_avg_us`, `dyn_p50_us`, `dyn_p99_us`, `dyn_p999_us`, `dyn_max_ms` (update latency);
`st_avg_ms` (static solver, self-reported time) and `st_wall_ms` (wall time incl. graph export);
`mismatch` (checkpoints where the maintained size differs from the static optimum) and `invalid`
(updates whose output is not a k-plex); `del_hit`, `del_global`, `cache_builds`, `cache_sub`,
`cache_sub_inv`, `level_reuse`, `fallbacks` (mechanism counters).
