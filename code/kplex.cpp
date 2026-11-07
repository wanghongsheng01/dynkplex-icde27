// Exact maintenance of a maximum k-plex under edge insertions/deletions (pilot).
// Modes:
//   kplex static  <edgefile> k
//   kplex dyn     <edgefile> k <rand|hot|temporal|tinc|delins> Q seed sample_every [window_frac]
//   env KPLEX_ALGO=lazymc: LazyMC baseline (Sun et al., TKDE 2022), k = 1 only
//   env KPLEX_REPAIR=1: local repair on deletions; KPLEX_CACHE=1: certificate cache + core bound
//   kplex brute   trials seed            (randomised correctness test vs. brute force)
// Instrumentation added for the pre-submission review (kplex14; default behaviour unchanged):
//   streams hotrev (insertions re-insert deleted optimum edges) and takedown (non-adaptive: deletes the
//   edges of the initial dense region in a fixed order); KPLEX_HOT_INSFRAC = insertion share (default 0.5)
//   KPLEX_REGION=twohop|core (insertion region ablation), KPLEX_NONADJ (k = 2: y adjacent to u or v)
//   KPLEX_THRESH_REL=d (alert "omega >= omega0 - d" instead of the maximum), KPLEX_APPROX (no global search)
//   KPLEX_PI_REBUILD=N (drop the cache every N updates), KPLEX_WINSTATS=N (per-window averages)
//   KPLEX_TRACE=file (omega after every update), KPLEX_LATDUMP=file (timestamp and latency per update)
//   JSON tail10: the 10 slowest updates as [total, local search, fallback, external call, budget] in ms
#include <algorithm>
#include <array>
#include <climits>
#include <unistd.h>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <numeric>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
using namespace std;
using Clock = chrono::steady_clock;
static double secs(Clock::time_point a) { return chrono::duration<double>(Clock::now() - a).count(); }

// ---------------------------------------------------------------- graph
struct DynGraph {
  int n = 0;
  long long m = 0;
  vector<vector<int>> adj;  // sorted
  explicit DynGraph(int n_ = 0) : n(n_), adj(n_) {}
  bool has(int u, int v) const {
    const auto &a = adj[u].size() < adj[v].size() ? adj[u] : adj[v];
    int x = adj[u].size() < adj[v].size() ? v : u;
    return binary_search(a.begin(), a.end(), x);
  }
  bool add(int u, int v) {
    if (u == v || has(u, v)) return false;
    adj[u].insert(lower_bound(adj[u].begin(), adj[u].end(), v), v);
    adj[v].insert(lower_bound(adj[v].begin(), adj[v].end(), u), u);
    m++;
    return true;
  }
  bool del(int u, int v) {
    if (u == v || !has(u, v)) return false;
    adj[u].erase(lower_bound(adj[u].begin(), adj[u].end(), v));
    adj[v].erase(lower_bound(adj[v].begin(), adj[v].end(), u));
    m--;
    return true;
  }
  int deg(int u) const { return (int)adj[u].size(); }
};

// Batagelj-Zaversnik core decomposition; also returns peel order and the
// degeneracy-peeling k-plex heuristic (largest suffix of the order that is a k-plex).
struct CoreInfo {
  vector<int> core, order, pos;
  int heurSize = 0, heurStart = 0;
};
static CoreInfo coreDecomp(const DynGraph &G, int k) {
  int n = G.n, md = 0;
  CoreInfo ci;
  vector<int> deg(n), bin, vert(n), pos(n);
  for (int v = 0; v < n; v++) deg[v] = G.deg(v), md = max(md, deg[v]);
  bin.assign(md + 1, 0);
  for (int v = 0; v < n; v++) bin[deg[v]]++;
  int start = 0;
  for (int d = 0; d <= md; d++) { int c = bin[d]; bin[d] = start; start += c; }
  for (int v = 0; v < n; v++) { pos[v] = bin[deg[v]]; vert[pos[v]] = v; bin[deg[v]]++; }
  for (int d = md; d >= 1; d--) bin[d] = bin[d - 1];
  if (md >= 0) bin[0] = 0;
  bool heurDone = false;
  // true remaining degrees (BZ clamps deg[]), with a bucket count to track the minimum
  vector<int> rdeg(n), cntDeg(md + 2, 0);
  vector<char> removed(n, 0);
  for (int v = 0; v < n; v++) rdeg[v] = G.deg(v), cntDeg[rdeg[v]]++;
  int minD = 0;
  while (minD <= md && cntDeg[minD] == 0) minD++;
  for (int i = 0; i < n; i++) {
    int v = vert[i];
    while (minD <= md && cntDeg[minD] == 0) minD++;
    if (!heurDone && minD >= (n - i) - k) { ci.heurSize = n - i; ci.heurStart = i; heurDone = true; }
    removed[v] = 1; cntDeg[rdeg[v]]--;
    for (int u : G.adj[v]) if (!removed[u]) { cntDeg[rdeg[u]]--; rdeg[u]--; cntDeg[rdeg[u]]++; if (rdeg[u] < minD) minD = rdeg[u]; }
    for (int u : G.adj[v]) {
      if (deg[u] > deg[v]) {
        int du = deg[u], pu = pos[u], pw = bin[du], w = vert[pw];
        if (u != w) { pos[u] = pw; vert[pu] = w; pos[w] = pu; vert[pw] = u; }
        bin[du]++; deg[u]--;
      }
    }
  }
  ci.core = deg; ci.order = vert; ci.pos = pos;
  return ci;
}

// ---------------------------------------------------------------- branch and bound on a small local graph
// Optional deadline for local searches (set per update by the dynamic algorithm when a static
// engine is available to fall back on); a search that passes it reports `aborted`.
static Clock::time_point g_deadline = Clock::time_point::max();
static bool g_aborted = false;
static const long long g_checkMask = getenv("STRESS_EVERY") ? 0 : 255;  // test hook: check the deadline at every node
struct LocalBB {
  int L = 0, W = 0, k = 1, t = 0;  // t = target size (lb+1)
  bool decision = false, found = false;
  vector<vector<int>> *all = nullptr;  // enumerate mode: collect every plex of size >= t
  long long allCap = 100000;
  long long nodes = 0, nodeLimit = LLONG_MAX;
  vector<uint64_t> A;  // L*W adjacency bitsets
  vector<int> bestLocal;
  uint64_t *row(int x) { return &A[(size_t)x * W]; }
  void init(int L_, int k_) { L = L_; W = (L + 63) / 64; k = k_; A.assign((size_t)L * W, 0); }
  void setEdge(int a, int b) { row(a)[b >> 6] |= 1ULL << (b & 63); row(b)[a >> 6] |= 1ULL << (a & 63); }
  static int pc(const uint64_t *x, const uint64_t *y, int W) { int c = 0; for (int i = 0; i < W; i++) c += __builtin_popcountll(x[i] & y[i]); return c; }
  static int pc1(const uint64_t *x, int W) { int c = 0; for (int i = 0; i < W; i++) c += __builtin_popcountll(x[i]); return c; }
  template <class F> static void forEach(const uint64_t *x, int W, F f) {
    for (int i = 0; i < W; i++) { uint64_t b = x[i]; while (b) { int j = __builtin_ctzll(b); f(i * 64 + j); b &= b - 1; } }
  }
  static bool test(const uint64_t *x, int j) { return (x[j >> 6] >> (j & 63)) & 1; }
  static void clr(uint64_t *x, int j) { x[j >> 6] &= ~(1ULL << (j & 63)); }
  static void set1(uint64_t *x, int j) { x[j >> 6] |= 1ULL << (j & 63); }

  void bb(vector<uint64_t> S, vector<uint64_t> C) {
    if (found && decision) return;
    if (++nodes > nodeLimit) return;
    if (((nodes & g_checkMask) == 0) && g_deadline != Clock::time_point::max() && Clock::now() > g_deadline) { g_aborted = true; nodeLimit = 0; return; }
    vector<uint64_t> U(W);
    // reduction
    for (bool changed = true; changed;) {
      changed = false;
      int sS = pc1(S.data(), W);
      // saturated S vertices: every C vertex must be adjacent to them
      bool bad = false;
      forEach(S.data(), W, [&](int u) {
        int miss = sS - pc(row(u), S.data(), W);
        if (miss > k) bad = true;
        else if (miss == k) for (int i = 0; i < W; i++) C[i] &= row(u)[i];
      });
      if (bad) return;
      // candidate must itself keep miss <= k
      forEach(C.data(), W, [&](int w) { if (sS - pc(row(w), S.data(), W) + 1 > k) { clr(C.data(), w); } });
      for (int i = 0; i < W; i++) U[i] = S[i] | C[i];
      int tot = pc1(U.data(), W);
      if (tot < t) return;
      forEach(C.data(), W, [&](int w) { if (pc(row(w), U.data(), W) < t - k) { clr(C.data(), w); changed = true; } });
      if (changed) continue;
      bool dead = false;
      forEach(S.data(), W, [&](int u) { if (pc(row(u), U.data(), W) < t - k) dead = true; });
      if (dead) return;
    }
    int tot = pc1(U.data(), W);
    // is U a k-plex?
    int minDeg = INT_MAX, minV = -1;
    forEach(U.data(), W, [&](int x) { int d = pc(row(x), U.data(), W); if (d < minDeg) { minDeg = d; minV = x; } });
    if (tot - minDeg <= k) {  // miss(x) = tot - deg(x) counts x itself
      found = true;
      bestLocal.clear();
      forEach(U.data(), W, [&](int x) { bestLocal.push_back(x); });
      if (all) { if ((long long)all->size() < allCap) all->push_back(bestLocal); return; }  // enumerate, keep t
      if (!decision) t = tot + 1;
      return;
    }
    // colouring bound: an independent set holds at most k members of a k-plex. Decisive for
    // cliques; for k >= 2 the bound is weak and its cost outweighs the pruning (measured), so off.
    if (k == 1 && !getenv("KPLEX_NOCOLOUR")) {
      int ubc = pc1(S.data(), W), csum = 0;
      vector<uint64_t> rem = C, cls(W);
      while (ubc + csum < t) {
        bool any = false;
        for (int i = 0; i < W; i++) if (rem[i]) { any = true; break; }
        if (!any) break;
        // one greedy colour class from rem
        int sz = 0;
        for (int i = 0; i < W; i++) cls[i] = rem[i];
        for (int i = 0; i < W; i++) while (cls[i]) {
          int x = i * 64 + __builtin_ctzll(cls[i]);
          clr(rem.data(), x); sz++;
          for (int q = 0; q < W; q++) cls[q] &= ~row(x)[q];
          clr(cls.data(), x);
        }
        csum += min(sz, k);
      }
      bool left = false;
      for (int i = 0; i < W; i++) if (rem[i]) { left = true; break; }
      if (!left && ubc + csum < t) return;
    }
    // partition upper bound
    {
      int sS = pc1(S.data(), W);
      vector<uint64_t> Cr = C;
      vector<int> Sv, sup;
      forEach(S.data(), W, [&](int u) { Sv.push_back(u); sup.push_back(k - (sS - pc(row(u), S.data(), W))); });
      vector<char> used(Sv.size(), 0);
      int ub = sS;
      while (true) {
        int bi = -1, bestGain = 0, bestPi = 0;
        for (size_t i = 0; i < Sv.size(); i++) if (!used[i]) {
          int pi = pc1(Cr.data(), W) - pc(Cr.data(), row(Sv[i]), W);
          int gain = pi - min(pi, sup[i]);
          if (gain > bestGain) { bestGain = gain; bi = (int)i; bestPi = pi; }
        }
        if (bi < 0) break;
        used[bi] = 1;
        ub += min(bestPi, sup[bi]);
        for (int i = 0; i < W; i++) Cr[i] &= row(Sv[bi])[i];
      }
      ub += pc1(Cr.data(), W);
      if (ub < t) return;
    }
    // branching vertex
    int w = -1;
    if (test(C.data(), minV)) w = minV;
    else {
      int bd = INT_MAX;
      forEach(C.data(), W, [&](int x) { if (!test(row(minV), x)) { int d = pc(row(x), U.data(), W); if (d < bd) { bd = d; w = x; } } });
      if (w < 0) forEach(C.data(), W, [&](int x) { if (w < 0) w = x; });
    }
    if (w < 0) return;
    vector<uint64_t> S2 = S, C2 = C;
    set1(S2.data(), w); clr(C2.data(), w);
    bb(S2, C2);
    if (found && decision) return;
    clr(C.data(), w);
    bb(S, C);
  }
};

// Build a local subproblem over vertices ids (first nf forced), prune by the pair
// common-neighbour bound w.r.t. every forced vertex and by degree, then branch.
struct SubResult { bool found; vector<int> plex; long long nodes; bool aborted = false; };
static vector<int> g_loc;  // scratch: global id -> local id (-1 = absent)
static void buildLocalEdges(const DynGraph &G, const vector<int> &ids, LocalBB &bb) {
  int L = (int)ids.size();
  if ((int)g_loc.size() < G.n) g_loc.assign(G.n, -1);
  for (int i = 0; i < L; i++) g_loc[ids[i]] = i;
  for (int i = 0; i < L; i++) {
    const auto &a = G.adj[ids[i]];
    if ((long long)a.size() <= 8LL * L) {
      for (int x : a) { int j = g_loc[x]; if (j > i) bb.setEdge(i, j); }
    } else {
      for (int j = i + 1; j < L; j++) if (binary_search(a.begin(), a.end(), ids[j])) bb.setEdge(i, j);
    }
  }
  for (int i = 0; i < L; i++) g_loc[ids[i]] = -1;
}
// Build a local subproblem over vertices ids (first nf forced), prune by the pair
// common-neighbour bound w.r.t. every forced vertex and by degree, then branch.
static SubResult solveLocal0(const DynGraph &G, const vector<int> &ids, int nf, int k, int t, bool decision);
static double g_searchSum = 0;  // time in local decision searches (budgeted work)
static SubResult solveLocal(const DynGraph &G, const vector<int> &ids, int nf, int k, int t, bool decision) {
  auto t0 = Clock::now();
  SubResult r = solveLocal0(G, ids, nf, k, t, decision);
  g_searchSum += secs(t0);
  return r;
}
static SubResult solveLocal0(const DynGraph &G, const vector<int> &ids, int nf, int k, int t, bool decision) {
  LocalBB bb;
  int L = (int)ids.size();
  bb.init(L, k);
  bb.t = t; bb.decision = decision;
  buildLocalEdges(G, ids, bb);
  int W = bb.W;
  vector<uint64_t> alive(W, 0), S(W, 0);
  for (int i = 0; i < L; i++) LocalBB::set1(alive.data(), i);
  for (int f = 0; f < nf; f++) LocalBB::set1(S.data(), f);
  // iterative pruning of non-forced vertices
  for (bool ch = true; ch;) {
    ch = false;
    for (int i = nf; i < L; i++) if (LocalBB::test(alive.data(), i)) {
      bool ok = LocalBB::pc(bb.row(i), alive.data(), W) >= t - k;
      if (ok && t >= 2 * k - 1)
        for (int f = 0; f < nf && ok; f++) {
          int cn = 0; const uint64_t *rf = bb.row(f), *ri = bb.row(i);
          for (int q = 0; q < W; q++) cn += __builtin_popcountll(rf[q] & ri[q] & alive[q]);
          ok = cn >= (LocalBB::test(rf, i) ? t - 2 * k : t - 2 * k + 2);
        }
      if (!ok) { LocalBB::clr(alive.data(), i); ch = true; }
    }
  }
  for (int f = 0; f < nf; f++) if (LocalBB::pc(bb.row(f), alive.data(), W) < t - k) return {false, {}, 0};
  if (LocalBB::pc1(alive.data(), W) < t) return {false, {}, 0};
  {  // greedy colouring bound: sum over colour classes of min(|class|, k)
    vector<vector<uint64_t>> cls;
    vector<int> csz;
    LocalBB::forEach(alive.data(), W, [&](int x) {
      size_t c = 0;
      for (; c < cls.size(); c++) { bool clash = false; for (int q = 0; q < W && !clash; q++) clash = (bb.row(x)[q] & cls[c][q]) != 0; if (!clash) break; }
      if (c == cls.size()) { cls.emplace_back(W, 0); csz.push_back(0); }
      LocalBB::set1(cls[c].data(), x); csz[c]++;
    });
    int cub = 0; for (int z : csz) cub += min(z, k);
    if (cub < t) return {false, {}, 0};
  }
  vector<uint64_t> C = alive;
  for (int f = 0; f < nf; f++) LocalBB::clr(C.data(), f);
  bool ab0 = g_aborted; g_aborted = false;
  bb.bb(S, C);
  SubResult r{bb.found, {}, bb.nodes};
  r.aborted = g_aborted && !bb.found; g_aborted = ab0 || g_aborted;
  if (bb.found) for (int x : bb.bestLocal) r.plex.push_back(ids[x]);
  return r;
}

// Greedy k-plex inside {v} + later neighbours: peel min-degree vertices until a k-plex remains.
static vector<int> egoGreedy(const DynGraph &G, const CoreInfo &ci, int v, int k) {
  vector<int> ids{v};
  for (int w : G.adj[v]) if (ci.pos[w] > ci.pos[v]) ids.push_back(w);
  LocalBB bb; int L = (int)ids.size(); bb.init(L, k);
  buildLocalEdges(G, ids, bb);
  vector<uint64_t> alive(bb.W, 0);
  for (int i = 0; i < L; i++) LocalBB::set1(alive.data(), i);
  int sz = L;
  while (sz > 0) {
    int md = INT_MAX, mv = -1;
    LocalBB::forEach(alive.data(), bb.W, [&](int x) { int d = LocalBB::pc(bb.row(x), alive.data(), bb.W); if (d < md) { md = d; mv = x; } });
    if (sz - md <= k) break;
    LocalBB::clr(alive.data(), mv); sz--;
  }
  vector<int> out;
  LocalBB::forEach(alive.data(), bb.W, [&](int x) { out.push_back(ids[x]); });
  return out;
}

// ---------------------------------------------------------------- static exact solver
struct StaticStats { double time = 0; long long nodes = 0; int subproblems = 0; int keptVertices = 0; int heurLb = 0; long long sumL = 0; };
// Returns a maximum k-plex. If lbInit>=0 the search only looks for plexes of size > lbInit
// (decision: stop at the first plex of size lbInit+1). Returns empty if none larger than lbInit exists.
static vector<int> staticSolve(const DynGraph &G, int k, int lbInit, bool decision, StaticStats &st) {
  auto t0 = Clock::now();
  CoreInfo ci = coreDecomp(G, k);
  vector<int> best;
  int lb = lbInit;
  if (lbInit < 0) {
    best.assign(ci.order.begin() + ci.heurStart, ci.order.end());
    lb = (int)best.size();
    // ego-network greedy on the highest-core vertices
    for (int i = G.n - 1, c = 0; i >= 0 && c < 2000; i--, c++) {
      int v = ci.order[i];
      if (ci.core[v] + k <= lb) break;
      vector<int> g = egoGreedy(G, ci, v, k);
      if ((int)g.size() > lb) { best = g; lb = (int)g.size(); }
    }
    st.heurLb = lb;
  }
  if (getenv("KPLEX_PROF")) fprintf(stderr, "core+heur %.3f\n", secs(t0));
  int t = lb + 1;
  // physically reduce to the (t-k)-core and relabel
  vector<int> keep, newId(G.n, -1);
  for (int v = 0; v < G.n; v++) if (ci.core[v] >= t - k) { newId[v] = (int)keep.size(); keep.push_back(v); }
  DynGraph R((int)keep.size());
  for (int i = 0; i < R.n; i++) {
    for (int w : G.adj[keep[i]]) if (newId[w] >= 0) R.adj[i].push_back(newId[w]);
    sort(R.adj[i].begin(), R.adj[i].end());
  }
  CoreInfo rc = coreDecomp(R, k);
  if (getenv("KPLEX_PROF")) fprintf(stderr, "reduce %.3f\n", secs(t0));
  struct P { StaticStats &s; ~P() { if (getenv("KPLEX_PROF")) fprintf(stderr, "avgL %.1f\n", s.subproblems ? (double)s.sumL / s.subproblems : 0.0); } } prof{st};
  int n = R.n;
  st.keptVertices = n;
  // adjacency sorted by peel position, descending: neighbours after position i form a prefix
  vector<vector<int>> desc(n);
  for (int v = 0; v < n; v++) {
    desc[v] = R.adj[v];
    sort(desc[v].begin(), desc[v].end(), [&](int a, int b) { return rc.pos[a] > rc.pos[b]; });
  }
  vector<int> cnt(n, 0), touched;
  vector<char> inN(n, 0);
  for (int i = 0; i < n; i++) {
    int v = rc.order[i];
    if (rc.core[v] < t - k) continue;
    // candidates: later vertices, core >= t-k, within 2 hops through later vertices
    vector<int> ids{v}, nb;
    for (int w : desc[v]) { if (rc.pos[w] <= i) break; if (rc.core[w] >= t - k) { nb.push_back(w); inN[w] = 1; } }
    if ((int)nb.size() < t - k) { for (int w : nb) inN[w] = 0; continue; }
    if (k == 1) {
      ids.insert(ids.end(), nb.begin(), nb.end());  // a clique contains only neighbours of v
    } else if (t >= 2 * k - 1) {
      for (int w : nb) for (int y : desc[w]) { if (rc.pos[y] <= i) break; if (rc.core[y] >= t - k) { if (cnt[y]++ == 0) touched.push_back(y); } }
      for (int y : touched) {
        bool ok = inN[y] ? cnt[y] >= t - 2 * k : (k >= 2 && cnt[y] >= t - 2 * k + 2);
        if (ok) ids.push_back(y);
      }
      for (int w : nb) if (cnt[w] == 0 && t - 2 * k <= 0) ids.push_back(w);  // neighbours with no later common nbr
      for (int y : touched) cnt[y] = 0;
      touched.clear();
    } else {
      for (int j = i + 1; j < n; j++) if (rc.core[rc.order[j]] >= t - k) ids.push_back(rc.order[j]);
    }
    for (int w : nb) inN[w] = 0;
    if ((int)ids.size() < t) continue;
    st.subproblems++;
    st.sumL += ids.size();
    SubResult r = solveLocal(R, ids, 1, k, t, decision);
    st.nodes += r.nodes;
    if (r.found) {
      best.clear(); for (int x : r.plex) best.push_back(keep[x]);
      lb = (int)best.size(); t = lb + 1;
      if (decision) break;
    }
  }
  st.time = secs(t0);
  if (lbInit >= 0 && (int)best.size() <= lbInit) best.clear();
  return best;
}

static bool isKPlex(const DynGraph &G, const vector<int> &S, int k) {
  for (int u : S) { int d = 0; for (int w : S) if (w != u && G.has(u, w)) d++; if ((int)S.size() - d > k) return false; }
  return true;
}

// ---------------------------------------------------------------- external exact engines
// KPLEX_EXT = kplexs | kplext | kpex | mcbrb | pmc runs the official solver (built from source under
// KPLEX_EXT_BIN, default ../external) on a file copy of the current graph written to KPLEX_EXT_DIR
// (default ./ext_tmp_<pid>, removed afterwards). When a witness is needed, the solver's own
// solution is read back (MC-BRB "output", kPlexS/kPlexT -o, the sets kPEX and PMC print) and
// checked; if it is missing or invalid, our engine finds one as a decision problem at omega - 1.
// Reported time is the solver's wall time (process start to exit, file reading and solution
// output included), without the time to write the graph file.
static double g_extWallSum = 0, g_extOwnSum = 0;  // all external calls of the process
struct ExtResult { int omega = -1; double wall = 0, own = 0, write = 0; string engine; vector<int> wit; };
static const char *extEngine() { const char *e = getenv("KPLEX_EXT"); return e && *e ? e : nullptr; }
static void writeU32(FILE *f, uint32_t x) { fwrite(&x, 4, 1, f); }
static void writeAdjBin(const DynGraph &G, FILE *deg, FILE *adj) {  // Chang's / kPEX binary layout
  writeU32(deg, 4); writeU32(deg, (uint32_t)G.n); writeU32(deg, (uint32_t)(2 * G.m));
  for (int v = 0; v < G.n; v++) writeU32(deg, (uint32_t)G.deg(v));
  for (int v = 0; v < G.n; v++) if (!G.adj[v].empty()) fwrite(G.adj[v].data(), 4, G.adj[v].size(), adj);
}
static vector<int> intsIn(const string &s) {
  vector<int> v; long long x = -1;
  for (char c : s) { if (isdigit((unsigned char)c)) x = (x < 0 ? 0 : x * 10) + (c - '0'); else if (x >= 0) { v.push_back((int)x); x = -1; } }
  if (x >= 0) v.push_back((int)x);
  return v;
}
static string absPath(const string &p) {
  char buf[PATH_MAX];
  if (!realpath(p.c_str(), buf)) { fprintf(stderr, "no such path %s\n", p.c_str()); exit(1); }
  return buf;
}
static ExtResult runExternal(const DynGraph &G, int k, bool wantWit = false) {
  ExtResult r;
  r.engine = extEngine();
  string bin = absPath(getenv("KPLEX_EXT_BIN") ? getenv("KPLEX_EXT_BIN") : "../external");
  string dir = getenv("KPLEX_EXT_DIR") ? getenv("KPLEX_EXT_DIR") : "./ext_tmp";
  dir += "_" + to_string((long long)getpid());
  if (system(("mkdir -p '" + dir + "'").c_str()) != 0) { fprintf(stderr, "cannot create %s\n", dir.c_str()); exit(1); }
  dir = absPath(dir);
  auto tw = Clock::now();
  string cmd, ks = to_string(k);
  const string &e = r.engine;
  if (e == "kplexs" || e == "kplext" || e == "mcbrb") {
    FILE *fd = fopen((dir + "/b_degree.bin").c_str(), "wb"), *fa = fopen((dir + "/b_adj.bin").c_str(), "wb");
    writeAdjBin(G, fd, fa); fclose(fd); fclose(fa);
    if (e == "kplexs" && k < 2) { fprintf(stderr, "kPlexS is not exact at k = 1; use mcbrb or pmc\n"); exit(1); }
    // the solvers write their solution to the working directory, hence the cd
    if (e == "kplexs") cmd = "cd '" + dir + "' && " + bin + "/LijunChang_Maximum-kPlex/kPlexS -g '" + dir + "' -a exact -k " + ks + " -b" + (wantWit ? " -o" : "");
    else if (e == "kplext") {
      if (k < 2) { fprintf(stderr, "kPlexT needs k >= 2\n"); exit(1); }
      cmd = "cd '" + dir + "' && " + bin + "/LijunChang_Maximum-kPlex-v2/kPlexT -g '" + dir + "' -k " + ks + " -b" + (wantWit ? " -o" : "");
    }
    else { if (k != 1) { fprintf(stderr, "mcbrb is a clique solver (k = 1)\n"); exit(1); } cmd = "cd '" + dir + "' && " + bin + "/LijunChang_MC-BRB/MC-BRB MC-BRB '" + dir + "'" + (wantWit ? " output" : ""); }
  } else if (e == "kpex") {
    // kPEX reads one file: the degree block followed by the adjacency block
    string path = dir + "/g.bin";
    FILE *f = fopen(path.c_str(), "wb");
    writeAdjBin(G, f, f); fclose(f);
    cmd = bin + "/ShuohaoGao_kPEX/no-size-constrain-kPEX/kPEX '" + path + "' " + ks;
  } else if (e == "pmc") {
    if (k != 1) { fprintf(stderr, "pmc is a clique solver (k = 1)\n"); exit(1); }
    string path = dir + "/g.edges";
    FILE *f = fopen(path.c_str(), "w");
    for (int u = 0; u < G.n; u++) for (int v : G.adj[u]) if (u < v) fprintf(f, "%d %d\n", u, v);
    fclose(f);
    cmd = bin + "/ryanrossi_pmc/pmc -f '" + path + "' -t 1";
  } else { fprintf(stderr, "unknown KPLEX_EXT=%s\n", e.c_str()); exit(1); }
  r.write = secs(tw);
  auto t0 = Clock::now();
  FILE *p = popen((cmd + " 2>&1").c_str(), "r");
  string out; char buf[4096];
  while (fgets(buf, sizeof buf, p)) out += buf;
  pclose(p);
  r.wall = secs(t0);
  if (wantWit) {
    string wf = e == "mcbrb" ? "/clique.txt" : e == "kplexs" || e == "kplext" ? "/kplexes.txt" : "";
    if (!wf.empty()) {
      ifstream in(dir + wf); stringstream ss; ss << in.rdbuf();
      vector<int> v = intsIn(ss.str());
      if (!v.empty() && (int)v.size() == v[0] + 1) r.wit.assign(v.begin() + 1, v.end());
    } else if (e == "kpex") {
      size_t i = out.rfind("Maximum solution(size=");
      if (i != string::npos) { i = out.find('\n', i); size_t j = out.find('\n', i + 1); if (i != string::npos) r.wit = intsIn(out.substr(i + 1, j - i - 1)); }
    } else {  // pmc prints 1-indexed ids
      size_t i = out.rfind("Maximum clique: ");
      if (i != string::npos) { size_t j = out.find('\n', i); r.wit = intsIn(out.substr(i + 16, j - i - 16)); for (int &x : r.wit) x--; }
    }
  }
  system(("rm -rf '" + dir + "'").c_str());
  auto grab = [&](const char *key) -> long long {  // number after the last occurrence of key
    size_t i = out.rfind(key);
    if (i == string::npos) return -1;
    i += strlen(key);
    while (i < out.size() && !isdigit((unsigned char)out[i])) i++;
    long long x = 0;  // Chang's tools print thousands separators
    for (; i < out.size() && (isdigit((unsigned char)out[i]) || out[i] == ','); i++) if (out[i] != ',') x = x * 10 + (out[i] - '0');
    return x;
  };
  if (e == "kplexs") { r.omega = (int)grab("Maximum kPlex Size:"); r.own = grab("Total Time:") / 1e6; }
  else if (e == "kplext") { r.omega = (int)grab("Maximum kPlex Size:"); r.own = grab("Total Time:") / 1e6; }
  else if (e == "mcbrb") { r.omega = (int)grab("Maximum Clique Size:"); if (r.omega < 0) r.omega = (int)grab("Max Clique Size:"); r.own = grab("Total Time:") / 1e6; }
  else if (e == "kpex") { r.omega = (int)grab("ground truth="); size_t i = out.rfind("kPEX time:"); if (i != string::npos) r.own = atof(out.c_str() + i + 10); }
  else { r.omega = (int)grab("Size (omega):"); if (r.omega < 0 && out.find("Heuristic found optimal") != string::npos) r.omega = (int)grab("Heuristic found clique of size"); r.own = -1; }
  if (r.omega <= 0) { fprintf(stderr, "external solver %s gave no answer; output:\n%s\n", e.c_str(), out.c_str()); exit(1); }
  if (getenv("KPLEX_EXT_LOG") && string(getenv("KPLEX_EXT_LOG")) == "2") fprintf(stderr, "%s", out.c_str());
  if (getenv("KPLEX_EXT_LOG")) fprintf(stderr, "[ext %s] omega=%d wall=%.3fs own=%.3fs write=%.3fs\n", e.c_str(), r.omega, r.wall, r.own, r.write);
  g_extWallSum += r.wall; g_extOwnSum += r.own;
  return r;
}
// The exact static engine used outside the update path (initial answer, checkpoint baseline):
// ours by default, or an official solver when KPLEX_EXT is set. st.time is the engine time: the
// solver's own reported time when it prints one (kPlexS/kPlexT/MC-BRB/kPEX; this excludes part of
// the file reading and favours the baseline), else its wall time.
static double g_lastExtWall = 0;  // wall time of the last external solve, graph export included
static vector<int> solveExact(const DynGraph &G, int k, bool witness, StaticStats &st, int *omegaOut = nullptr) {
  if (!extEngine()) { vector<int> b = staticSolve(G, k, -1, false, st); if (omegaOut) *omegaOut = (int)b.size(); return b; }
  ExtResult r = runExternal(G, k, witness);
  st.time = r.own > 0 ? r.own : r.wall;
  g_lastExtWall = r.wall + r.write;
  if (omegaOut) *omegaOut = r.omega;
  if (!witness) return {};
  {
    vector<int> w = r.wit;
    sort(w.begin(), w.end()); w.erase(unique(w.begin(), w.end()), w.end());
    bool ok = (int)w.size() == r.omega && (w.empty() || (w[0] >= 0 && w.back() < G.n)) && isKPlex(G, w, k);
    if (getenv("KPLEX_EXT_LOG")) fprintf(stderr, "[ext] solver witness %s (%zu vertices)\n", ok ? "valid" : "INVALID", r.wit.size());
    if (ok) return w;
  }
  StaticStats sw;
  vector<int> b = r.omega > 1 ? staticSolve(G, k, r.omega - 1, true, sw) : vector<int>{};
  if ((int)b.size() != r.omega) {  // fall back to the full search; also flags a disagreement
    fprintf(stderr, "witness search at %d returned %zu\n", r.omega, b.size());
    StaticStats s2; b = staticSolve(G, k, -1, false, s2);
  }
  if (getenv("KPLEX_EXT_LOG")) fprintf(stderr, "[ext] witness of size %zu in %.3fs\n", b.size(), sw.time);
  return b;
}

// KPLEX_EXT_GLOBAL (with KPLEX_EXT): both dynamic methods hand their whole-graph re-solves to the
// external engine instead of our branch-and-bound; local searches stay ours.
static bool extGlobal() { return extEngine() && getenv("KPLEX_EXT_GLOBAL"); }

// ---------------------------------------------------------------- dynamic maintenance
struct DynStats {
  long long ins = 0, del = 0, insSeeded = 0, insGrow = 0, delHit = 0, delRepairLocal = 0, delRepairSwap = 0, delGlobal = 0, delShrink = 0, cacheBuilds = 0, cacheSub = 0, invalidated = 0, warmWitness = 0, cacheSubInv = 0, levelReuse = 0, fallbacks = 0, invPassed = 0, coreSubN = 0;
  double tIns = 0, tDel = 0, tGlobal = 0;
  long long nodes = 0;
  double curFb = 0, curExt = 0;  // per update: time in fallback solves / in external whole-graph or core calls
  vector<int> xsizes;            // size of the insertion region X of every growing-insertion test
  long long fwdSum = 0, fwdCnt = 0;  // forward-region sizes of cache subproblems
};
// Core-number maintenance under edge updates. Deletion peels with fresh max-core-degree
// counts (as in OrderRemoval). Insertion is order-based (Zhang et al., ICDE'17, in the
// simplified form of Guo & Sekerinski, 2022): a k-order is kept as one labelled list per
// core value, with dout(v) = #neighbours after v, and only vertices reached from the
// earlier endpoint inside its level are visited. KPLEX_COREALG=trav selects the older
// traversal (purecore) insertion, which keeps no order.
struct CoreMaint {
  DynGraph &G;
  vector<int> K, hist, cd, mark, dropped, proc;
  int maxK = 0, stamp = 0;
  long long visited = 0, relabels = 0;
  double secs = 0;
  bool ordered = true;
  static constexpr long long GAP = 1LL << 24;
  vector<long long> lab;
  vector<int> nxt, prv, head, tail, dout, din;
  vector<char> st;  // insertion state: 0 untouched, 1 queued, 2 candidate, 3 settled, 4 removal pending
  explicit CoreMaint(DynGraph &g) : G(g) {
    const char *a = getenv("KPLEX_COREALG");
    ordered = !(a && string(a) == "trav");
  }
  void init() {
    CoreInfo ci = coreDecomp(G, 1);
    K = ci.core;
    hist.assign(G.n + 2, 0);
    maxK = 0;
    for (int x : K) { hist[x]++; maxK = max(maxK, x); }
    cd.assign(G.n, 0); mark.assign(G.n, 0); dropped.assign(G.n, 0); proc.assign(G.n, 0);
    if (!ordered) return;
    // the peeling order has non-decreasing core numbers and dout(v) <= K(v)
    lab.assign(G.n, 0); nxt.assign(G.n, -1); prv.assign(G.n, -1); head.assign(G.n + 2, -1); tail.assign(G.n + 2, -1);
    dout.assign(G.n, 0); din.assign(G.n, 0); st.assign(G.n, 0);
    for (int i = 0; i < G.n; i++) {
      int v = ci.order[i];
      pushBack(v, K[v]);
      for (int y : G.adj[v]) if (ci.pos[y] > i) dout[v]++;
    }
  }
  void setK(int x, int v) {
    hist[K[x]]--; K[x] = v; hist[v]++;
    if (v > maxK) maxK = v;
    while (maxK > 0 && hist[maxK] == 0) maxK--;
  }
  bool before(int x, int y) const { return K[x] != K[y] ? K[x] < K[y] : lab[x] < lab[y]; }
  void unlink(int x) {
    int c = K[x];
    (prv[x] >= 0 ? nxt[prv[x]] : head[c]) = nxt[x];
    (nxt[x] >= 0 ? prv[nxt[x]] : tail[c]) = prv[x];
    nxt[x] = prv[x] = -1;
  }
  void pushBack(int x, int c) {
    lab[x] = tail[c] >= 0 ? lab[tail[c]] + GAP : 0;
    prv[x] = tail[c]; nxt[x] = -1;
    (tail[c] >= 0 ? nxt[tail[c]] : head[c]) = x;
    tail[c] = x;
  }
  void pushFront(int x, int c) {
    lab[x] = head[c] >= 0 ? lab[head[c]] - GAP : 0;
    nxt[x] = head[c]; prv[x] = -1;
    (head[c] >= 0 ? prv[head[c]] : tail[c]) = x;
    head[c] = x;
  }
  void placeAfter(int a, int x) {  // x joins a's level right after a
    int c = K[a];
    if (nxt[a] < 0) { pushBack(x, c); return; }
    if (lab[nxt[a]] - lab[a] < 2) {  // spread the smallest sparse-enough window after a
      vector<int> win;
      int z = nxt[a];
      while (z >= 0 && (win.empty() || (__int128)(lab[z] - lab[a]) <= (__int128)4 * (win.size() + 1) * (win.size() + 1))) { win.push_back(z); z = nxt[z]; }
      __int128 span = z >= 0 ? (__int128)(lab[z] - lab[a]) : (__int128)GAP * (win.size() + 1);
      for (size_t i = 0; i < win.size(); i++) lab[win[i]] = lab[a] + (long long)(span * (i + 1) / (win.size() + 1));
      relabels += win.size();
    }
    int b = nxt[a];
    lab[x] = lab[a] + (lab[b] - lab[a]) / 2;
    prv[x] = a; nxt[x] = b; nxt[a] = x; prv[b] = x;
  }
  void afterInsert(int u, int v) {  // edge already inserted
    auto t0 = Clock::now();
    if (ordered) insertOrdered(u, v); else insertTraversal(u, v);
    secs += ::secs(t0);
  }
  void insertOrdered(int u, int v) {
    if (!before(u, v)) swap(u, v);
    int c = K[u];
    if (++dout[u] <= c) return;
    priority_queue<pair<long long, int>, vector<pair<long long, int>>, greater<>> pq;
    vector<int> touched{u}, cand, R;
    vector<pair<int, int>> moves;  // (anchor, x): x is re-placed right after anchor
    st[u] = 1; pq.push({lab[u], u});
    while (!pq.empty()) {
      int w = pq.top().second; pq.pop();
      visited++;
      if (din[w] + dout[w] > c) {  // forward: w may rise to c + 1
        st[w] = 2; cand.push_back(w);
        for (int y : G.adj[w]) if (K[y] == c && lab[y] > lab[w]) {
          din[y]++;
          if (!st[y]) { st[y] = 1; touched.push_back(y); pq.push({lab[y], y}); }
        }
        continue;
      }
      st[w] = 3;  // w stays at c; candidates before it will end up after it
      if (!din[w]) continue;
      dout[w] += din[w]; din[w] = 0;
      R.clear();
      for (int y : G.adj[w]) if (st[y] == 2 && --dout[y] + din[y] <= c) { st[y] = 4; R.push_back(y); }
      int pre = w;
      for (size_t i = 0; i < R.size(); i++) {  // removed candidates are chained after w
        int x = R[i];
        st[x] = 3;
        for (int y : G.adj[x]) {
          if (K[y] != c) continue;
          if (st[y] == 2 || st[y] == 4) {
            if (lab[y] < lab[x]) dout[y]--; else din[y]--;
            if (st[y] == 2 && din[y] + dout[y] <= c) { st[y] = 4; R.push_back(y); }
          } else if (st[y] == 1) din[y]--;
        }
        dout[x] += din[x]; din[x] = 0;
        moves.push_back({pre, x}); pre = x;
      }
    }
    for (auto &m : moves) unlink(m.second);
    for (int x : cand) if (st[x] == 2) unlink(x);
    for (auto &m : moves) placeAfter(m.first, m.second);
    for (int i = (int)cand.size() - 1; i >= 0; i--) {
      int x = cand[i];
      if (st[x] != 2) continue;
      din[x] = 0; setK(x, c + 1); pushFront(x, c + 1);
    }
    for (int x : touched) st[x] = 0, din[x] = 0;
  }
  int mcd(int x, int r) const { int c = 0; for (int y : G.adj[x]) if (K[y] >= r) c++; return c; }
  void insertTraversal(int u, int v) {
    int r = min(K[u], K[v]);
    ++stamp;
    vector<int> q, sc;
    for (int x : {u, v}) if (K[x] == r && mark[x] != stamp) { mark[x] = stamp; q.push_back(x); }
    for (size_t i = 0; i < q.size(); i++) {  // purecore: expand through vertices with mcd > r
      int x = q[i];
      if (mcd(x, r) <= r) continue;
      sc.push_back(x);
      for (int y : G.adj[x]) if (K[y] == r && mark[y] != stamp) { mark[y] = stamp; q.push_back(y); }
    }
    visited += q.size();
    ++stamp;
    for (int x : sc) mark[x] = stamp;
    for (int x : sc) { int c = 0; for (int y : G.adj[x]) if (K[y] > r || (K[y] == r && mark[y] == stamp)) c++; cd[x] = c; }
    vector<int> stk;
    for (int x : sc) if (cd[x] <= r) { mark[x] = 0; stk.push_back(x); }
    while (!stk.empty()) {
      int x = stk.back(); stk.pop_back();
      for (int y : G.adj[x]) if (K[y] == r && mark[y] == stamp && --cd[y] <= r) { mark[y] = 0; stk.push_back(y); }
    }
    for (int x : sc) if (mark[x] == stamp) setK(x, r + 1);
  }
  void afterDelete(int u, int v) {  // edge already deleted
    auto t0 = Clock::now();
    int r = min(K[u], K[v]);
    if (ordered) dout[before(u, v) ? u : v]--;
    if (r == 0) return;
    ++stamp;
    vector<int> stk, dl;
    vector<long long> oldLab;
    auto drop = [&](int x) {
      dropped[x] = stamp;
      if (ordered) { dl.push_back(x); oldLab.push_back(lab[x]); unlink(x); }
      setK(x, r - 1); stk.push_back(x);
      // neighbours still at level >= r all end up after x at the tail of level r - 1 or above
      if (ordered) dout[x] = mcd(x, r);
    };
    // a fresh count also includes dropped neighbours whose decrement is still pending
    auto check = [&](int x) {
      if (dropped[x] == stamp) return;
      if (mark[x] != stamp) {
        mark[x] = stamp; visited++;
        int c = 0; for (int y : G.adj[x]) if (K[y] >= r || (dropped[y] == stamp && proc[y] != stamp)) c++;
        cd[x] = c;
      }
      if (cd[x] < r) drop(x);
    };
    for (int x : {u, v}) if (K[x] == r) check(x);
    while (!stk.empty()) {
      int x = stk.back(); stk.pop_back();
      proc[x] = stamp;
      for (int y : G.adj[x]) if (K[y] == r && dropped[y] != stamp) {
        if (mark[y] == stamp) { if (--cd[y] < r) drop(y); }
        else check(y);
      }
    }
    for (size_t i = 0; i < dl.size(); i++) {  // a level-r neighbour that preceded x loses it as a later neighbour
      int x = dl[i];
      for (int y : G.adj[x]) if (K[y] == r && lab[y] < oldLab[i]) dout[y]--;
      pushBack(x, r - 1);
    }
    secs += ::secs(t0);
  }
  // test hook: k-order invariants (levels sorted by label, K constant per level, dout exact and <= K)
  bool checkOrder() const {
    if (!ordered) return true;
    vector<long long> rank(G.n, -1);
    long long t = 0;
    for (int c = 0; c < (int)head.size(); c++) {
      long long last = LLONG_MIN;
      for (int x = head[c]; x >= 0; x = nxt[x]) { if (K[x] != c || lab[x] <= last) return false; last = lab[x]; rank[x] = t++; }
    }
    if (t != G.n) return false;
    for (int x = 0; x < G.n; x++) {
      int c = 0; for (int y : G.adj[x]) if (rank[y] > rank[x]) c++;
      if (c != dout[x] || dout[x] > K[x] || din[x] != 0) return false;
    }
    return true;
  }
};

struct DynKPlex {
  DynGraph &G;
  int k;
  vector<int> best;
  vector<char> inBest;
  vector<int> cntU, cntV, touched;
  DynStats ds;
  DynKPlex(DynGraph &g, int k_) : G(g), k(k_), inBest(g.n, 0), cntU(g.n, 0), cntV(g.n, 0) {}
  // core upper bound: core numbers from the last decomposition plus insertions since then
  // (an insertion raises any core number by at most one; deletions never raise it).
  bool useCoreUB = getenv("KPLEX_CACHE") != nullptr;
  vector<int> coreB;
  long long insSince = 0;
  // with KPLEX_CORE, exact core numbers are maintained incrementally instead
  bool useCoreMaint = getenv("KPLEX_CORE") != nullptr;
  CoreMaint *cmt = nullptr;
  const CoreMaint *core() const { return cmt; } int cappedM() const { return 0; }
  int ub(int x) const {
    if (cmt) return cmt->K[x];
    return useCoreUB && !coreB.empty() ? (int)min<long long>(G.deg(x), coreB[x] + insSince) : G.deg(x);
  }
  void setBest(const vector<int> &b) { for (int x : best) inBest[x] = 0; best = b; for (int x : best) inBest[x] = 1; }
  int omega() const { return (int)best.size(); }
  // Time budget (KPLEX_BUDGET = c, default 1, with an external engine only): the local searches of
  // one update may spend at most c times the wall time of the last external solver call (export,
  // load, solve); past that, the update falls back to one exact external solve restricted to the
  // vertices with core >= t-k, which contain every k-plex of size >= t. KPLEX_NOBUDGET disables it.
  double stEst = 0;
  bool useBudget = extEngine() && !getenv("KPLEX_NOBUDGET");
  double budgetC = getenv("KPLEX_BUDGET") ? atof(getenv("KPLEX_BUDGET")) : 1.0;
  void arm() { if (useBudget && stEst > 0) g_deadline = Clock::now() + chrono::duration_cast<Clock::duration>(chrono::duration<double>(budgetC * stEst)); }
  static void disarm() { g_deadline = Clock::time_point::max(); }
  vector<int> fallbackSolve(int t) {  // a maximum k-plex of G[{core >= t-k}]; its size is >= t iff omega_k(G) >= t
    disarm();
    ds.fallbacks++;
    g_lastExtWall = 0;
    auto tf = Clock::now();
    vector<int> b = solveOnCore(t);
    ds.curFb += secs(tf);
    if (g_lastExtWall > 0) stEst = g_lastExtWall;
    return b;
  }
  double budgetMs() const { return useBudget ? budgetC * stEst * 1e3 : 0; }
  // review instrumentation (see the header): region ablation, alert threshold, approximate mode
  int regionMode = !getenv("KPLEX_REGION") ? 0 : string(getenv("KPLEX_REGION")) == "twohop" ? 1 : 2;
  bool nonAdj = getenv("KPLEX_NONADJ") != nullptr, recordX = false;
  int thresh = 0;
  bool approx = getenv("KPLEX_APPROX") != nullptr;
  int threshold() const { return thresh; }
  void dropCache() { cacheT = -1; insLog.clear(); }
  void init() {
    StaticStats st; setBest(solveExact(G, k, true, st));
    if (extEngine()) stEst = g_lastExtWall;
    if (useCoreMaint) { cmt = new CoreMaint(G); cmt->init(); }
    else if (useCoreUB) { coreB = coreDecomp(G, k).core; insSince = 0; }
    if (getenv("KPLEX_THRESH_REL")) thresh = max(1, omega() - atoi(getenv("KPLEX_THRESH_REL")));
    if (useCache && useWarm) warmCache();
  }

  // Vertices that may share a k-plex of size >= t with both u and v (edge (u,v) present).
  // Returns false if the pair bounds already rule out such a plex. ids starts with u, v.
  bool pairCandidates(int u, int v, int t, vector<int> &ids) {
    bool ok = pairCandidates0(u, v, t, ids);
    if (recordX) ds.xsizes.push_back((int)ids.size());
    return ok;
  }
  bool pairCandidates0(int u, int v, int t, vector<int> &ids) {
    int need = t - k;
    ids = {u, v};
    if (ub(u) < need || ub(v) < need) return false;
    if (regionMode == 2 && k > 1) {  // ablation: the whole (t-k)-core
      for (int x = 0; x < G.n; x++) if (x != u && x != v && ub(x) >= need) ids.push_back(x);
      return (int)ids.size() >= t;
    }
    auto heavy = [&](int x) { return ub(x) >= need; };
    if (k == 1) {  // a clique containing u and v lies in their common neighbourhood
      const auto &a = G.adj[u], &b = G.adj[v];
      size_t i = 0, j = 0;
      while (i < a.size() && j < b.size()) { if (a[i] < b[j]) i++; else if (a[i] > b[j]) j++; else { if (heavy(a[i])) ids.push_back(a[i]); i++; j++; } }
      return (int)ids.size() >= t;
    }
    if (t >= 2 * k - 1) {
      {
        const auto &a = G.adj[u], &b = G.adj[v];
        int c = 0; size_t i = 0, j = 0;
        while (i < a.size() && j < b.size()) { if (a[i] < b[j]) i++; else if (a[i] > b[j]) j++; else { if (heavy(a[i])) c++; i++; j++; } }
        if (c < t - 2 * k) return false;
      }
      for (int x : G.adj[u]) if (heavy(x)) for (int y : G.adj[x]) if (heavy(y)) { if (cntU[y]++ == 0 && cntV[y] == 0) touched.push_back(y); }
      for (int x : G.adj[v]) if (heavy(x)) for (int y : G.adj[x]) if (heavy(y)) { if (cntV[y]++ == 0 && cntU[y] == 0) touched.push_back(y); }
      for (int x : G.adj[u]) if (heavy(x) && cntU[x] == 0 && cntV[x] == 0) touched.push_back(x);
      for (int x : G.adj[v]) if (heavy(x) && cntU[x] == 0 && cntV[x] == 0) touched.push_back(x);
      sort(touched.begin(), touched.end());
      touched.erase(unique(touched.begin(), touched.end()), touched.end());
      for (int y : touched) {
        if (y == u || y == v) continue;
        bool au = G.has(u, y), av = G.has(v, y);
        bool ok = regionMode == 1 ? (au || cntU[y] > 0) && (av || cntV[y] > 0)  // ablation: within two hops of both
                                  : cntU[y] >= (au ? t - 2 * k : t - 2 * k + 2) && cntV[y] >= (av ? t - 2 * k : t - 2 * k + 2);
        if (ok && nonAdj && k == 2 && !au && !av) ok = false;  // y may miss at most k-1 = 1 of u, v
        if (ok) ids.push_back(y);
      }
      for (int y : touched) cntU[y] = cntV[y] = 0;
      touched.clear();
    } else {
      for (int x = 0; x < G.n; x++) if (x != u && x != v && heavy(x)) ids.push_back(x);
    }
    return (int)ids.size() >= t;
  }
  // Is there a k-plex of size t containing both u and v (edge (u,v) present)?
  bool seeded(int u, int v, int t, vector<int> &out, bool *ab = nullptr) {
    vector<int> ids;
    recordX = true;
    bool pass = pairCandidates(u, v, t, ids);
    recordX = false;
    if (!pass) return false;
    ds.insSeeded++;
    SubResult r = solveLocal(G, ids, 2, k, t, true);
    ds.nodes += r.nodes;
    if (r.found) out = r.plex;
    if (ab && r.aborted) *ab = true;
    return r.found;
  }

  // ---- certificate cache. For level cacheT, cleared[v]=1 certifies "no k-plex of size >= cacheT
  // has v as its first vertex in the fixed order pi". Deletions keep every certificate valid;
  // an insertion (u,w) voids only certificates of vertices that may share a plex with u and w.
  bool useCache = getenv("KPLEX_CACHE") != nullptr;
  int cacheT = -1;
  vector<int> piPos, pending;
  vector<char> cleared, pendInv;  // pendInv[v]: v is pending because an insertion invalidated it
  // Lazy invalidation (default; KPLEX_EAGERINV for the eager variant): insertions are only logged,
  // and the log is applied, on the current graph and at the cache level, before the next cache
  // search. Sound because a k-plex that is new since a certificate was issued contains both
  // endpoints of an edge inserted since then and still present (Theorem 1 on the current graph).
  bool lazyInv = getenv("KPLEX_EAGERINV") == nullptr;
  vector<pair<int, int>> insLog;
  bool reuseUp = getenv("KPLEX_NOREUSE") == nullptr;  // a certificate at level t is valid at t' > t
  void applyLog() {
    if (cacheT >= 0) for (auto [a, b] : insLog) if (cacheT >= 0 && G.has(a, b)) { ds.invPassed++; invalidate(a, b); }
    insLog.clear();
  }
  void buildCache(int t) {
    if (cmt) {
      // Dense region P = {core >= t-k}: degeneracy order of G[P] (cheap, P is small); the
      // remaining vertices precede P in arbitrary order (they start no plex of size t).
      vector<int> P, loc(G.n, -1);
      for (int x = 0; x < G.n; x++) if (cmt->K[x] >= t - k) { loc[x] = (int)P.size(); P.push_back(x); }
      DynGraph R((int)P.size());
      for (int i = 0; i < R.n; i++) for (int y : G.adj[P[i]]) if (loc[y] >= 0) R.adj[i].push_back(loc[y]);
      CoreInfo rc = coreDecomp(R, k);
      int base = G.n - R.n, c = 0;
      piPos.assign(G.n, 0);
      for (int x = 0; x < G.n; x++) if (loc[x] < 0) piPos[x] = c++;
      for (int i = 0; i < R.n; i++) piPos[P[i]] = base + rc.pos[i];
      cleared.assign(G.n, 1); pendInv.assign(G.n, 0); insLog.clear();
      pending.clear();
      for (int i = 0; i < R.n; i++) { int v = P[rc.order[i]]; cleared[v] = 0; pending.push_back(v); }
      cacheT = t; ds.cacheBuilds++;
      return;
    }
    CoreInfo ci = coreDecomp(G, k);
    piPos = ci.pos;
    if (useCoreUB) { coreB = ci.core; insSince = 0; }
    cleared.assign(G.n, 1); pendInv.assign(G.n, 0); insLog.clear();
    pending.clear();
    for (int i = 0; i < G.n; i++) { int v = ci.order[i]; if (ci.core[v] >= t - k) { cleared[v] = 0; pending.push_back(v); } }
    cacheT = t; ds.cacheBuilds++;
  }
  void invalidate(int u, int w) {
    if (cacheT < 0) return;
    if (cacheT < 2 * k - 1) { cacheT = -1; return; }
    vector<int> ids;
    pairCandidates(u, w, cacheT, ids);
    if ((int)ids.size() < cacheT) return;  // no plex of size cacheT can use the new edge
    for (int x : ids) if (cleared[x]) { cleared[x] = 0; pendInv[x] = 1; pending.push_back(x); ds.invalidated++; }
  }
  // Decision search for a plex of size >= t whose pi-first vertex is v (pi fixed by the cache).
  bool trySub(int v, int t, vector<int> &out, bool *ab = nullptr) {
    int need = t - k;
    vector<int> ids{v}, nb;
    if (ub(v) >= need) {
      for (int w : G.adj[v]) if (piPos[w] > piPos[v] && ub(w) >= need) { nb.push_back(w); cntV[w] = -1; }
      if ((int)nb.size() >= need) {
        if (k == 1) ids.insert(ids.end(), nb.begin(), nb.end());
        else if (t >= 2 * k - 1) {
          for (int w : nb) for (int y : G.adj[w]) if (piPos[y] > piPos[v] && ub(y) >= need) { if (cntU[y]++ == 0) touched.push_back(y); }
          for (int y : touched) { bool adj = cntV[y] == -1; if (adj ? cntU[y] >= t - 2 * k : cntU[y] >= t - 2 * k + 2) ids.push_back(y); }
          for (int w : nb) if (cntU[w] == 0 && t - 2 * k <= 0) ids.push_back(w);
          for (int y : touched) cntU[y] = 0;
          touched.clear();
        } else {
          for (int y = 0; y < G.n; y++) if (piPos[y] > piPos[v] && ub(y) >= need) ids.push_back(y);
        }
      }
      for (int w : nb) cntV[w] = 0;
    }
    if ((int)ids.size() < t) return false;
    ds.fwdSum += (long long)ids.size(); ds.fwdCnt++;
    ds.cacheSub++;
    if (pendInv[v]) ds.cacheSubInv++;
    SubResult r = solveLocal(G, ids, 1, k, t, true);
    ds.nodes += r.nodes;
    if (r.found) out = r.plex;
    if (ab && r.aborted) *ab = true;
    return r.found;
  }
  vector<int> globalFind(int t, bool *ab = nullptr) {
    applyLog();
    if (cacheT != t) {
      if (reuseUp && cacheT >= 2 * k - 1 && cacheT < t) { cacheT = t; ds.levelReuse++; }
      else buildCache(t);
    }
    vector<int> out;
    while (!pending.empty()) {
      int v = pending.back();
      if (cleared[v]) { pending.pop_back(); continue; }
      // the budget covers the whole sweep, not only the nodes of each search: a sweep of many
      // small subproblems never reaches the per-node deadline check of the search engine
      if (g_deadline != Clock::time_point::max() && Clock::now() > g_deadline) { if (ab) *ab = true; return {}; }
      bool a = false;
      if (trySub(v, t, out, &a)) return out;  // v stays pending: it may still start a plex
      if (a) { if (ab) *ab = true; return {}; }  // budget exceeded: v stays pending, uncertified
      cleared[v] = 1; pendInv[v] = 0;
      pending.pop_back();
    }
    return {};
  }
  // Warm-up (KPLEX_WARM): certify every vertex at level omega once, so that later hits only
  // revisit vertices that start a maximum plex or were invalidated by insertions.
  bool useWarm = getenv("KPLEX_WARM") != nullptr;
  void warmCache() {
    int t = omega();
    buildCache(t);
    vector<int> keep, out;
    for (int v : pending) { if (trySub(v, t, out)) keep.push_back(v); else cleared[v] = 1; }
    pending = keep;
    ds.warmWitness = (long long)keep.size();
  }
  void insertEdge(int u, int v) {
    auto t0 = Clock::now();
    if (!G.add(u, v)) return;
    ds.ins++;
    insSince++;
    if (cmt) cmt->afterInsert(u, v);
    vector<int> out;
    if (useCache) {
      if (!lazyInv) invalidate(u, v);
      else if (cacheT >= 0) { insLog.push_back({u, v}); if (insLog.size() > (1u << 20)) { cacheT = -1; insLog.clear(); } }
    }
    bool ab = false;
    if (thresh > 0) {  // alert mode: only a missing witness of size thresh needs a search
      if (omega() < thresh) {
        arm();
        if (seeded(u, v, thresh, out, &ab)) { setBest(out); ds.insGrow++; }
        else if (ab) { vector<int> b = fallbackSolve(thresh); if ((int)b.size() >= thresh) { setBest(b); ds.insGrow++; } }
        disarm();
      }
      ds.tIns += secs(t0);
      return;
    }
    arm();
    if (seeded(u, v, omega() + 1, out, &ab)) { setBest(out); ds.insGrow++; }
    else if (ab) { vector<int> b = fallbackSolve(omega() + 1); if ((int)b.size() > omega()) { setBest(b); ds.insGrow++; } }
    disarm();
    ds.tIns += secs(t0);
  }
  // BasicX: whole-graph re-solve by the official solver. KPLEX_EXT_CORESUB (BasicX+): pass only the
  // vertices with core >= t-k (exact with KPLEX_CORE), which contain every k-plex of size >= t.
  bool coreSub = getenv("KPLEX_EXT_CORESUB") != nullptr;
  vector<int> extSolveWhole() { StaticStats st; return solveExact(G, k, true, st); }
  vector<int> solveOnCore(int t) {
    vector<int> P, loc(G.n, -1);
    for (int x = 0; x < G.n; x++) if (ub(x) >= t - k) { loc[x] = (int)P.size(); P.push_back(x); }
    if ((int)P.size() < t) return {};
    DynGraph R((int)P.size());
    for (int i = 0; i < R.n; i++) for (int y : G.adj[P[i]]) if (loc[y] >= 0) { R.adj[i].push_back(loc[y]); if (loc[y] > i) R.m++; }
    StaticStats st; vector<int> b = solveExact(R, k, true, st);
    for (int &x : b) x = P[x];
    ds.coreSubN += R.n;
    return b;
  }
  bool useRepair = getenv("KPLEX_REPAIR") != nullptr;
  bool repair(int u, int v, bool *ab = nullptr) {
    int w = omega();
    vector<int> F;
    for (int x : best) if (x != u && x != v) F.push_back(x);
    // candidate x needs at least w-k neighbours in the final plex, of which at most 1 is outside F
    int need = w - k - 1;
    for (int f : F) for (int x : G.adj[f]) if (!inBest[x] || x == u || x == v) { if (cntU[x]++ == 0) touched.push_back(x); }
    vector<int> ids = F;
    for (int x : touched) if (cntU[x] >= need && ub(x) >= w - k) ids.push_back(x);
    if (need <= 0) { ids = F; for (int x = 0; x < G.n; x++) if (!inBest[x] || x == u || x == v) ids.push_back(x); }
    for (int x : touched) cntU[x] = 0;
    touched.clear();
    if ((int)ids.size() < w) return false;
    SubResult r = solveLocal(G, ids, (int)F.size(), k, w, true);
    ds.nodes += r.nodes;
    if (r.found) setBest(r.plex);
    if (ab && r.aborted) *ab = true;
    return r.found;
  }
  void deleteEdge(int u, int v) {
    auto t0 = Clock::now();
    if (!G.del(u, v)) return;
    ds.del++;
    if (cmt) cmt->afterDelete(u, v);
    if (inBest[u] && inBest[v]) {
      ds.delHit++;
      int w = omega(), du = 0, dv = 0;
      for (int x : best) { if (x != u && G.has(u, x)) du++; if (x != v && G.has(v, x)) dv++; }
      if ((w - du > k || w - dv > k) && thresh > 0 && (w - 1 >= thresh || w < thresh)) {
        // alert mode: S*\{u} is still a witness of size >= thresh, or there was none
        vector<int> b2; for (int x : best) if (x != u) b2.push_back(x);
        setBest(b2);
      } else if (w - du > k || w - dv > k) {
        // the old optimum broke; OPT is w or w-1. First try a local repair that keeps
        // S*\{u,v} and adds two vertices; then fall back to a global decision search.
        arm();
        bool ab = false;
        if (useRepair && repair(u, v, &ab)) { disarm(); ds.delRepairSwap++; ds.tDel += secs(t0); return; }
        auto tg = Clock::now();
        vector<int> b;
        if (approx) { disarm(); ab = false; }  // approximate mode: no global search, the answer shrinks
        else if (extGlobal()) { ds.delGlobal++; disarm(); ab = false; b = coreSub ? solveOnCore(w) : extSolveWhole(); if ((int)b.size() < w) b.clear(); ds.curExt += secs(tg); }
        else if (useCache) { ds.delGlobal++; b = ab ? vector<int>{} : globalFind(w, &ab); }
        else { ds.delGlobal++; disarm(); ab = false; StaticStats st; b = staticSolve(G, k, w - 1, true, st); ds.nodes += st.nodes; }
        disarm();
        if (ab) { b = fallbackSolve(w); if ((int)b.size() < w) b.clear(); }
        ds.tGlobal += secs(tg);
        if (!b.empty()) setBest(b);
        else {
          vector<int> b2; for (int x : best) if (x != u) b2.push_back(x);
          setBest(b2); ds.delShrink++;
        }
      } else ds.delRepairLocal++;
    }
    ds.tDel += secs(t0);
  }
};


// ---------------------------------------------------------------- LazyMC baseline (k = 1)
// Reimplementation of LazyMC (Sun et al., IEEE TKDE 34(4), 2022), Algorithms 2-7. The paper's
// exact search (PMC) is replaced by the branch-and-bound engine shared with our method, so the
// comparison isolates the dynamic strategy. Core numbers are maintained incrementally with the
// traversal (purecore) algorithm; the paper uses the order-based method of Zhang et al. (ICDE'17).
struct LazyMC {
  DynGraph &G;
  vector<int> best;
  vector<char> inBest;
  DynStats ds;
  CoreMaint cm;
  int maxCha = 0, mCapped = 0;
  struct EBE { int p, q; bool flg; };
  vector<EBE> EL;
  int vld = 0;
  LazyMC(DynGraph &g, int k) : G(g), inBest(g.n, 0), cm(g) {
    if (k != 1) { fprintf(stderr, "LazyMC is defined for k = 1 only\n"); exit(1); }
  }
  int omega() const { return (int)best.size(); }
  void setBest(const vector<int> &b) { for (int x : best) inBest[x] = 0; best = b; for (int x : best) inBest[x] = 1; }
  const CoreMaint *core() const { return &cm; } int cappedM() const { return mCapped; }
  double budgetMs() const { return 0; } int threshold() const { return 0; } void dropCache() {}
  vector<int> common(int u, int v) const {
    vector<int> c;
    const auto &a = G.adj[u], &b = G.adj[v];
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) { if (a[i] < b[j]) i++; else if (a[i] > b[j]) j++; else { if (a[i] != u && a[i] != v) c.push_back(a[i]); i++; j++; } }
    return c;
  }
  // size estimator (vertices with core >= cmin and >= phi neighbours inside the filtered set)
  // and colour estimator (greedy colours of the filtered set, degeneracy order)
  pair<int, int> est(const vector<int> &X, const vector<int> &core, int cmin, int phi) {
    vector<int> ids;
    for (size_t i = 0; i < X.size(); i++) if (core[i] >= cmin) ids.push_back(X[i]);
    if (ids.empty()) return {0, 0};
    LocalBB bb; int L = (int)ids.size(); bb.init(L, 1);
    buildLocalEdges(G, ids, bb);
    vector<uint64_t> alive(bb.W, 0);
    for (int i = 0; i < L; i++) LocalBB::set1(alive.data(), i);
    int cnt = 0;
    for (int i = 0; i < L; i++) if (LocalBB::pc(bb.row(i), alive.data(), bb.W) >= phi) cnt++;
    vector<int> order; vector<uint64_t> rem = alive;
    for (int s = 0; s < L; s++) {
      int md = INT_MAX, mv = -1;
      LocalBB::forEach(rem.data(), bb.W, [&](int x) { int d = LocalBB::pc(bb.row(x), rem.data(), bb.W); if (d < md) { md = d; mv = x; } });
      order.push_back(mv); LocalBB::clr(rem.data(), mv);
    }
    vector<int> col(L, -1); int ncol = 0;
    for (int i = L - 1; i >= 0; i--) {
      int x = order[i];
      vector<char> used(ncol + 1, 0);
      LocalBB::forEach(bb.row(x), bb.W, [&](int y) { if (col[y] >= 0) used[col[y]] = 1; });
      int c = 0; while (used[c]) c++;
      col[x] = c; ncol = max(ncol, c + 1);
    }
    return {cnt, ncol};
  }
  vector<int> coresOf(const vector<int> &X) const { vector<int> c; for (int x : X) c.push_back(cm.K[x]); return c; }
  vector<int> famF(int p, int q) const { vector<int> f{p, q}; for (int x : common(p, q)) f.push_back(x); return f; }
  // Theorem 7 test: may an insertion of (u,v) create a clique of size theta+1?
  bool mayGrow(int theta, const vector<int> &C, const vector<int> &preK, const vector<int> &F, const vector<int> &postK) {
    auto [q0, p0] = est(C, preK, theta - 1, theta - 2);
    auto [q1, p1] = est(F, postK, theta, theta);
    int Esize = min(q0 + 2, q1), Ecolor = min(p0 + 2, p1);
    return min(Esize, Ecolor) > theta;
  }
  // LazyMC+ (review request): the global refresh hands the official solver only the vertices with core
  // number >= t-1, which contain every clique of size >= t (cf. LazyMC's Theorem 4 restricted to the core)
  vector<int> refreshOnCore(int t, StaticStats &st) {
    vector<int> P, loc(G.n, -1);
    for (int x = 0; x < G.n; x++) if (cm.K[x] >= t - 1) { loc[x] = (int)P.size(); P.push_back(x); }
    if ((int)P.size() < t) return {};
    DynGraph R((int)P.size());
    for (int i = 0; i < R.n; i++) for (int y : G.adj[P[i]]) if (loc[y] >= 0) { R.adj[i].push_back(loc[y]); if (loc[y] > i) R.m++; }
    vector<int> b = solveExact(R, 1, true, st);
    for (int &x : b) x = P[x];
    ds.coreSubN += R.n;
    return b;
  }
  // threshold-based refresh on the subgraph induced by X: a clique larger than t, or empty
  vector<int> refreshOn(const vector<int> &X, int t) {
    vector<int> ids;
    for (int x : X) if (cm.K[x] >= t) ids.push_back(x);
    if ((int)ids.size() < t + 1) return {};
    SubResult r = solveLocal(G, ids, 0, 1, t + 1, false);
    ds.nodes += r.nodes;
    return r.found ? r.plex : vector<int>{};
  }
  void init() {
    StaticStats st;
    vector<int> c0 = solveExact(G, 1, true, st);
    int w = (int)c0.size();
    cm.init();
    // warm-up: the set M of all maximum cliques (capped)
    CoreInfo ci = coreDecomp(G, 1);
    // capped at 100000 cliques and, if KPLEX_LAZY_MCAP_S is set, at that many seconds
    vector<vector<int>> M;
    double mcap = getenv("KPLEX_LAZY_MCAP_S") ? atof(getenv("KPLEX_LAZY_MCAP_S")) : 1e18;
    auto tm = Clock::now();
    for (int i = 0; i < G.n && (long long)M.size() < 100000 && secs(tm) < mcap; i++) {
      int v = ci.order[i];
      if (ci.core[v] < w - 1) continue;
      vector<int> ids{v};
      for (int y : G.adj[v]) if (ci.pos[y] > i && ci.core[y] >= w - 1) ids.push_back(y);
      if ((int)ids.size() < w) continue;
      LocalBB bb; bb.init((int)ids.size(), 1); bb.t = w;
      vector<vector<int>> found; bb.all = &found;
      buildLocalEdges(G, ids, bb);
      vector<uint64_t> S(bb.W, 0), C(bb.W, 0);
      LocalBB::set1(S.data(), 0);
      for (int j = 1; j < (int)ids.size(); j++) LocalBB::set1(C.data(), j);
      bb.bb(S, C);
      for (auto &f : found) { vector<int> g; for (int x : f) g.push_back(ids[x]); M.push_back(g); }
    }
    bool capped = (long long)M.size() >= 100000 || secs(tm) >= mcap;
    if (M.empty()) M.push_back(c0);
    setBest(M[0]);
    EL.clear(); vld = w;
    if (capped) { maxCha = w; vld = -1; mCapped = 1; }  // M incomplete: assume rivals, buffer invalid (global refresh)
    else if (M.size() == 1) maxCha = w - 1;
    else {
      maxCha = w;
      for (size_t i = 1; i < M.size(); i++) EL.push_back({M[i][0], M[i][1], false});
    }
    ds.cacheBuilds = (long long)M.size();  // reported as |M|
  }
  void insertEdge(int u, int v) {
    auto t0 = Clock::now();
    if (G.has(u, v) || u == v) return;
    vector<int> C = common(u, v);
    vector<int> preK = coresOf(C);
    G.add(u, v);
    ds.ins++;
    cm.afterInsert(u, v);
    vector<int> F{u, v}; F.insert(F.end(), C.begin(), C.end());
    vector<int> postK = coresOf(F);
    // instInit
    vector<int> H = best;
    if (inBest[u] != inBest[v]) {
      int w = inBest[u] ? v : u;
      bool ok = true;
      for (int x : H) if (!G.has(w, x)) { ok = false; break; }
      if (ok) H.push_back(w);
    }
    int cmax = omega();
    if ((int)H.size() == cmax + 1) {
      setBest(H); maxCha = (int)H.size(); EL = {{u, v, false}}; vld = (int)H.size(); ds.insGrow++;
      ds.tIns += secs(t0); return;
    }
    if (mayGrow(maxCha, C, preK, F, postK)) maxCha++;  // lazy growth
    if (maxCha > (int)H.size()) {
      ds.insSeeded++;  // reported as insertion refreshes
      vector<int> c = refreshOn(F, (int)H.size());
      if (!c.empty()) {
        setBest(c); ds.insGrow++; EL = {{u, v, false}}; vld = (int)c.size(); maxCha = (int)c.size();
        ds.tIns += secs(t0); return;
      }
      maxCha = (int)H.size();  // adjust on fact
    }
    if (vld == maxCha && mayGrow(maxCha - 1, C, preK, F, postK)) EL.push_back({u, v, false});
    ds.cacheBuilds = max<long long>(ds.cacheBuilds, (long long)EL.size());
    ds.tIns += secs(t0);
  }
  bool inF(int x, int p, int q) const { return x == p || x == q || (G.has(x, p) && G.has(x, q)); }
  vector<int> localRfr(const vector<int> &H) {
    int h = (int)H.size();
    for (size_t i = 0; i < EL.size();) {
      auto e = EL[i];
      vector<int> F = famF(e.p, e.q);
      vector<int> c;
      bool run = true;
      if (e.flg) {
        auto [q1, p1] = est(F, coresOf(F), h, h);
        run = min(q1, p1) > h;
      }
      if (run && G.has(e.p, e.q)) c = refreshOn(F, h);
      if (!c.empty()) return c;
      EL.erase(EL.begin() + i);
    }
    return {};
  }
  void deleteEdge(int u, int v) {
    auto t0 = Clock::now();
    if (!G.has(u, v)) return;
    int ru = cm.K[u], rv = cm.K[v];
    G.del(u, v);
    ds.del++;
    if (min(ru, rv) >= maxCha - 1) {  // screen EB, round 1
      for (size_t i = 0; i < EL.size();) {
        auto &e = EL[i];
        if ((e.p == u && e.q == v) || (e.p == v && e.q == u)) { EL.erase(EL.begin() + i); continue; }
        if (!e.flg && inF(u, e.p, e.q) && inF(v, e.p, e.q)) e.flg = true;
        i++;
      }
    }
    cm.afterDelete(u, v);
    int cmax = omega();
    if (!(inBest[u] && inBest[v])) { ds.tDel += secs(t0); return; }
    ds.delHit++;
    // delInit
    vector<int> H;
    for (int w : {u, v}) {
      H.clear(); for (int x : best) if (x != w) H.push_back(x);
      int piv = H[0];
      for (int x : H) if (G.deg(x) < G.deg(piv)) piv = x;
      int add = -1;
      for (int p : G.adj[piv]) {
        if (inBest[p] || p == w) continue;
        bool ok = true;
        for (int x : H) if (x != piv && !G.has(p, x)) { ok = false; break; }
        if (ok) { add = p; break; }
      }
      if (add >= 0) { H.push_back(add); break; }
    }
    if ((int)H.size() == cmax) { setBest(H); ds.delRepairLocal++; ds.tDel += secs(t0); return; }
    setBest(H);
    if (cm.maxK + 1 < maxCha) maxCha = cm.maxK + 1;  // adjust on core bound
    if ((int)H.size() < maxCha) {
      auto tg = Clock::now();
      vector<int> c;
      if (maxCha == vld) { ds.delRepairSwap++; c = localRfr(H); }  // reported as local refreshes
      else {
        ds.delGlobal++;
        StaticStats st;
        if (extGlobal() && getenv("KPLEX_EXT_CORESUB")) c = refreshOnCore((int)H.size() + 1, st);  // LazyMC+: only the core that can hold a larger clique
        else if (extGlobal()) c = solveExact(G, 1, true, st);
        else { c = staticSolve(G, 1, (int)H.size(), false, st); ds.nodes += st.nodes; }
      }
      ds.tGlobal += secs(tg);
      ds.curExt += secs(tg);
      if ((int)c.size() > (int)H.size()) setBest(c);
    }
    if (omega() < maxCha) maxCha = omega();  // adjust on fact
    if (omega() < cmax) ds.delShrink++;
    ds.tDel += secs(t0);
  }
};

// ---------------------------------------------------------------- IO
struct Loaded { DynGraph G; vector<pair<int, int>> temporalOrder; vector<long long> temporalTime; };
static Loaded loadGraph(const string &path, bool keepOrder) {
  bool gz = path.size() > 3 && path.compare(path.size() - 3, 3, ".gz") == 0;  // read .gz without unpacking
  FILE *in = gz ? popen(("gzip -dc '" + path + "'").c_str(), "r") : fopen(path.c_str(), "r");
  if (!in) { cerr << "cannot open " << path << "\n"; exit(1); }
  unordered_map<long long, int> id;
  vector<pair<int, int>> edges;
  vector<pair<long long, pair<int, int>>> timed;
  char *buf = nullptr; size_t cap = 0;
  while (getline(&buf, &cap, in) > 0) {
    if (buf[0] == '#' || buf[0] == '%' || buf[0] == '\n') continue;
    long long a, b, ts = 0;
    if (sscanf(buf, "%lld %lld %lld", &a, &b, &ts) < 2) continue;
    if (a == b) continue;
    int x = id.emplace(a, (int)id.size()).first->second, y = id.emplace(b, (int)id.size()).first->second;
    if (keepOrder) timed.push_back({ts, {x, y}}); else edges.push_back({x, y});
  }
  free(buf);
  if (gz) pclose(in); else fclose(in);
  Loaded L{DynGraph((int)id.size()), {}, {}};
  if (keepOrder) {
    stable_sort(timed.begin(), timed.end(), [](auto &p, auto &q) { return p.first < q.first; });
    // every interaction, in time order (repeated pairs included: they refresh the edge in a window)
    for (auto &p : timed) {
      int x = min(p.second.first, p.second.second), y = max(p.second.first, p.second.second);
      L.temporalOrder.push_back({x, y});
      L.temporalTime.push_back(p.first);
    }
  } else {
    for (auto [x, y] : edges) {
      L.G.adj[x].push_back(y); L.G.adj[y].push_back(x);
    }
    long long m2 = 0;
    for (auto &a : L.G.adj) { sort(a.begin(), a.end()); a.erase(unique(a.begin(), a.end()), a.end()); m2 += a.size(); }
    L.G.m = m2 / 2;
  }
  return L;
}

static double pct(vector<double> v, double p) {
  if (v.empty()) return 0;
  sort(v.begin(), v.end());
  return v[min(v.size() - 1, (size_t)(p * (v.size() - 1)))];
}

// ---------------------------------------------------------------- modes
static int modeStatic(int argc, char **argv) {
  Loaded L = loadGraph(argv[2], false);
  int k = atoi(argv[3]);
  StaticStats st;
  vector<int> b = staticSolve(L.G, k, -1, false, st);
  printf("{\"graph\":\"%s\",\"n\":%d,\"m\":%lld,\"k\":%d,\"omega\":%zu,\"heur\":%d,\"kept\":%d,\"time\":%.4f,\"nodes\":%lld,\"sub\":%d,\"valid\":%d}\n", argv[2], L.G.n, L.G.m, k,
         b.size(), st.heurLb, st.keptVertices, st.time, st.nodes, st.subproblems, (int)isKPlex(L.G, b, k));
  return 0;
}

template <class Algo>
static int modeDynT(int argc, char **argv) {
  string path = argv[2], stream = argv[4];
  int k = atoi(argv[3]), Q = atoi(argv[5]);
  unsigned seed = atoi(argv[6]);
  int sampleEvery = atoi(argv[7]);
  double wfrac = argc > 8 ? atof(argv[8]) : 0.8;
  bool tinc = stream == "tinc";  // insertion-only replay in time order
  bool temporal = stream == "temporal" || tinc;
  Loaded L = loadGraph(path, temporal);
  DynGraph &G = L.G;
  mt19937_64 rng(seed);
  // update stream
  vector<pair<int, pair<int, int>>> ops;  // (+1 insert / -1 delete, edge)
  vector<long long> opTime;               // interaction timestamp of each op (temporal streams)
  size_t winStart = 0, winEnd = 0;
  if (temporal) {
    // Sliding window over interactions: an edge is present while at least one of its interactions
    // is in the window. Q window steps (one arrival, and for "temporal" one expiry, per step); only
    // steps that change the graph become updates.
    auto &E = L.temporalOrder;
    winEnd = (size_t)(E.size() * wfrac);
    if (getenv("KPLEX_WIN_SECONDS") && !tinc) {
      // time-based window of W seconds ending at the interaction at fraction wfrac of the history;
      // each of the Q steps admits the next interaction and expires every interaction older than W
      long long W = atoll(getenv("KPLEX_WIN_SECONDS"));
      auto &T = L.temporalTime;
      size_t s0 = winEnd;
      while (s0 > 0 && T[s0 - 1] > T[winEnd] - W) s0--;
      unordered_map<long long, int> mult;
      auto key = [&](const pair<int, int> &e) { return (long long)e.first * G.n + e.second; };
      for (size_t i = s0; i < winEnd; i++) if (mult[key(E[i])]++ == 0) G.add(E[i].first, E[i].second);
      size_t old = s0;
      for (int q = 0; q < Q && winEnd + q < E.size(); q++) {
        size_t i = winEnd + q;
        if (mult[key(E[i])]++ == 0) { ops.push_back({+1, E[i]}); opTime.push_back(T[i]); }
        while (old < i && T[old] <= T[i] - W) { if (--mult[key(E[old])] == 0) { ops.push_back({-1, E[old]}); opTime.push_back(T[i]); } old++; }
      }
      winStart = s0;
    } else {
      unordered_map<long long, int> mult;
      auto key = [&](const pair<int, int> &e) { return (long long)e.first * G.n + e.second; };
      for (size_t i = 0; i < winEnd; i++) if (mult[key(E[i])]++ == 0) G.add(E[i].first, E[i].second);
      for (int q = 0; q < Q && winEnd + q < E.size(); q++) {
        long long ts = L.temporalTime[winEnd + q];
        if (mult[key(E[winEnd + q])]++ == 0) { ops.push_back({+1, E[winEnd + q]}); opTime.push_back(ts); }
        if (!tinc && --mult[key(E[winStart + q])] == 0) { ops.push_back({-1, E[winStart + q]}); opTime.push_back(ts); }
      }
    }
  }
  auto tInit = Clock::now();
  Algo D(G, k);
  D.init();
  double initTime = secs(tInit);
  int omega0 = D.omega();
  // edge sampler for deletions (rand/hot): keep a list
  vector<pair<int, int>> elist;
  if (!temporal) { for (int u = 0; u < G.n; u++) for (int v : G.adj[u]) if (u < v) elist.push_back({u, v}); }
  // delins: LazyMC's protocol -- delete Q sampled edges one by one, then reinsert them shuffled
  bool replay = temporal;
  if (stream == "delins") {
    vector<pair<int, int>> pick = elist;
    shuffle(pick.begin(), pick.end(), rng);
    pick.resize(min<size_t>(Q, pick.size()));
    for (auto &e : pick) ops.push_back({-1, e});
    shuffle(pick.begin(), pick.end(), rng);
    for (auto &e : pick) ops.push_back({+1, e});
    replay = true;
  }
  vector<int> dense;  // for hot stream: vertices with core >= omega-k at start
  bool hotLike = stream == "hot" || stream == "hotrev" || stream == "takedown";
  vector<pair<int, int>> denseEdges;  // takedown: edges inside the initial dense region, fixed order
  size_t denseNext = 0;
  if (hotLike) {
    CoreInfo ci = coreDecomp(G, k);
    vector<char> inD(G.n, 0);
    for (int v = 0; v < G.n; v++) if (ci.core[v] >= omega0 - k) { dense.push_back(v); inD[v] = 1; }
    if (stream == "takedown") {  // densest part first: by the smaller core number of the endpoints, ties shuffled
      for (int a : dense) for (int b : G.adj[a]) if (a < b && inD[b]) denseEdges.push_back({a, b});
      shuffle(denseEdges.begin(), denseEdges.end(), rng);
      stable_sort(denseEdges.begin(), denseEdges.end(), [&](auto &x, auto &y) { return min(ci.core[x.first], ci.core[x.second]) > min(ci.core[y.first], ci.core[y.second]); });
    }
  }
  double insFrac = getenv("KPLEX_HOT_INSFRAC") ? atof(getenv("KPLEX_HOT_INSFRAC")) : 0.5;
  vector<pair<int, int>> deletedOpt;  // hotrev: deleted optimum edges, re-inserted later
  auto randEdgeExisting = [&](pair<int, int> &e) {
    for (int tries = 0; tries < 1000; tries++) {
      size_t i = rng() % elist.size();
      e = elist[i];
      if (G.has(e.first, e.second)) return true;
      elist[i] = elist.back(); elist.pop_back();
    }
    return false;
  };
  vector<double> lat;
  vector<double> stT, warmT, stW;
  vector<array<float, 4>> tailInfo;  // per update: fallback ms, external ms, budget ms, local search ms
  double gapSum = 0; int gapMax = 0; long long subopt = 0, alertBad = 0;
  double extWall0 = g_extWallSum, extOwn0 = g_extOwnSum, extSampleWall = 0, extSampleOwn = 0;
  FILE *traceF = getenv("KPLEX_TRACE") ? fopen(getenv("KPLEX_TRACE"), "w") : nullptr;
  FILE *latF = getenv("KPLEX_LATDUMP") ? fopen(getenv("KPLEX_LATDUMP"), "w") : nullptr;
  int piRebuild = getenv("KPLEX_PI_REBUILD") ? atoi(getenv("KPLEX_PI_REBUILD")) : 0;
  int winStats = getenv("KPLEX_WINSTATS") ? atoi(getenv("KPLEX_WINSTATS")) : 0;
  vector<double> winUs, winFwd; double winAcc = 0; long long winN = 0, fwdS0 = 0, fwdC0 = 0;
  long long mismatches = 0, invalid = 0, samples = 0;
  vector<char> isIns;
  const char *cv = getenv("KPLEX_COREVERIFY");
  int coreVerify = cv ? atoi(cv) : 0;
  long long coreChecks = 0, coreBad = 0;
  bool noWarm = getenv("KPLEX_NOWARM") != nullptr;  // skip the warm-start baseline at checkpoints
  auto runSample = [&](bool wasIns, int omegaBefore, bool bestStillValid) {
    double ew = g_extWallSum, eo = g_extOwnSum;
    StaticStats s1; int w1 = 0; solveExact(G, k, false, s1, &w1);
    extSampleWall += g_extWallSum - ew; extSampleOwn += g_extOwnSum - eo;
    stT.push_back(s1.time); stW.push_back(extEngine() ? g_lastExtWall : s1.time);
    if (D.threshold() > 0) { samples++; if ((w1 >= D.threshold()) != (D.omega() >= D.threshold())) alertBad++; return; }
    if (w1 > D.omega()) { gapSum += w1 - D.omega(); gapMax = max(gapMax, w1 - D.omega()); subopt++; }
    if (noWarm) { samples++; if (w1 != D.omega()) mismatches++; return; }
    // warm-start baseline: global search from the previous answer
    int lbw = wasIns ? omegaBefore : (bestStillValid ? omegaBefore : omegaBefore - 1);
    auto tw = Clock::now();
    StaticStats s2; vector<int> b2 = staticSolve(G, k, lbw, false, s2);
    warmT.push_back(secs(tw));
    samples++;
    if (w1 != D.omega()) mismatches++;
    int warmOmega = b2.empty() ? lbw : (int)b2.size();
    if (warmOmega != w1) mismatches += 1000;  // should never happen
  };
  // Run cap (KPLEX_RUN_CAP_S, default 1200 s): stop the stream early and report the updates processed.
  // Needed where the static engine itself becomes slow on the updated graph (each fallback or
  // BasicX re-solve then costs seconds); a truncated run gets one checkpoint at its end.
  double runCap = getenv("KPLEX_RUN_CAP_S") ? atof(getenv("KPLEX_RUN_CAP_S")) : 1200.0;
  auto tRun = Clock::now();
  int truncated = 0, lastTyp = 0, lastOmegaBefore = omega0;
  for (int q = 0; q < (replay ? (int)ops.size() : Q); q++) {
    if (runCap > 0 && secs(tRun) > runCap) { truncated = 1; break; }
    int typ; pair<int, int> e;
    if (replay) { typ = ops[q].first; e = ops[q].second; }
    else if (stream == "rand") {
      typ = (rng() & 1) ? +1 : -1;
      if (typ > 0) { do { e = {(int)(rng() % G.n), (int)(rng() % G.n)}; } while (e.first == e.second || G.has(e.first, e.second)); }
      else if (!randEdgeExisting(e)) continue;
    } else {  // hot, hotrev, takedown
      typ = insFrac == 0.5 ? ((rng() & 1) ? +1 : -1) : ((double)(rng() % 1000000) < insFrac * 1e6 ? +1 : -1);  // 0.5: as before
      if (typ > 0 && stream == "hotrev" && !deletedOpt.empty() && (rng() & 1)) {  // re-form the community
        size_t i = rng() % deletedOpt.size();
        e = deletedOpt[i]; deletedOpt[i] = deletedOpt.back(); deletedOpt.pop_back();
        if (G.has(e.first, e.second)) continue;
      } else if (typ < 0 && stream == "takedown" && (rng() & 1)) {  // next edge of the dense region
        while (denseNext < denseEdges.size() && !G.has(denseEdges[denseNext].first, denseEdges[denseNext].second)) denseNext++;
        if (denseNext == denseEdges.size()) continue;
        e = denseEdges[denseNext++];
      } else if (typ > 0) {
        int tries = 0;
        do {
          int a = dense[rng() % dense.size()];
          int x = G.adj[a].empty() ? a : G.adj[a][rng() % G.adj[a].size()];
          int b = G.adj[x].empty() ? x : G.adj[x][rng() % G.adj[x].size()];
          e = {a, b};
        } while ((e.first == e.second || G.has(e.first, e.second)) && ++tries < 1000);
        if (tries >= 1000) continue;
      } else {
        if (stream != "takedown" && (rng() % 2) == 0 && D.omega() >= 2) {  // adversarial: an edge inside the current optimum
          vector<pair<int, int>> in;
          for (size_t i = 0; i < D.best.size(); i++) for (size_t j = i + 1; j < D.best.size(); j++) if (G.has(D.best[i], D.best[j])) in.push_back({D.best[i], D.best[j]});
          if (in.empty()) continue;
          e = in[rng() % in.size()];
          if (stream == "hotrev") deletedOpt.push_back(e);
        } else {
          int a = dense[rng() % dense.size()];
          if (G.adj[a].empty()) continue;
          e = {a, G.adj[a][rng() % G.adj[a].size()]};
        }
      }
    }
    int omegaBefore = D.omega();
    lastTyp = typ; lastOmegaBefore = omegaBefore;
    if (piRebuild > 0 && q > 0 && q % piRebuild == 0) D.dropCache();
    D.ds.curFb = D.ds.curExt = 0;
    double budget = D.budgetMs(), search0 = g_searchSum;
    auto t0 = Clock::now();
    if (typ > 0) { D.insertEdge(e.first, e.second); if (!temporal) elist.push_back(e); }
    else D.deleteEdge(e.first, e.second);
    lat.push_back(secs(t0));
    tailInfo.push_back({(float)(D.ds.curFb * 1e3), (float)(D.ds.curExt * 1e3), (float)budget, (float)((g_searchSum - search0) * 1e3)});
    if (traceF) fprintf(traceF, "%d\n", D.omega());
    if (latF) fprintf(latF, "%lld %.3f\n", replay && q < (int)opTime.size() ? opTime[q] : (long long)q, lat.back() * 1e6);
    if (winStats > 0) {
      winAcc += lat.back(); winN++;
      if (winN == winStats) {
        winUs.push_back(winAcc / winN * 1e6);
        winFwd.push_back(D.ds.fwdCnt > fwdC0 ? (double)(D.ds.fwdSum - fwdS0) / (D.ds.fwdCnt - fwdC0) : 0);
        fwdS0 = D.ds.fwdSum; fwdC0 = D.ds.fwdCnt; winAcc = 0; winN = 0;
      }
    }
    if (!isKPlex(G, D.best, k)) invalid++;
    if (sampleEvery > 0 && q % sampleEvery == sampleEvery - 1) runSample(typ > 0, omegaBefore, D.omega() == omegaBefore);
    if (coreVerify > 0 && D.core() && q % coreVerify == coreVerify - 1) { coreChecks++; if (coreDecomp(G, 1).core != D.core()->K) coreBad++; }
  }
  if (truncated && samples == 0 && !lat.empty()) runSample(lastTyp > 0, lastOmegaBefore, D.omega() == lastOmegaBefore);
  if (coreVerify > 0 && D.core()) { coreChecks++; if (coreDecomp(G, 1).core != D.core()->K || !D.core()->checkOrder()) coreBad++; }
  if (coreVerify > 0) fprintf(stderr, "core verify: %lld checks, %lld bad\n", coreChecks, coreBad);
  if (traceF) fclose(traceF);
  if (latF) fclose(latF);
  // review instrumentation: insertion-region sizes, the 10 slowest updates, external-call time
  string extra;
  {
    char b[512];
    vector<double> xs(D.ds.xsizes.begin(), D.ds.xsizes.end());
    double xm = xs.empty() ? 0 : accumulate(xs.begin(), xs.end(), 0.0) / xs.size();
    snprintf(b, sizeof b, ",\"x_n\":%zu,\"x_mean\":%.1f,\"x_p50\":%.0f,\"x_p99\":%.0f,\"x_max\":%.0f,\"fwd_mean\":%.1f",
             xs.size(), xm, pct(xs, 0.5), pct(xs, 0.99), xs.empty() ? 0 : *max_element(xs.begin(), xs.end()),
             D.ds.fwdCnt ? (double)D.ds.fwdSum / D.ds.fwdCnt : 0);
    extra += b;
    snprintf(b, sizeof b, ",\"ext_wall_s\":%.4f,\"ext_own_s\":%.4f,\"gap_sum\":%.0f,\"gap_max\":%d,\"subopt\":%lld,\"thresh\":%d,\"alert_bad\":%lld",
             g_extWallSum - extWall0 - extSampleWall, g_extOwnSum - extOwn0 - extSampleOwn, gapSum, gapMax, subopt, D.threshold(), alertBad);
    extra += b;
    vector<int> idx(lat.size()); iota(idx.begin(), idx.end(), 0);
    int top = min<int>(10, (int)idx.size());
    partial_sort(idx.begin(), idx.begin() + top, idx.end(), [&](int a, int c) { return lat[a] > lat[c]; });
    extra += ",\"tail10\":[";
    for (int i = 0; i < top; i++) {
      snprintf(b, sizeof b, "%s[%.2f,%.2f,%.2f,%.2f,%.2f]", i ? "," : "", lat[idx[i]] * 1e3, tailInfo[idx[i]][3], tailInfo[idx[i]][0], tailInfo[idx[i]][1], tailInfo[idx[i]][2]);
      extra += b;
    }
    extra += "]";
    if (winStats > 0) {
      extra += ",\"win_us\":[";
      for (size_t i = 0; i < winUs.size(); i++) { snprintf(b, sizeof b, "%s%.2f", i ? "," : "", winUs[i]); extra += b; }
      extra += "],\"win_fwd\":[";
      for (size_t i = 0; i < winFwd.size(); i++) { snprintf(b, sizeof b, "%s%.1f", i ? "," : "", winFwd[i]); extra += b; }
      extra += "]";
    }
    if (getenv("KPLEX_TAG")) { snprintf(b, sizeof b, ",\"tag\":\"%s\"", getenv("KPLEX_TAG")); extra += b; }
  }
  double dynTotal = accumulate(lat.begin(), lat.end(), 0.0);
  double stAvg = stT.empty() ? 0 : accumulate(stT.begin(), stT.end(), 0.0) / stT.size();
  double stWall = stW.empty() ? 0 : accumulate(stW.begin(), stW.end(), 0.0) / stW.size();
  double warmAvg = warmT.empty() ? 0 : accumulate(warmT.begin(), warmT.end(), 0.0) / warmT.size();
  double dynAvg = lat.empty() ? 0 : dynTotal / lat.size();
  auto &s = D.ds;
  const CoreMaint *cm = D.core();
  printf("{\"graph\":\"%s\",\"n\":%d,\"m\":%lld,\"k\":%d,\"stream\":\"%s\",\"updates\":%zu,\"omega0\":%d,\"omega_end\":%d,\"init_s\":%.4f,"
         "\"dyn_total_s\":%.4f,\"dyn_avg_us\":%.2f,\"dyn_p50_us\":%.2f,\"dyn_p99_us\":%.2f,\"dyn_max_ms\":%.3f,"
         "\"ins\":%lld,\"ins_seeded\":%lld,\"ins_grow\":%lld,\"del\":%lld,\"del_hit\":%lld,\"del_local_ok\":%lld,\"del_swap\":%lld,\"del_global\":%lld,\"del_shrink\":%lld,\"cache_builds\":%lld,\"cache_sub\":%lld,\"invalidated\":%lld,\"warm_witness\":%lld,"
         "\"t_ins_s\":%.4f,\"t_del_s\":%.4f,\"t_global_s\":%.4f,\"samples\":%lld,\"st_avg_ms\":%.3f,\"warm_avg_ms\":%.3f,"
         "\"speedup_vs_st\":%.1f,\"speedup_vs_warm\":%.1f,\"mismatch\":%lld,\"invalid\":%lld,"
         "\"core_alg\":\"%s\",\"core_s\":%.4f,\"core_visited\":%lld,\"core_relabels\":%lld,\"static_engine\":\"%s\","
         "\"seed\":%u,\"st_wall_ms\":%.3f,\"cache_sub_inv\":%lld,\"level_reuse\":%lld,\"fallbacks\":%lld,\"inv_passes\":%lld,\"m_capped\":%d,\"dyn_p999_us\":%.2f,\"truncated\":%d,\"run_s\":%.1f,\"coresub_n\":%lld%s}\n",
         path.c_str(), G.n, G.m, k, stream.c_str(), lat.size(), omega0, D.omega(), initTime, dynTotal, dynAvg * 1e6, pct(lat, 0.5) * 1e6, pct(lat, 0.99) * 1e6,
         lat.empty() ? 0 : *max_element(lat.begin(), lat.end()) * 1e3, s.ins, s.insSeeded, s.insGrow, s.del, s.delHit, s.delRepairLocal, s.delRepairSwap, s.delGlobal, s.delShrink, s.cacheBuilds, s.cacheSub, s.invalidated, s.warmWitness,
         s.tIns, s.tDel, s.tGlobal, samples, stAvg * 1e3, warmAvg * 1e3, dynAvg > 0 ? stAvg / dynAvg : 0, dynAvg > 0 ? warmAvg / dynAvg : 0, mismatches, invalid,
         cm ? (cm->ordered ? "order" : "trav") : "none", cm ? cm->secs : 0.0, cm ? cm->visited : 0LL, cm ? cm->relabels : 0LL, extEngine() ? extEngine() : "ours",
         seed, stWall * 1e3, s.cacheSubInv, s.levelReuse, s.fallbacks, s.invPassed, D.cappedM(), pct(lat, 0.999) * 1e6, truncated, secs(tRun), s.coreSubN, extra.c_str());
  return 0;
}

static int modeDyn(int argc, char **argv) {
  const char *a = getenv("KPLEX_ALGO");
  if (a && string(a) == "lazymc") return modeDynT<LazyMC>(argc, argv);
  return modeDynT<DynKPlex>(argc, argv);
}

static int bruteOmega(const DynGraph &G, int k) {
  int n = G.n, best = 0;
  vector<uint32_t> nb(n, 0);
  for (int u = 0; u < n; u++) for (int v : G.adj[u]) nb[u] |= 1u << v;
  for (uint32_t s = 1; s < (1u << n); s++) {
    int sz = __builtin_popcount(s);
    if (sz <= best) continue;
    bool ok = true;
    for (int u = 0; u < n && ok; u++) if (s >> u & 1) if (sz - __builtin_popcount(nb[u] & s) > k) ok = false;
    if (ok) best = sz;
  }
  return best;
}
static int modeBrute(int argc, char **argv) {
  int trials = atoi(argv[2]);
  mt19937 rng(atoi(argv[3]));
  long long checks = 0, bad = 0;
  for (int tr = 0; tr < trials; tr++) {
    int n = 8 + rng() % 9, k = 1 + rng() % 3;
    double p = 0.2 + (rng() % 60) / 100.0;
    DynGraph G(n);
    for (int u = 0; u < n; u++) for (int v = u + 1; v < n; v++) if ((rng() % 1000) / 1000.0 < p) G.add(u, v);
    StaticStats st;
    vector<int> s0 = staticSolve(G, k, -1, false, st);
    checks++;
    if ((int)s0.size() != bruteOmega(G, k) || !isKPlex(G, s0, k)) { bad++; fprintf(stderr, "static mismatch n=%d k=%d got %zu want %d\n", n, k, s0.size(), bruteOmega(G, k)); }
    DynGraph G2 = G;  // LazyMC (k = 1) runs on its own copy with the same updates
    LazyMC *Z = nullptr;
    if (k == 1) { Z = new LazyMC(G2, 1); Z->init(); }
    DynKPlex D(G, k);
    D.useRepair = tr % 2;
    D.useCache = (tr / 2) % 2;
    D.useCoreMaint = (tr / 4) % 2;
    D.useWarm = (tr / 8) % 2;
    D.init();
    for (int q = 0; q < 60; q++) {
      int u = rng() % n, v = rng() % n;
      if (u == v) continue;
      bool del = G.has(u, v);
      if (del) D.deleteEdge(u, v); else D.insertEdge(u, v);
      checks++;
      int want = bruteOmega(G, k);
      if (D.omega() != want || !isKPlex(G, D.best, k)) { bad++; fprintf(stderr, "dyn mismatch n=%d k=%d got %d want %d\n", n, k, D.omega(), want); }
      if (Z) {
        if (del) Z->deleteEdge(u, v); else Z->insertEdge(u, v);
        checks++;
        vector<int> kc = coreDecomp(G2, 1).core;
        if (Z->omega() != want || !isKPlex(G2, Z->best, 1)) { bad++; fprintf(stderr, "lazymc mismatch n=%d got %d want %d\n", n, Z->omega(), want); }
        if (kc != Z->cm.K) { bad++; fprintf(stderr, "core maintenance mismatch n=%d\n", n); }
      }
    }
    delete Z;
  }
  printf("brute checks=%lld bad=%lld\n", checks, bad);
  return bad != 0;
}

// core maintenance alone on a real graph: kplex corebench <graph> <rand|delins> <Q> <seed> <cap_s>
// (KPLEX_COREALG selects the method); stops after cap_s seconds of maintenance time
static int modeCoreBench(int argc, char **argv) {
  string path = argv[2], stream = argv[3];
  int Q = atoi(argv[4]);
  mt19937_64 rng(atoi(argv[5]));
  double cap = atof(argv[6]);
  auto tl = Clock::now();
  Loaded L = loadGraph(path, false);
  DynGraph &G = L.G;
  double loadS = secs(tl);
  auto ti = Clock::now();
  CoreMaint cm(G);
  cm.init();
  double initS = secs(ti);
  vector<pair<int, int>> elist;
  for (int u = 0; u < G.n; u++) for (int v : G.adj[u]) if (u < v) elist.push_back({u, v});
  vector<pair<int, pair<int, int>>> ops;
  if (stream == "delins") {
    shuffle(elist.begin(), elist.end(), rng);
    vector<pair<int, int>> pick(elist.begin(), elist.begin() + min<size_t>(Q, elist.size()));
    for (auto &e : pick) ops.push_back({-1, e});
    shuffle(pick.begin(), pick.end(), rng);
    for (auto &e : pick) ops.push_back({+1, e});
  } else {
    for (int q = 0; q < Q; q++) {
      if (rng() & 1) { int a = rng() % G.n, b = rng() % G.n; if (a != b) ops.push_back({+1, {a, b}}); }
      else ops.push_back({-1, elist[rng() % elist.size()]});
    }
  }
  size_t done = 0;
  vector<double> lat;
  for (auto &o : ops) {
    int u = o.second.first, v = o.second.second;
    auto t0 = Clock::now();
    if (o.first > 0) { if (!G.add(u, v)) continue; cm.afterInsert(u, v); }
    else { if (!G.del(u, v)) continue; cm.afterDelete(u, v); }
    lat.push_back(secs(t0));
    done++;
    if (cm.secs > cap) break;
  }
  bool ok = coreDecomp(G, 1).core == cm.K && cm.checkOrder();
  printf("{\"graph\":\"%s\",\"n\":%d,\"m\":%lld,\"stream\":\"%s\",\"core_alg\":\"%s\",\"ops_planned\":%zu,\"ops_done\":%zu,\"load_s\":%.2f,\"init_s\":%.3f,"
         "\"core_s\":%.4f,\"avg_us\":%.3f,\"p99_us\":%.2f,\"max_ms\":%.3f,\"visited\":%lld,\"relabels\":%lld,\"maxK\":%d,\"correct\":%d}\n",
         path.c_str(), G.n, G.m, stream.c_str(), cm.ordered ? "order" : "trav", ops.size(), done, loadS, initS, cm.secs,
         lat.empty() ? 0 : cm.secs / lat.size() * 1e6, pct(lat, 0.99) * 1e6, lat.empty() ? 0 : *max_element(lat.begin(), lat.end()) * 1e3, cm.visited, cm.relabels, cm.maxK, ok);
  return ok ? 0 : 1;
}

// core maintenance alone: random graphs and updates, checked against recomputation and the k-order invariants
static int modeCoreBrute(int argc, char **argv) {
  int trials = atoi(argv[2]);
  mt19937 rng(atoi(argv[3]));
  long long checks = 0, bad = 0;
  for (int tr = 0; tr < trials; tr++) {
    int n = 5 + rng() % 60;
    double p = (1 + rng() % 40) / 100.0;
    DynGraph G(n);
    for (int u = 0; u < n; u++) for (int v = u + 1; v < n; v++) if ((rng() % 1000) / 1000.0 < p) G.add(u, v);
    CoreMaint cm(G);
    cm.init();
    for (int q = 0; q < 400; q++) {
      int u = rng() % n, v = rng() % n;
      if (u == v) continue;
      // bias toward insertions while sparse, deletions while dense
      bool del = G.has(u, v);
      if (del) { G.del(u, v); cm.afterDelete(u, v); } else { G.add(u, v); cm.afterInsert(u, v); }
      checks++;
      if (coreDecomp(G, 1).core != cm.K || !cm.checkOrder()) { bad++; fprintf(stderr, "core mismatch trial=%d q=%d n=%d %s\n", tr, q, n, del ? "del" : "ins"); break; }
    }
  }
  printf("corebrute checks=%lld bad=%lld\n", checks, bad);
  return bad != 0;
}

// kplex ext <graph> <k>: run the engine named by KPLEX_EXT once on a graph file
static int modeExt(int argc, char **argv) {
  Loaded L = loadGraph(argv[2], false);
  int k = atoi(argv[3]);
  if (!extEngine()) { fprintf(stderr, "set KPLEX_EXT\n"); return 1; }
  ExtResult r = runExternal(L.G, k);
  printf("{\"graph\":\"%s\",\"n\":%d,\"m\":%lld,\"k\":%d,\"engine\":\"%s\",\"omega\":%d,\"wall_s\":%.4f,\"own_s\":%.4f,\"write_s\":%.4f}\n",
         argv[2], L.G.n, L.G.m, k, r.engine.c_str(), r.omega, r.wall, r.own, r.write);
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: see header\n"); return 1; }
  string mode = argv[1];
  if (mode == "static") return modeStatic(argc, argv);
  if (mode == "dyn") return modeDyn(argc, argv);
  if (mode == "brute") return modeBrute(argc, argv);
  if (mode == "corebrute") return modeCoreBrute(argc, argv);
  if (mode == "corebench") return modeCoreBench(argc, argv);
  if (mode == "ext") return modeExt(argc, argv);
  return 1;
}
