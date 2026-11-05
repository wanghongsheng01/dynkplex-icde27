#!/bin/bash
# Download the SNAP graphs used in the paper into ./data (about 1.6 GB uncompressed).
set -e
mkdir -p data; cd data
for f in email-Enron ca-AstroPh soc-Epinions1 com-dblp.ungraph com-youtube.ungraph wiki-Talk soc-pokec-relationships soc-LiveJournal1 sx-mathoverflow sx-askubuntu; do
  [ -f $f.txt ] && continue
  case $f in com-*) u=https://snap.stanford.edu/data/bigdata/communities/$f.txt.gz;; *) u=https://snap.stanford.edu/data/$f.txt.gz;; esac
  curl -fsSLO "$u" && gunzip -k $f.txt.gz
done
ls -la
