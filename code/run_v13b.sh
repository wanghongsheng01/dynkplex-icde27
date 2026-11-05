#!/bin/bash
# BasicX with the kplex12 build (its insertion searches use the same budget and core-restricted fallback),
# so that every budgeted variant in results/v13 comes from one build. Output: results/v13b, then copied into v13.
cd "$(dirname "$0")"
R=../results/v13b; mkdir -p $R; S=./ext_tmp; P=${1:-3}
eng() { [ $1 = 1 ] && echo mcbrb || echo kpex; }
job() {  # graph k stream Q seed sample [wfrac]
  echo "env KPLEX_EXT_GLOBAL=1 KPLEX_NOWARM=1 KPLEX_EXT=$(eng $2) KPLEX_EXT_DIR=$S/v13b_${1}_$2_$3_$5 ./kplex12 dyn ../data/$1.txt $2 $3 $4 $5 $6 $7 >> $R/$3_basicx.jsonl 2>> $R/err_$3_basicx.log"
}
{
for seed in 3 4 5; do for k in 1 2 3; do
  for g in com-dblp.ungraph email-Enron soc-Epinions1; do job $g $k hot 2000 $seed 200; done; job com-youtube.ungraph $k hot 1000 $seed 200
done; done
for seed in 3 4 5; do for k in 1 2 3; do
  for g in sx-mathoverflow sx-askubuntu; do job $g $k temporal 10000 $seed 1000; job $g $k tinc 20000 $seed 2000 0.1; done
  for g in email-Enron soc-Epinions1 com-dblp.ungraph com-youtube.ungraph; do job $g $k rand 10000 $seed 1000; job $g $k delins 10000 $seed 2000; done
done; done
for k in 1 2 3; do for g in wiki-Talk soc-pokec-relationships; do job $g $k rand 10000 3 2500; job $g $k delins 10000 3 5000; done; done
} | xargs -P $P -S 4096 -I{} bash -c "{}"
for s in hot rand delins temporal tinc; do cp $R/${s}_basicx.jsonl ../results/v13/${s}_basicx.jsonl; done
echo done
