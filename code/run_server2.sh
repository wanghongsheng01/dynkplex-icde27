#!/bin/bash
# Adversarial stream on com-Friendster (1.8B edges), k = 1, CertKP then BasicX+, one run at a time
# (peak memory about 60 GB), on the 28-core / 110 GB server (2026-11-07). Same build as run_server.sh.
cd "$(dirname "$0")"
R=../results; S=/root/certkp/tmp_ext; B=./kplex14; F=com-friendster.ungraph.txt.gz
while pgrep -f run_server.sh > /dev/null; do sleep 30; done
flags() { case $1 in
  full) echo "KPLEX_REPAIR=1 KPLEX_CACHE=1 KPLEX_CORE=1 KPLEX_WARM=1";;
  bxc) echo "KPLEX_REPAIR=1 KPLEX_CORE=1 KPLEX_EXT_GLOBAL=1 KPLEX_EXT_CORESUB=1";;
esac; }
for v in full bxc; do
  /usr/bin/time -v env $(flags $v) KPLEX_TAG=$v KPLEX_NOWARM=1 KPLEX_EXT=mcbrb KPLEX_EXT_DIR=$S/fsh KPLEX_RUN_CAP_S=7200 $B dyn ../data/$F 1 hot 2000 3 1000 >> $R/friendster_hot.jsonl 2>> $R/friendster_hot_$v.time
done
echo friendster-hot-done
