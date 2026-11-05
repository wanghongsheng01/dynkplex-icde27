#!/bin/bash
# Large-graph experiments (com-Orkut, com-Friendster) on a Linux server: >= 64 GB RAM for Orkut,
# >= 128 GB for Friendster. Run from a clone of this repository:
#   bash scripts/run_large.sh [orkut|friendster|all]      (default: orkut)
# Output: results/large/*.jsonl, *.log and sysinfo.txt.
set -e
cd "$(dirname "$0")/.."
WHAT=${1:-orkut}
R=$PWD/results/large; mkdir -p $R data bin
{ date; uname -a; nproc; free -g; df -h .; g++ --version | head -1; } > $R/sysinfo.txt 2>&1
g++ -O3 -march=native -std=c++17 -o bin/kplex code/kplex.cpp
[ -d external ] || bash scripts/fetch_solvers.sh external
get() { [ -f data/$1.txt ] || (cd data && curl -fsSLO https://snap.stanford.edu/data/bigdata/communities/$1.txt.gz && gunzip $1.txt.gz); }
case $WHAT in orkut) graphs="com-orkut.ungraph";; friendster) graphs="com-friendster.ungraph";; all) graphs="com-orkut.ungraph com-friendster.ungraph";; esac
export KPLEX_EXT_BIN=$PWD/external KPLEX_EXT_DIR=$PWD/tmp_ext KPLEX_NOWARM=1 KPLEX_RUN_CAP_S=${KPLEX_RUN_CAP_S:-3600}
eng() { [ $1 = 1 ] && echo mcbrb || echo kpex; }
cd bin
for g in $graphs; do
  (cd .. && get $g)
  # static solvers
  for e in mcbrb pmc; do KPLEX_EXT=$e ./kplex ext ../data/$g.txt 1 >> $R/static.jsonl 2>> $R/static.log || true; done
  for k in 2 3; do for e in kpex kplext; do KPLEX_EXT=$e ./kplex ext ../data/$g.txt $k >> $R/static.jsonl 2>> $R/static.log || true; done; done
  # k = 1, 2, 3 run in parallel (separate temp dirs); each run is capped at KPLEX_RUN_CAP_S seconds
  for k in 1 2 3; do ( export KPLEX_EXT_DIR=$PWD/../tmp_ext/k$k; mkdir -p $KPLEX_EXT_DIR; for sq in "rand 10000 2500" "delins 10000 5000"; do
    set -- $sq
    # CertKP (local searches with our engine, time budget, fallback to the official solver)
    KPLEX_EXT=$(eng $k) KPLEX_REPAIR=1 KPLEX_CACHE=1 KPLEX_CORE=1 ./kplex dyn ../data/$g.txt $k $1 $2 3 $3 >> $R/${1}_full_k$k.jsonl 2>> $R/dyn_k$k.log || true
    # BasicX (every harmful deletion re-solved by the official solver)
    KPLEX_EXT=$(eng $k) KPLEX_EXT_GLOBAL=1 ./kplex dyn ../data/$g.txt $k $1 $2 3 $3 >> $R/${1}_basicx_k$k.jsonl 2>> $R/dyn_k$k.log || true
    # LazyMC (k = 1; whole-graph searches by MC-BRB, warm-up capped at 600 s)
    [ $k = 1 ] && { KPLEX_EXT=mcbrb KPLEX_EXT_GLOBAL=1 KPLEX_NOBUDGET=1 KPLEX_ALGO=lazymc KPLEX_LAZY_MCAP_S=600 ./kplex dyn ../data/$g.txt 1 $1 $2 3 $3 >> $R/${1}_lazymc.jsonl 2>> $R/dyn_k$k.log || true; }
  done ) & done; wait
done
echo "done: results in $R"
