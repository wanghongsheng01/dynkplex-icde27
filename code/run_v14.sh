#!/bin/bash
# Pre-submission review fixes (2026-11-06), all with kplex12, the build of every other v13 run:
#  E1  LazyMC on every stream (3 seeds; WikiTalk/Pokec as in v10) and LazyMC + BasicX on LiveJournal,
#      so that no reported run comes from an earlier build;
#  E2  ablation variants basic / repair / cache on the adversarial stream with seeds 4 and 5 (seed 3 is in v13).
# Output ../results/v14/; copy into ../results/v13 after checking (see the end of the script).
cd "$(dirname "$0")"
R=../results/v14; mkdir -p $R; S=./ext_tmp; P=${1:-3}
flags() { case $1 in basic) echo "";; repair) echo "KPLEX_REPAIR=1";; cache) echo "KPLEX_REPAIR=1 KPLEX_CACHE=1";; esac; }
eng() { [ $1 = 1 ] && echo mcbrb || echo kpex; }
job() {  # variant graph k stream Q seed sample
  echo "env $(flags $1) KPLEX_NOWARM=1 KPLEX_EXT=$(eng $3) KPLEX_EXT_DIR=$S/v14_$1_$2_$3_$4_$6 ./kplex12 dyn ../data/$2.txt $3 $4 $5 $6 $7 >> $R/$4_$1.jsonl 2>> $R/err_$4_$1.log"
}
lz() {  # graph stream Q seed sample [wfrac]  (same parameters as the v10 LazyMC runs)
  echo "env KPLEX_ALGO=lazymc KPLEX_NOWARM=1 KPLEX_EXT=mcbrb KPLEX_NOBUDGET=1 KPLEX_EXT_DIR=$S/v14_lz_$1_$2_$4 ./kplex12 dyn ../data/$1.txt 1 $2 $3 $4 $5 $6 >> $R/lazymc_$2.jsonl 2>> $R/err_lazymc.log"
}
{
for seed in 3 4 5; do
  for g in email-Enron soc-Epinions1 com-dblp.ungraph com-youtube.ungraph; do lz $g rand 10000 $seed 2500; lz $g delins 10000 $seed 5000; done
  for g in sx-mathoverflow sx-askubuntu; do lz $g temporal 10000 $seed 1000; lz $g tinc 20000 $seed 2000 0.1; done
  for g in email-Enron soc-Epinions1 com-dblp.ungraph; do lz $g hot 2000 $seed 200; done; lz com-youtube.ungraph hot 1000 $seed 200
done
for g in wiki-Talk soc-pokec-relationships; do lz $g rand 10000 3 2500; lz $g delins 10000 3 5000; done
for seed in 4 5; do for k in 1 2 3; do for v in basic repair cache; do
  for g in email-Enron soc-Epinions1 com-dblp.ungraph; do job $v $g $k hot 2000 $seed 200; done
  job $v com-youtube.ungraph $k hot 1000 $seed 200
done; done; done
} | xargs -P $P -S 4096 -I{} bash -c "{}"
# LiveJournal (k = 1), one job at a time, same parameters as v10
export KPLEX_EXT=mcbrb KPLEX_NOWARM=1 KPLEX_EXT_DIR=$S/v14_lj
for sq in "rand 10000 2500" "delins 10000 5000"; do set -- $sq
  KPLEX_ALGO=lazymc KPLEX_EXT_GLOBAL=1 KPLEX_NOBUDGET=1 KPLEX_LAZY_MCAP_S=600 ./kplex12 dyn ../data/soc-LiveJournal1.txt 1 $1 $2 3 $3 >> $R/lj_lazymc.jsonl 2>> $R/err_lj.log
  KPLEX_EXT_GLOBAL=1 ./kplex12 dyn ../data/soc-LiveJournal1.txt 1 $1 $2 3 $3 >> $R/lj_basicx.jsonl 2>> $R/err_lj.log
done
echo done
