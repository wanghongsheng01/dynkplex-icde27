#!/bin/bash
# Final experiments (kplex10). ST = MC-BRB (k=1) / kPEX (k>=2), solver-reported time (wall time also
# logged). Every variant runs with the same per-update time budget (1 x the last static solve) and
# falls back to one static solve when a local search exceeds it.
#   full   = CertKP: repair + certificates (lazy invalidation, level reuse) + exact cores + warm-up
#   basicx = filter + seeded insertions; every harmful deletion re-solved by the official solver
#   basic  = filter + seeded insertions; harmful deletions by a global search with our engine
#   repair, cache = ablation steps (adversarial stream only)
# Output ../results/v10/<stream>_<variant>.jsonl ; LazyMC pairs in ../results/v10/lazymc_*.jsonl
cd "$(dirname "$0")"
R=../results/v10; mkdir -p $R
S=./ext_tmp; P=${1:-3}
flags() { case $1 in full) echo "KPLEX_REPAIR=1 KPLEX_CACHE=1 KPLEX_CORE=1 KPLEX_WARM=1";; basic) echo "";; basicx) echo "KPLEX_EXT_GLOBAL=1";;
  repair) echo "KPLEX_REPAIR=1";; cache) echo "KPLEX_REPAIR=1 KPLEX_CACHE=1";; esac; }
eng() { [ $1 = 1 ] && echo mcbrb || echo kpex; }
job() {  # variant graph k stream Q seed sample [wfrac]
  echo "env $(flags $1) KPLEX_NOWARM=1 KPLEX_EXT=$(eng $3) KPLEX_EXT_DIR=$S/v10_$1_$2_$3_$4_$6 ./kplex10 dyn ../data/$2.txt $3 $4 $5 $6 $7 $8 >> $R/$4_$1.jsonl 2>> $R/err_$4_$1.log"
}
lz() {  # graph stream Q seed sample [wfrac]  (k = 1, no external engine needed except ST at checkpoints)
  echo "env KPLEX_ALGO=lazymc KPLEX_NOWARM=1 KPLEX_EXT=mcbrb KPLEX_NOBUDGET=1 KPLEX_EXT_DIR=$S/v10_lz_$1_$2_$4 ./kplex10 dyn ../data/$1.txt 1 $2 $3 $4 $5 $6 >> $R/lazymc_$2.jsonl 2>> $R/err_lazymc.log"
}
{
for seed in 3 4 5; do
  for k in 1 2 3; do
    for g in sx-mathoverflow sx-askubuntu; do for v in full basicx; do job $v $g $k temporal 10000 $seed 1000; job $v $g $k tinc 20000 $seed 2000 0.1; done; done
    for g in email-Enron soc-Epinions1 com-dblp.ungraph com-youtube.ungraph; do for v in full basicx; do job $v $g $k rand 10000 $seed 1000; job $v $g $k delins 10000 $seed 2000; done; done
    for g in email-Enron soc-Epinions1 com-dblp.ungraph; do for v in full basicx; do job $v $g $k hot 2000 $seed 200; done; done
    for v in full basicx; do job $v com-youtube.ungraph $k hot 1000 $seed 200; done
  done
  for g in email-Enron soc-Epinions1 com-dblp.ungraph com-youtube.ungraph wiki-Talk soc-pokec-relationships; do lz $g rand 10000 $seed 2500; lz $g delins 10000 $seed 5000; done
  for g in sx-mathoverflow sx-askubuntu; do lz $g temporal 10000 $seed 1000; lz $g tinc 20000 $seed 2000 0.1; done
  for g in email-Enron soc-Epinions1 com-dblp.ungraph; do lz $g hot 2000 $seed 200; done; lz com-youtube.ungraph hot 1000 $seed 200
done
# single seed: ablation steps and Basic, large graphs
for k in 1 2 3; do
  for g in email-Enron soc-Epinions1 com-dblp.ungraph; do for v in basic repair cache; do job $v $g $k hot 2000 3 200; done; done
  for v in basic repair cache; do job $v com-youtube.ungraph $k hot 1000 3 200; done
  for g in sx-mathoverflow sx-askubuntu; do job basic $g $k temporal 10000 3 1000; job basic $g $k tinc 20000 3 2000 0.1; done
  for g in email-Enron soc-Epinions1 com-dblp.ungraph com-youtube.ungraph; do job basic $g $k rand 10000 3 1000; job basic $g $k delins 10000 3 2000; done
  for g in wiki-Talk soc-pokec-relationships; do for v in full basicx; do job $v $g $k rand 10000 3 2500; job $v $g $k delins 10000 3 5000; done; done
done
} | xargs -P $P -S 4096 -I{} bash -c "{}"
# LiveJournal (k = 1), one job at a time
export KPLEX_EXT=mcbrb KPLEX_NOWARM=1 KPLEX_EXT_DIR=$S/v10_lj
for sq in "rand 10000 2500" "delins 10000 5000"; do
  set -- $sq
  KPLEX_ALGO=lazymc KPLEX_EXT_GLOBAL=1 KPLEX_NOBUDGET=1 KPLEX_LAZY_MCAP_S=600 ./kplex10 dyn ../data/soc-LiveJournal1.txt 1 $1 $2 3 $3 >> $R/lj_lazymc.jsonl 2>> $R/err_lj.log
  KPLEX_REPAIR=1 KPLEX_CACHE=1 KPLEX_CORE=1 ./kplex10 dyn ../data/soc-LiveJournal1.txt 1 $1 $2 3 $3 >> $R/lj_full.jsonl 2>> $R/err_lj.log
  KPLEX_EXT_GLOBAL=1 ./kplex10 dyn ../data/soc-LiveJournal1.txt 1 $1 $2 3 $3 >> $R/lj_basicx.jsonl 2>> $R/err_lj.log
done
for k in 2 3; do KPLEX_EXT=kpex ./kplex10 ext ../data/soc-LiveJournal1.txt $k >> ../results/external/lj_static.jsonl 2>> ../results/external/lj_static.err; done
echo done
