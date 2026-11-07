#!/bin/bash
# kplex15 re-runs on the server (2026-11-07): the server runs of CertKP that executed a cache sweep
# (Orkut del-ins k = 1, 2 and adversarial k = 1, 2; Friendster adversarial k = 1). Same parameters as
# run_server.sh / run_server2.sh; Orkut four at a time as before, then Friendster alone (memory).
cd "$(dirname "$0")"
g++ -O3 -march=native -std=c++17 -o kplex15 kplex.cpp || exit 1
R=../results/v17; mkdir -p $R; S=/root/certkp/tmp_ext; B=./kplex15
F="KPLEX_REPAIR=1 KPLEX_CACHE=1 KPLEX_CORE=1 KPLEX_WARM=1 KPLEX_TAG=full KPLEX_NOWARM=1"
G=com-orkut.ungraph.txt.gz
{
for k in 1 2; do eng=$([ $k = 1 ] && echo mcbrb || echo kpex)
  echo "env $F KPLEX_RUN_CAP_S=7200 KPLEX_EXT=$eng KPLEX_EXT_DIR=$S/v17_d$k $B dyn ../data/$G $k delins 10000 3 5000 >> $R/orkut.jsonl 2>> $R/err_orkut.log"
  echo "env $F KPLEX_RUN_CAP_S=3600 KPLEX_EXT=$eng KPLEX_EXT_DIR=$S/v17_h$k $B dyn ../data/$G $k hot 2000 3 200 >> $R/orkut_hot.jsonl 2>> $R/err_orkut_hot.log"
done
} | xargs -P 4 -I{} bash -c "{}"
/usr/bin/time -v env $F KPLEX_EXT=mcbrb KPLEX_EXT_DIR=$S/v17_fsh KPLEX_RUN_CAP_S=7200 $B dyn ../data/com-friendster.ungraph.txt.gz 1 hot 2000 3 1000 >> $R/friendster_hot.jsonl 2>> $R/friendster_hot_full.time
echo v17-server-done
