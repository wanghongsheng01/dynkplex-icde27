#!/bin/bash
# Experiments requested by the simulated round-3 review (2026-11-06). Starts after run_v14.sh finishes so
# that at most three runs execute at a time, as in every other batch. Output ../results/v15/.
#  R1-E4  LazyMC+ : LazyMC whose global refresh calls MC-BRB on the (|S*|)-core only (kplex13; the only
#         change from kplex12 is this KPLEX_EXT_CORESUB path inside LazyMC)
#  R1-E3  budget constant c in {0.25, 0.5, 2, 4} (c = 1 is in v13)
#  R1-E7  k = 4 on the adversarial stream (3 seeds) and rand (seed 3)
#  R2/R3  real windows that expire dense groups: window streams over 5% and 20% of the history
#  R2/R3  adversarial stream on the larger graphs WikiTalk and Pokec
#  R3     peak memory (one run at a time)
cd "$(dirname "$0")"
while pgrep -f run_v14.sh > /dev/null; do sleep 60; done
R=../results/v15; mkdir -p $R; S=./ext_tmp; P=${1:-3}
flags() { case $1 in full) echo "KPLEX_REPAIR=1 KPLEX_CACHE=1 KPLEX_CORE=1 KPLEX_WARM=1";; bxc) echo "KPLEX_REPAIR=1 KPLEX_CORE=1 KPLEX_EXT_GLOBAL=1 KPLEX_EXT_CORESUB=1";; esac; }
eng() { [ $1 = 1 ] && echo mcbrb || echo kpex; }
job() {  # file-tag variant graph k stream Q seed sample [wfrac]
  echo "env $(flags $2) KPLEX_NOWARM=1 KPLEX_EXT=$(eng $4) KPLEX_EXT_DIR=$S/v15_$1_$2_$3_$4_$7 ./kplex12 dyn ../data/$3.txt $4 $5 $6 $7 $8 $9 >> $R/$1_$2.jsonl 2>> $R/err_$1.log"
}
lz() {  # file-tag graph stream Q seed sample [wfrac] [extra env]
  echo "env KPLEX_ALGO=lazymc KPLEX_NOWARM=1 KPLEX_EXT=mcbrb KPLEX_NOBUDGET=1 $8 KPLEX_EXT_DIR=$S/v15_lz_$1_$2_$5 ./kplex13 dyn ../data/$2.txt 1 $3 $4 $5 $6 $7 >> $R/$1.jsonl 2>> $R/err_$1.log"
}
{
# LazyMC+ (adversarial, 3 seeds)
for seed in 3 4 5; do
  for g in email-Enron soc-Epinions1 com-dblp.ungraph; do lz lazymcplus_hot $g hot 2000 $seed 200 "" "KPLEX_EXT_GLOBAL=1 KPLEX_EXT_CORESUB=1 KPLEX_CORE=1"; done
  lz lazymcplus_hot com-youtube.ungraph hot 1000 $seed 200 "" "KPLEX_EXT_GLOBAL=1 KPLEX_EXT_CORESUB=1 KPLEX_CORE=1"
done
# budget constant
for c in 0.25 0.5 2 4; do for k in 2 3; do
  for g in email-Enron soc-Epinions1; do echo "env KPLEX_BUDGET=$c $(flags full) KPLEX_NOWARM=1 KPLEX_EXT=kpex KPLEX_EXT_DIR=$S/v15_c${c}_${g}_${k} ./kplex12 dyn ../data/$g.txt $k hot 2000 3 200 >> $R/budget_c$c.jsonl 2>> $R/err_budget.log"; done
  echo "env KPLEX_BUDGET=$c $(flags full) KPLEX_NOWARM=1 KPLEX_EXT=kpex KPLEX_EXT_DIR=$S/v15_c${c}_yt_$k ./kplex12 dyn ../data/com-youtube.ungraph.txt $k hot 1000 3 200 >> $R/budget_c$c.jsonl 2>> $R/err_budget.log"
done; done
# k = 4
for v in full bxc; do
  for seed in 3 4 5; do for g in email-Enron soc-Epinions1; do job hotk4 $v $g 4 hot 2000 $seed 200; done; job hotk4 $v com-youtube.ungraph 4 hot 1000 $seed 200; done
  for g in email-Enron soc-Epinions1 com-youtube.ungraph; do job randk4 $v $g 4 rand 10000 3 1000; done
done
# short real windows (deterministic streams: one run each)
for w in 0.05 0.2; do for g in sx-mathoverflow sx-askubuntu; do
  for k in 1 2 3; do for v in full bxc; do job win$w $v $g $k temporal 20000 3 2000 $w; done; done
  lz lazymc_win$w $g temporal 20000 3 2000 $w ""
done; done
# adversarial stream on the larger graphs
for g in wiki-Talk soc-pokec-relationships; do
  for k in 1 2; do for v in full bxc; do job hotbig $v $g $k hot 2000 3 200; done; done
  lz lazymc_hotbig $g hot 2000 3 200 "" ""
done
} | xargs -P $P -S 4096 -I{} bash -c "{}"
# peak memory, one run at a time (macOS /usr/bin/time -l reports the maximum resident set size)
for g in soc-pokec-relationships soc-LiveJournal1; do
  /usr/bin/time -l env $(flags full) KPLEX_NOWARM=1 KPLEX_EXT=mcbrb KPLEX_EXT_DIR=$S/v15_mem ./kplex12 dyn ../data/$g.txt 1 rand 2000 3 1000 >> $R/mem_full.jsonl 2>> $R/mem_full_$g.time
done
echo done
