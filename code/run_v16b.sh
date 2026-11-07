#!/bin/bash
# Second part of the review experiments (2026-11-06); starts when run_v16.sh has finished.
#  p1        single-job reruns (one run at a time) of the 34 typical cells whose ratio to BasicX+ lies in
#            0.7-1.3, median-run seed, no checkpoints (R2-E9)
#  fidelity  LazyMC under the protocol of Sun et al. [11]: 20,000 random updates and 50,000 deleted then
#            reinserted edges on Pokec, LiveJournal and WikiTalk; insertion refresh rate = ins_seeded/ins (R2-E13)
#  orkut     com-Orkut (117M edges), rand and del-ins, k = 1, 2, one run at a time (R2-E4)
cd "$(dirname "$0")"
while pgrep -f run_v16.sh > /dev/null; do sleep 60; done
R=../results/v16; S=./ext_tmp; B=./kplex15
flags() { case $1 in
  full) echo "KPLEX_REPAIR=1 KPLEX_CACHE=1 KPLEX_CORE=1 KPLEX_WARM=1";;
  bxc) echo "KPLEX_REPAIR=1 KPLEX_CORE=1 KPLEX_EXT_GLOBAL=1 KPLEX_EXT_CORESUB=1";;
  lazymc) echo "KPLEX_ALGO=lazymc KPLEX_NOBUDGET=1";;
esac; }
eng() { [ $1 = 1 ] && echo mcbrb || echo kpex; }
run() {  # exp variant graph k stream Q seed sample [wfrac] [extra env]
  env $(flags $2) KPLEX_TAG=$2 ${10} KPLEX_NOWARM=1 KPLEX_EXT=$(eng $4) KPLEX_EXT_DIR=$S/v16b $B dyn ../data/$3 $4 $5 $6 $7 $8 $9 >> $R/$1.jsonl 2>> $R/err_$1.log
}
python3 - > $R/p1_cells.txt <<'PY'
import contextlib, io
with contextlib.redirect_stdout(io.StringIO()):
    from paper_facts import grid
for s in ("rand", "delins", "temporal", "tinc"):
    for (g, k), f in sorted(grid[(s, "full")].items()):
        b = grid.get((s, "bxc"), {}).get((g, k))
        if b and g != "soc-LiveJournal1" and 0.7 <= b["dyn_avg_us"] / f["dyn_avg_us"] <= 1.3:
            print(s, g, k, f["seed"], b["seed"])
PY
while read s g k sf sb; do
  case $s in rand) a="10000";; delins) a="10000";; temporal) a="10000";; tinc) a="20000";; esac
  w=""; [ $s = tinc ] && w=0.1
  run p1 full $g.txt $k $s $a $sf 0 "$w"
  run p1 bxc $g.txt $k $s $a $sb 0 "$w"
done < $R/p1_cells.txt
for g in soc-pokec-relationships.txt wiki-Talk.txt soc-LiveJournal1.txt; do
  x=""; [ $g = soc-LiveJournal1.txt ] && x="KPLEX_EXT_GLOBAL=1 KPLEX_LAZY_MCAP_S=600"
  run fidelity lazymc $g 1 rand 20000 3 10000 "" "$x"
  run fidelity lazymc $g 1 delins 50000 3 50000 "" "$x"
  run fidelity full $g 1 rand 20000 3 10000
  run fidelity full $g 1 delins 50000 3 50000
done
for k in 1 2; do for s in rand delins; do for v in full bxc; do
  run orkut $v com-orkut.ungraph.txt.gz $k $s 10000 3 5000 "" "KPLEX_RUN_CAP_S=7200"
done; done; done
echo done
