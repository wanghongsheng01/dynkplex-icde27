#!/bin/bash
# Final grid after review round 2 (kplex12): the time budget is c x the wall time of the last external
# solver call, and a fallback solves only the (t-k)-core. Rerun every variant that uses the budget:
#   full (CertKP), bxc (BasicX+: exact cores + repair + official solver on the (omega-k)-core for every
#   remaining harmful deletion), basic / repair / cache (ablation, adversarial stream, seed 3).
# BasicX (whole-graph re-solve) and LazyMC (no budget) keep their v10 runs (copied in at the end).
cd "$(dirname "$0")"
R=../results/v13; mkdir -p $R; S=./ext_tmp; P=${1:-3}
flags() { case $1 in full) echo "KPLEX_REPAIR=1 KPLEX_CACHE=1 KPLEX_CORE=1 KPLEX_WARM=1";; bxc) echo "KPLEX_REPAIR=1 KPLEX_CORE=1 KPLEX_EXT_GLOBAL=1 KPLEX_EXT_CORESUB=1";;
  basic) echo "";; repair) echo "KPLEX_REPAIR=1";; cache) echo "KPLEX_REPAIR=1 KPLEX_CACHE=1";; esac; }
eng() { [ $1 = 1 ] && echo mcbrb || echo kpex; }
job() {  # variant graph k stream Q seed sample [wfrac]
  echo "env $(flags $1) KPLEX_NOWARM=1 KPLEX_EXT=$(eng $3) KPLEX_EXT_DIR=$S/v13_$1_$2_$3_$4_$6 ./kplex12 dyn ../data/$2.txt $3 $4 $5 $6 $7 $8 >> $R/$4_$1.jsonl 2>> $R/err_$4_$1.log"
}
{
for seed in 3 4 5; do for k in 1 2 3; do for v in full bxc; do
  for g in email-Enron soc-Epinions1 com-dblp.ungraph; do job $v $g $k hot 2000 $seed 200; done; job $v com-youtube.ungraph $k hot 1000 $seed 200
done; done; done
for seed in 3 4 5; do for k in 1 2 3; do for v in full bxc; do
  for g in sx-mathoverflow sx-askubuntu; do job $v $g $k temporal 10000 $seed 1000; job $v $g $k tinc 20000 $seed 2000 0.1; done
  for g in email-Enron soc-Epinions1 com-dblp.ungraph com-youtube.ungraph; do job $v $g $k rand 10000 $seed 1000; job $v $g $k delins 10000 $seed 2000; done
done; done; done
for k in 1 2 3; do
  for v in basic repair cache; do for g in email-Enron soc-Epinions1 com-dblp.ungraph; do job $v $g $k hot 2000 3 200; done; job $v com-youtube.ungraph $k hot 1000 3 200; done
  for g in wiki-Talk soc-pokec-relationships; do for v in full bxc; do job $v $g $k rand 10000 3 2500; job $v $g $k delins 10000 3 5000; done; done
done
for seed in 3 4 5; do
  for g in email-Enron soc-Epinions1 com-dblp.ungraph; do echo "env KPLEX_ALGO=lazymc KPLEX_EXT_GLOBAL=1 KPLEX_NOBUDGET=1 KPLEX_NOWARM=1 KPLEX_EXT=mcbrb KPLEX_EXT_DIR=$S/v13_lzx_${g}_$seed ./kplex12 dyn ../data/$g.txt 1 hot 2000 $seed 200 >> $R/lazymcx_hot.jsonl 2>> $R/err_lazymcx.log"; done
  echo "env KPLEX_ALGO=lazymc KPLEX_EXT_GLOBAL=1 KPLEX_NOBUDGET=1 KPLEX_NOWARM=1 KPLEX_EXT=mcbrb KPLEX_EXT_DIR=$S/v13_lzx_yt_$seed ./kplex12 dyn ../data/com-youtube.ungraph.txt 1 hot 1000 $seed 200 >> $R/lazymcx_hot.jsonl 2>> $R/err_lazymcx.log"
done
} | xargs -P $P -S 4096 -I{} bash -c "{}"
# LiveJournal k = 1, one job at a time (CertKP without warm-up, as before; BasicX+)
export KPLEX_EXT=mcbrb KPLEX_NOWARM=1 KPLEX_EXT_DIR=$S/v13_lj
for sq in "rand 10000 2500" "delins 10000 5000"; do set -- $sq
  KPLEX_REPAIR=1 KPLEX_CACHE=1 KPLEX_CORE=1 ./kplex12 dyn ../data/soc-LiveJournal1.txt 1 $1 $2 3 $3 >> $R/lj_full.jsonl 2>> $R/err_lj.log
  KPLEX_REPAIR=1 KPLEX_CORE=1 KPLEX_EXT_GLOBAL=1 KPLEX_EXT_CORESUB=1 ./kplex12 dyn ../data/soc-LiveJournal1.txt 1 $1 $2 3 $3 >> $R/lj_bxc.jsonl 2>> $R/err_lj.log
done
# Single-job (P = 1) measurements: Pokec rand k = 1 (noise check), LiveJournal with the warm-up (init cost)
Q=$R/p1
mkdir -p $Q
KPLEX_REPAIR=1 KPLEX_CACHE=1 KPLEX_CORE=1 KPLEX_WARM=1 ./kplex12 dyn ../data/soc-pokec-relationships.txt 1 rand 10000 3 2500 >> $Q/pokec_full.jsonl 2>> $Q/err.log
KPLEX_EXT_GLOBAL=1 ./kplex12 dyn ../data/soc-pokec-relationships.txt 1 rand 10000 3 2500 >> $Q/pokec_basicx.jsonl 2>> $Q/err.log
KPLEX_REPAIR=1 KPLEX_CORE=1 KPLEX_EXT_GLOBAL=1 KPLEX_EXT_CORESUB=1 ./kplex12 dyn ../data/soc-pokec-relationships.txt 1 rand 10000 3 2500 >> $Q/pokec_bxc.jsonl 2>> $Q/err.log
KPLEX_ALGO=lazymc KPLEX_NOBUDGET=1 ./kplex12 dyn ../data/soc-pokec-relationships.txt 1 rand 10000 3 2500 >> $Q/pokec_lazymc.jsonl 2>> $Q/err.log
KPLEX_REPAIR=1 KPLEX_CACHE=1 KPLEX_CORE=1 KPLEX_WARM=1 KPLEX_RUN_CAP_S=3600 ./kplex12 dyn ../data/soc-LiveJournal1.txt 1 rand 10000 3 2500 >> $Q/lj_full_warm.jsonl 2>> $Q/err.log
# static cross-checks for Pokec and the kPlexS k = 1 record
X=../results/external; export KPLEX_EXT_DIR=$S/v13_static
for e in pmc mcbrb; do KPLEX_EXT=$e ./kplex12 ext ../data/soc-pokec-relationships.txt 1 >> $X/pokec_static.jsonl 2>> $X/pokec_static.err; done
for k in 2 3; do for e in kpex kplext; do KPLEX_EXT=$e ./kplex12 ext ../data/soc-pokec-relationships.txt $k >> $X/pokec_static.jsonl 2>> $X/pokec_static.err; done; done
KPLEX_EXT=kplexs ./kplex12 ext ../data/soc-Epinions1.txt 1 >> $X/kplexs_k1.jsonl 2>> $X/kplexs_k1.err
# carry over the runs that do not use the budget
for f in ../results/v10/*_basicx.jsonl ../results/v10/lazymc_*.jsonl ../results/v10/lj_basicx.jsonl ../results/v10/lj_lazymc.jsonl; do cp $f $R/; done
echo done
