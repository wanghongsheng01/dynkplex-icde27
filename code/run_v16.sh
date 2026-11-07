#!/bin/bash
# Experiments that the round-3 review asked for and the first revision declined (2026-11-06), with
# kplex14 (= kplex.cpp; default behaviour identical to kplex12/13, instrumentation in the header).
# Output ../results/v16/<experiment>.jsonl; every record carries "tag" = variant. Three runs at a time.
#  exact      per-update omega traces of CertKP, BasicX+ and LazyMC on the non-adaptive streams (R1-E6)
#  everyst    a static solve after every update on the smaller graphs (R1-E6)
#  region     insertion region: two-seed vs two-hop vs (t-k)-core, plus the k = 2 non-adjacency filter (R1-E5, R2-E12)
#  hotrev     revocation pressure / level oscillation: re-inserted optimum edges, insertion:hit 1, 4, 16 (R1-E1, R1-E2)
#  takedown   non-adaptive replay that deletes the densest region first (R2-E2)
#  budget2    c = 0.1 and no budget; MathOverflow window; DBLP (R1-E3, R2-E11)
#  densest    a static solve every 20 updates on the adversarial stream (R1-E8, R2-E7)
#  approx     local repair without global search, size gap to the optimum (R3-E11)
#  thresh     alert "a k-plex of size >= omega0 - d exists" (R3-E10)
#  k5         k = 5 (R1-E7)
#  seeds      seeds 6-12 for every cell with hits (R2-E6); also ext_wall/ext_own for the in-process bound (R2-E8)
#  long       2x10^4 adversarial updates, pi rebuild every 2,000 updates, 10^6-update streams, cache drop (R2-E5, R1-E9, R3-E6)
#  twin       time windows of a day / week / month on real temporal graphs, latency dumps (R2-E1, R3-E1, R3-E2, R3-E5)
#  dblpfull   DBLP adversarial k = 2, 3 with a 3 h cap instead of 20 min, tail decomposition (R3-E12, R2-E10)
#  lj         LiveJournal: adversarial k = 1, 2 and typical k = 2, 3 (R2-E3, R3-E3)
cd "$(dirname "$0")"
R=../results/v16; mkdir -p $R $R/traces $R/lat; S=./ext_tmp; P=${1:-3}; B=./kplex14
flags() { case $1 in
  full) echo "KPLEX_REPAIR=1 KPLEX_CACHE=1 KPLEX_CORE=1 KPLEX_WARM=1";;
  bxc) echo "KPLEX_REPAIR=1 KPLEX_CORE=1 KPLEX_EXT_GLOBAL=1 KPLEX_EXT_CORESUB=1";;
  basicx) echo "KPLEX_EXT_GLOBAL=1";;
  approx) echo "KPLEX_REPAIR=1 KPLEX_CORE=1 KPLEX_APPROX=1";;
  lazymc) echo "KPLEX_ALGO=lazymc KPLEX_NOBUDGET=1";;
esac; }
eng() { [ $1 = 1 ] && echo mcbrb || echo kpex; }
n=0
# job exp variant graph k stream Q seed sample [wfrac] [extra env]
job() {
  n=$((n+1))
  echo "env $(flags $2) KPLEX_TAG=$2 ${10} KPLEX_NOWARM=1 KPLEX_EXT=$(eng $4) KPLEX_EXT_DIR=$S/v16_$n $B dyn ../data/$3 $4 $5 $6 $7 $8 $9 >> $R/$1.jsonl 2>> $R/err_$1.log"
}
G4="email-Enron.txt soc-Epinions1.txt com-dblp.ungraph.txt com-youtube.ungraph.txt"
G3="email-Enron.txt soc-Epinions1.txt com-youtube.ungraph.txt"
hq() { [ $1 = com-youtube.ungraph.txt ] && echo 1000 || echo 2000; }  # adversarial stream length
TR=../results/v16/traces
{
# exact: identical omega after every update (sample 0: no checkpoints)
for g in $G4; do for s in rand delins; do for k in 1 2 3; do for v in full bxc; do
  job exact $v $g $k $s 10000 3 0 "" "KPLEX_TRACE=$TR/${g%.txt}_${s}_${k}_$v"; done; done
  job exact lazymc $g 1 $s 10000 3 0 "" "KPLEX_TRACE=$TR/${g%.txt}_${s}_1_lazymc"; done; done
for g in sx-mathoverflow.txt sx-askubuntu.txt; do for s in temporal tinc; do
  Q=10000; [ $s = tinc ] && Q=20000; w=""; [ $s = tinc ] && w=0.1
  for k in 1 2 3; do for v in full bxc; do job exact $v $g $k $s $Q 3 0 "$w" "KPLEX_TRACE=$TR/${g%.txt}_${s}_${k}_$v"; done; done
  job exact lazymc $g 1 $s $Q 3 0 "$w" "KPLEX_TRACE=$TR/${g%.txt}_${s}_1_lazymc"; done; done
# everyst: static solve after every update (no run cap)
for k in 1 2 3; do job everyst full email-Enron.txt $k rand 10000 3 1 "" "KPLEX_RUN_CAP_S=0"; job everyst full sx-askubuntu.txt $k temporal 10000 3 1 "" "KPLEX_RUN_CAP_S=0"; done
job everyst full soc-Epinions1.txt 1 rand 10000 3 1 "" "KPLEX_RUN_CAP_S=0"; job everyst full sx-mathoverflow.txt 1 tinc 20000 3 1 0.1 "KPLEX_RUN_CAP_S=0"
# region ablation (CertKP, k = 2, 3)
for k in 2 3; do for r in base twohop core nonadj; do
  [ $r = nonadj ] && [ $k = 3 ] && continue
  e=""; [ $r = twohop ] && e="KPLEX_REGION=twohop"; [ $r = core ] && e="KPLEX_REGION=core"; [ $r = nonadj ] && e="KPLEX_NONADJ=1"
  for g in sx-mathoverflow.txt sx-askubuntu.txt; do job region_$r full $g $k tinc 20000 3 2000 0.1 "$e"; job region_$r full $g $k temporal 10000 3 1000 "" "$e"; done
  for g in $G3; do job region_$r full $g $k delins 10000 3 5000 "" "$e"; done
done; done
# hotrev at insertion:hit = 1, 4, 16
for f in 0.333 0.667 0.889; do for g in $G3; do for k in 1 2; do for v in full bxc; do
  job hotrev_$f $v $g $k hotrev $(hq $g) 3 200 "" "KPLEX_HOT_INSFRAC=$f"; done; done; done; done
# takedown (non-adaptive)
for g in $G4; do for k in 1 2 3; do for v in full bxc; do job takedown $v $g $k takedown 10000 3 1000 "" "KPLEX_HOT_INSFRAC=0.25"; done; done
  job takedown lazymc $g 1 takedown 10000 3 1000 "" "KPLEX_HOT_INSFRAC=0.25"; done
# budget: c = 0.1 and no budget on the adversarial stream; MathOverflow window
for g in $G3; do for k in 2 3; do
  job budget2 full $g $k hot $(hq $g) 3 200 "" "KPLEX_BUDGET=0.1 KPLEX_TAG=c0.1"
  job budget2 full $g $k hot $(hq $g) 3 200 "" "KPLEX_NOBUDGET=1 KPLEX_TAG=cinf"; done; done
for k in 2 3; do for c in 0.25 0.5 1 2 4; do job budget2 full sx-mathoverflow.txt $k temporal 10000 3 1000 "" "KPLEX_BUDGET=$c KPLEX_TAG=c$c"; done; done
# denser checkpoints on the adversarial stream
for g in $G3; do for k in 1 2 3; do job densest full $g $k hot $(hq $g) 3 20; done; done
job densest full com-dblp.ungraph.txt 1 hot 2000 3 20
# approximate maintenance (no global search)
for g in $G3; do for k in 1 2 3; do job approx approx $g $k hot $(hq $g) 3 20; done; done
for g in $G4; do for k in 1 2 3; do job approx approx $g $k rand 10000 3 500; done; done
# threshold alerts
for d in 1 3; do for g in $G3; do for k in 1 2; do for v in full bxc; do job thresh_$d $v $g $k hot $(hq $g) 3 50 "" "KPLEX_THRESH_REL=$d"; done; done; done; done
# k = 5
for v in full bxc; do for seed in 3 4 5; do for g in $G3; do job k5 $v $g 5 hot $(hq $g) $seed 200; done; done
  for g in $G3; do job k5 $v $g 5 rand 10000 3 1000; done; done
# seeds 6-12
for seed in 6 7 8 9 10 11 12; do
  for g in $G3; do for k in 1 2 3; do for v in full bxc; do job seeds_hot $v $g $k hot $(hq $g) $seed 200; done; done; job seeds_hot lazymc $g 1 hot $(hq $g) $seed 200; done
  for g in $G4; do for s in rand delins; do Q=$([ $s = rand ] && echo 2500 || echo 5000)
    for k in 1 2 3; do for v in full bxc; do job seeds_$s $v $g $k $s 10000 $seed $Q; done; done; job seeds_$s lazymc $g 1 $s 10000 $seed $Q; done; done
done
# long streams on the smaller graphs; pi rebuilt every 2,000 updates
for g in email-Enron.txt soc-Epinions1.txt; do for k in 1 2; do
  for v in full bxc; do job long_hot $v $g $k hot 20000 3 2000 "" "KPLEX_WINSTATS=2000 KPLEX_RUN_CAP_S=7200"; done
  job long_hot full $g $k hot 20000 3 2000 "" "KPLEX_WINSTATS=2000 KPLEX_RUN_CAP_S=7200 KPLEX_PI_REBUILD=2000 KPLEX_TAG=full_pi2000"; done; done
job long_rand full soc-pokec-relationships.txt 1 rand 1000000 3 100000 "" "KPLEX_WINSTATS=100000 KPLEX_RUN_CAP_S=7200"
job long_rand bxc soc-pokec-relationships.txt 1 rand 1000000 3 100000 "" "KPLEX_WINSTATS=100000 KPLEX_RUN_CAP_S=7200"
job long_rand full soc-pokec-relationships.txt 1 rand 2200000 3 220000 "" "KPLEX_WINSTATS=220000 KPLEX_RUN_CAP_S=7200 KPLEX_TAG=full_drop"
# time windows on the smaller temporal graphs (bitcoin: trust ratings between traders)
for g in sx-mathoverflow.txt sx-askubuntu.txt wiki-talk-temporal.txt.gz; do for W in 86400 604800 2592000; do
  for k in 1 2 3; do for v in full bxc; do job twin_$W $v $g $k temporal 20000 3 2000 0.8 "KPLEX_WIN_SECONDS=$W KPLEX_LATDUMP=../results/v16/lat/${g%%.*}_${W}_${k}_$v"; done; done
  job twin_$W lazymc $g 1 temporal 20000 3 2000 0.8 "KPLEX_WIN_SECONDS=$W KPLEX_LATDUMP=../results/v16/lat/${g%%.*}_${W}_1_lazymc"; done; done
for g in soc-sign-bitcoinotc.txt soc-sign-bitcoinalpha.txt; do for W in 2592000 7776000 31536000; do
  for k in 1 2 3; do for v in full bxc; do job twin_$W $v $g $k temporal 20000 3 2000 0.3 "KPLEX_WIN_SECONDS=$W"; done; done
  job twin_$W lazymc $g 1 temporal 20000 3 2000 0.3 "KPLEX_WIN_SECONDS=$W"; done; done
# budget on DBLP; DBLP adversarial with a 3 h cap (tail decomposition in tail10)
for k in 2 3; do for c in 0.25 0.5 2 4; do job budget2 full com-dblp.ungraph.txt $k hot 2000 3 200 "" "KPLEX_BUDGET=$c KPLEX_TAG=c$c"; done; done
for k in 2 3; do for v in full bxc basicx; do job dblpfull $v com-dblp.ungraph.txt $k hot 2000 3 200 "" "KPLEX_RUN_CAP_S=10800"; done; done
} | xargs -P $P -S 4096 -I{} bash -c "{}"
# LiveJournal and Stack Overflow, two runs at a time (memory)
{
for v in full bxc basicx; do job lj $v soc-LiveJournal1.txt 1 hot 2000 3 200 "" "KPLEX_RUN_CAP_S=3600"; done
job lj lazymc soc-LiveJournal1.txt 1 hot 2000 3 200 "" "KPLEX_RUN_CAP_S=3600 KPLEX_EXT_GLOBAL=1 KPLEX_LAZY_MCAP_S=600"  # as in Table VI
for v in full bxc; do job lj $v soc-LiveJournal1.txt 2 hot 2000 3 200 "" "KPLEX_RUN_CAP_S=3600"; done
for k in 2 3; do for s in rand delins; do for v in full bxc; do job lj $v soc-LiveJournal1.txt $k $s 10000 3 $([ $s = rand ] && echo 2500 || echo 5000); done; done; done
job long_rand full soc-LiveJournal1.txt 1 rand 1000000 3 100000 "" "KPLEX_WINSTATS=100000 KPLEX_RUN_CAP_S=7200"
for W in 86400 604800 2592000; do for k in 1 2 3; do for v in full bxc; do
  job twin_$W $v sx-stackoverflow.txt.gz $k temporal 20000 3 2000 0.8 "KPLEX_WIN_SECONDS=$W KPLEX_LATDUMP=../results/v16/lat/sx-stackoverflow_${W}_${k}_$v"; done; done
  job twin_$W lazymc sx-stackoverflow.txt.gz 1 temporal 20000 3 2000 0.8 "KPLEX_WIN_SECONDS=$W KPLEX_LATDUMP=../results/v16/lat/sx-stackoverflow_${W}_1_lazymc"; done
} | xargs -P 2 -S 4096 -I{} bash -c "{}"
echo done
