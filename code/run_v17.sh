#!/bin/bash
# Re-run with kplex15 (2026-11-07) every earlier run that executed a cache sweep (CertKP, +Cache; rows with
# cache_builds > 0 and del_global > 0 in results/v13-v16). kplex15 adds one deadline check between the
# subproblems of a sweep, so that the time budget bounds the whole sweep; runs without a sweep execute
# the same code as before and are not re-run. Output: results/v17/<v13..v16>/<same file names>.
# Jobs (results/v17/jobs_parallel.txt, generated from run_v13-v16.sh) run three at a time: one worker
# while run_v16.sh still runs two jobs, three after it ends. Then the single-job runs, then run_v16b.sh.
cd "$(dirname "$0")"
J=../results/v17/jobs_parallel.txt; C=../results/v17/.next; L=../results/v17/.lock
[ -f $C ] || echo 1 > $C
worker() {
  while true; do
    until mkdir $L 2>/dev/null; do sleep 0.2; done
    i=$(cat $C); echo $((i+1)) > $C; rmdir $L
    line=$(sed -n "${i}p" $J); [ -z "$line" ] && break
    bash -c "$line"
  done
}
worker & 
while pgrep -f run_v16.sh > /dev/null; do sleep 60; done
worker & worker &
wait
# single-job runs (one at a time, as originally): LiveJournal k = 1 without and with the warm-up, memory
R=../results/v17; S=./ext_tmp
export KPLEX_EXT=mcbrb KPLEX_NOWARM=1 KPLEX_EXT_DIR=$S/v17_lj
for sq in "rand 10000 2500" "delins 10000 5000"; do set -- $sq
  KPLEX_REPAIR=1 KPLEX_CACHE=1 KPLEX_CORE=1 ./kplex15 dyn ../data/soc-LiveJournal1.txt 1 $1 $2 3 $3 >> $R/v13/lj_full.jsonl 2>> $R/v13/err_lj.log
done
KPLEX_REPAIR=1 KPLEX_CACHE=1 KPLEX_CORE=1 KPLEX_WARM=1 KPLEX_RUN_CAP_S=3600 ./kplex15 dyn ../data/soc-LiveJournal1.txt 1 rand 10000 3 2500 >> $R/v13/p1/lj_full_warm.jsonl 2>> $R/v13/p1/err.log
/usr/bin/time -l env KPLEX_REPAIR=1 KPLEX_CACHE=1 KPLEX_CORE=1 KPLEX_WARM=1 ./kplex15 dyn ../data/soc-LiveJournal1.txt 1 rand 2000 3 1000 >> $R/v15/mem_full.jsonl 2>> $R/v15/mem_full_soc-LiveJournal1.time
echo v17-done
bash run_v16b.sh > ../results/v16b_run.log 2>&1
