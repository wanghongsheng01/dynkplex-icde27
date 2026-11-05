#!/bin/bash
# Fetch and build the official static solvers used as engines and baselines, at the commits used
# in the paper. They are not redistributed here (kPEX has no licence; MC-BRB and kPlexT are GPL-3).
# Usage: scripts/fetch_solvers.sh [dir]   (default ./external). Needs git, g++ (or clang++), make.
set -e
D=${1:-external}; mkdir -p "$D"; cd "$D"
SHIM=$(cd "$(dirname "$0")/.." && pwd)/code/macos_shim   # <bits/stdc++.h> and a 1-thread <omp.h> for Apple clang
CXX=${CXX:-g++}
get() { [ -d "$2" ] || git clone -q "https://github.com/$1.git" "$2"; git -C "$2" checkout -q "$3"; }
get LijunChang/MC-BRB          LijunChang_MC-BRB           26da8438b9a066e1187f1509f9dc1a5b917edfaa
get LijunChang/Maximum-kPlex   LijunChang_Maximum-kPlex    2cab2193f61de7b603d6267fd938e67c007afba1
get LijunChang/Maximum-kPlex-v2 LijunChang_Maximum-kPlex-v2 c63bfe0874df459281c895314011ba2b6d7b1261
get ShuohaoGao/kPEX            ShuohaoGao_kPEX             85dc4469f1f07d8072041eeb89c37a1df0ea3339
get ryanrossi/pmc              ryanrossi_pmc               06594a0ee1cde5a04f1679aff28072668da96062
# never run shipped binaries: rebuild everything from source
rm -f ShuohaoGao_kPEX/kPEX/kPEX ShuohaoGao_kPEX/kPEX/kPEX.exe ShuohaoGao_kPEX/no-size-constrain-kPEX/kPEX
EXTRA=""; [ "$(uname)" = Darwin ] && EXTRA="-I$SHIM"
(cd LijunChang_MC-BRB && mkdir -p .obj && $CXX -O3 -std=c++11 $EXTRA -I. main.cpp Graph.cpp -o MC-BRB)
(cd LijunChang_Maximum-kPlex && $CXX -O3 -std=c++11 $EXTRA -I. main.cpp Graph.cpp -o kPlexS)
(cd LijunChang_Maximum-kPlex-v2 && $CXX -O3 -std=c++17 $EXTRA -I. main.cpp Graph.cpp -o kPlexT)
(cd ShuohaoGao_kPEX/no-size-constrain-kPEX && $CXX -std=c++11 -O3 $EXTRA -DNO_PROGRESS_BAR -DNDEBUG -w main.cpp -o kPEX)
# PMC: fix a stream-to-bool conversion rejected by recent compilers; build single-threaded on macOS
sed -i.bak 's/return ifile;/return ifile.good();/' ryanrossi_pmc/pmc_utils.cpp
if [ "$(uname)" = Darwin ]; then
  (cd ryanrossi_pmc && $CXX -O3 -std=c++11 $EXTRA pmc_utils.cpp pmc_graph.cpp pmc_clique_utils.cpp pmc_heu.cpp pmc_maxclique.cpp pmcx_maxclique.cpp pmcx_maxclique_basic.cpp pmc_cores.cpp pmc_driver.cpp -o pmc)
else
  (cd ryanrossi_pmc && make -s)
fi
echo "built: MC-BRB kPlexS kPlexT kPEX pmc in $D"
