#!/bin/bash
# kplex15 re-runs of the 11 sweep runs of run_v16.sh that finished with kplex14 after run_v17.sh had built its
# job list (LiveJournal k = 2, 3 and the 10^6-update stream; Stack Overflow windows). Three at a time, longest
# first. Output results/v17/v16/; merged into results/v16 by merge_v17.py.
cd "$(dirname "$0")"
xargs -P 3 -S 4096 -I{} bash -c "{}" < ../results/v17/jobs_v17b.txt
echo v17b-done
