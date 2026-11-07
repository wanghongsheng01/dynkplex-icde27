#!/bin/bash
# Large graphs on the 28-core / 110 GB server (2026-11-06): com-Orkut, then com-Friendster when its
# download has finished. kplex14 built from code/kplex.cpp; solvers built from the commits of
# scripts/fetch_solvers.sh. Output /root/certkp/results/<graph>_*.jsonl. Ratios are only formed
# between runs on this server.
cd "$(dirname "$0")"
R=../results; mkdir -p $R; S=/root/certkp/tmp_ext; B=./kplex14
flags() { case $1 in
  full) echo "KPLEX_REPAIR=1 KPLEX_CACHE=1 KPLEX_CORE=1 KPLEX_WARM=1";;
  bxc) echo "KPLEX_REPAIR=1 KPLEX_CORE=1 KPLEX_EXT_GLOBAL=1 KPLEX_EXT_CORESUB=1";;
  basicx) echo "KPLEX_EXT_GLOBAL=1";;
  lazymc) echo "KPLEX_ALGO=lazymc KPLEX_NOBUDGET=1 KPLEX_EXT_GLOBAL=1 KPLEX_LAZY_MCAP_S=600";;
esac; }
eng() { [ $1 = 1 ] && echo mcbrb || echo kpex; }
n=0
job() {  # file variant graph k stream Q seed sample [extra env]
  n=$((n+1))
  echo "env $(flags $2) KPLEX_TAG=$2 $9 KPLEX_NOWARM=1 KPLEX_EXT=$(eng $4) KPLEX_EXT_DIR=$S/j$n $B dyn ../data/$3 $4 $5 $6 $7 $8 >> $R/$1.jsonl 2>> $R/err_$1.log"
}
G=com-orkut.ungraph.txt.gz
for e in "1 mcbrb" "1 pmc" "2 kpex" "2 kplext" "3 kpex" "3 kplext"; do set -- $e
  KPLEX_EXT=$2 KPLEX_EXT_DIR=$S/static timeout 7200 $B ext ../data/$G $1 >> $R/orkut_static.jsonl 2>> $R/err_orkut_static.log; done
{
for k in 1 2 3; do for s in rand delins; do for v in full bxc basicx; do job orkut $v $G $k $s 10000 3 5000 "KPLEX_RUN_CAP_S=7200"; done; done; done
for s in rand delins; do job orkut lazymc $G 1 $s 10000 3 5000 "KPLEX_RUN_CAP_S=7200"; done
for k in 1 2; do for v in full bxc; do job orkut_hot $v $G $k hot 2000 3 200 "KPLEX_RUN_CAP_S=3600"; done; done
job orkut_hot lazymc $G 1 hot 2000 3 200 "KPLEX_RUN_CAP_S=3600"
} | xargs -P 4 -I{} bash -c "{}"
# peak memory of CertKP (k = 1, rand), one run alone
/usr/bin/time -v env $(flags full) KPLEX_TAG=full KPLEX_NOWARM=1 KPLEX_EXT=mcbrb KPLEX_EXT_DIR=$S/mem $B dyn ../data/$G 1 rand 2000 3 1000 >> $R/orkut_mem.jsonl 2>> $R/orkut_mem.time
echo orkut-done
# Friendster, one run at a time (memory), once the download is complete
F=com-friendster.ungraph.txt.gz
while [ ! -f ../data/$F ]; do sleep 120; done
KPLEX_EXT=mcbrb KPLEX_EXT_DIR=$S/fs timeout 14400 /usr/bin/time -v $B ext ../data/$F 1 >> $R/friendster_static.jsonl 2>> $R/friendster_static.log
for s in rand delins; do for v in full bxc; do
  /usr/bin/time -v env $(flags $v) KPLEX_TAG=$v KPLEX_NOWARM=1 KPLEX_EXT=mcbrb KPLEX_EXT_DIR=$S/fs KPLEX_RUN_CAP_S=14400 $B dyn ../data/$F 1 $s 10000 3 10000 >> $R/friendster.jsonl 2>> $R/friendster_${s}_${v}.time
done; done
echo friendster-done
