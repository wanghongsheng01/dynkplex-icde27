#!/bin/bash
# Re-run the three DBLP adversarial k>=2 cells (seed 3) that were stopped before the run cap existed.
# Waits for run_v10.sh to finish so that timings are not taken under extra load.
cd "$(dirname "$0")"
while pgrep -f run_v10.sh >/dev/null; do sleep 60; done
R=../results/v10; S=./ext_tmp
env KPLEX_REPAIR=1 KPLEX_CACHE=1 KPLEX_CORE=1 KPLEX_WARM=1 KPLEX_NOWARM=1 KPLEX_EXT=kpex KPLEX_EXT_DIR=$S/redo_full_2 ./kplex10 dyn ../data/com-dblp.ungraph.txt 2 hot 2000 3 200 >> $R/hot_full.jsonl 2>> $R/err_hot_full.log &
env KPLEX_EXT_GLOBAL=1 KPLEX_NOWARM=1 KPLEX_EXT=kpex KPLEX_EXT_DIR=$S/redo_basicx_2 ./kplex10 dyn ../data/com-dblp.ungraph.txt 2 hot 2000 3 200 >> $R/hot_basicx.jsonl 2>> $R/err_hot_basicx.log &
env KPLEX_REPAIR=1 KPLEX_CACHE=1 KPLEX_CORE=1 KPLEX_WARM=1 KPLEX_NOWARM=1 KPLEX_EXT=kpex KPLEX_EXT_DIR=$S/redo_full_3 ./kplex10 dyn ../data/com-dblp.ungraph.txt 3 hot 2000 3 200 >> $R/hot_full.jsonl 2>> $R/err_hot_full.log &
wait; echo redo done
