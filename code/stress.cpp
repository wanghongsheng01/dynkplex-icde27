// Stress test for DynKPlex: exactness + direct check of the certificate invariant (Theorem 2).
// Build: clang++ -O2 -std=c++17 -o stress stress.cpp ; run: ./stress <trials> <seed>
// STRESS_BUDGET / STRESS_EVERY force the time-budget fallback; STRESS_KMIN / STRESS_KMAX choose k.
#define main kplex_main
#include "kplex.cpp"
#undef main

static vector<uint32_t> plexesAtLeast(const DynGraph &G, int k, int t) {
  int n = G.n; vector<uint32_t> nb(n, 0), out;
  for (int u = 0; u < n; u++) for (int v : G.adj[u]) nb[u] |= 1u << v;
  for (uint32_t s = 1; s < (1u << n); s++) {
    int sz = __builtin_popcount(s); if (sz < t) continue;
    bool ok = true;
    for (int u = 0; u < n && ok; u++) if (s >> u & 1) if (sz - __builtin_popcount(nb[u] & s) > k) ok = false;
    if (ok) out.push_back(s);
  }
  return out;
}

int main(int argc, char **argv) {
  int trials = atoi(argv[1]); mt19937 rng(atoi(argv[2]));
  long long fb = 0, lr = 0, checks = 0, bad = 0, certChecks = 0, certBad = 0, pendBad = 0, lzBad = 0;
  for (int tr = 0; tr < trials; tr++) {
    int kmin = getenv("STRESS_KMIN") ? atoi(getenv("STRESS_KMIN")) : 1, kmax = getenv("STRESS_KMAX") ? atoi(getenv("STRESS_KMAX")) : 4;
    int n = 8 + rng() % 11, k = kmin + rng() % (kmax - kmin + 1);
    double p = 0.3 + (rng() % 60) / 100.0;
    DynGraph G(n);
    for (int u = 0; u < n; u++) for (int v = u + 1; v < n; v++) if ((rng() % 1000) / 1000.0 < p) G.add(u, v);
    DynGraph G2 = G;
    DynKPlex D(G, k);
    int mode = tr % 32;
    D.useRepair = mode & 1; D.useCache = (mode >> 1) & 1; D.useCoreMaint = (mode >> 2) & 1; D.useWarm = (mode >> 3) & 1;
    D.useCoreUB = D.useCache && ((mode >> 4) & 1);
    D.init();
    if (getenv("STRESS_BUDGET")) { D.useBudget = true; D.stEst = atof(getenv("STRESS_BUDGET")); }
    if (getenv("STRESS_EAGER")) D.lazyInv = false;
    LazyMC *Z = nullptr; if (k == 1) { Z = new LazyMC(G2, 1); Z->init(); }
    for (int q = 0; q < 80; q++) {
      int u, v; bool del;
      int r = rng() % 4;
      if (r == 0 && D.omega() >= 2) {  // adversarial: delete an edge inside the current optimum
        vector<pair<int,int>> in;
        for (size_t i = 0; i < D.best.size(); i++) for (size_t j = i + 1; j < D.best.size(); j++) if (G.has(D.best[i], D.best[j])) in.push_back({D.best[i], D.best[j]});
        if (in.empty()) continue;
        tie(u, v) = in[rng() % in.size()]; del = true;
      } else if (r == 1) {  // two-hop insertion
        u = rng() % n; if (G.adj[u].empty()) continue;
        int x = G.adj[u][rng() % G.adj[u].size()]; if (G.adj[x].empty()) continue;
        v = G.adj[x][rng() % G.adj[x].size()];
        if (u == v || G.has(u, v)) continue; del = false;
      } else { u = rng() % n; v = rng() % n; if (u == v) continue; del = G.has(u, v); }
      if (del) D.deleteEdge(u, v); else D.insertEdge(u, v);
      checks++;
      int want = bruteOmega(G, k);
      if (D.omega() != want || !isKPlex(G, D.best, k)) { bad++; fprintf(stderr, "dyn mismatch tr=%d mode=%d n=%d k=%d got %d want %d\n", tr, mode, n, k, D.omega(), want); }
      if (Z) { if (del) Z->deleteEdge(u, v); else Z->insertEdge(u, v); if (Z->omega() != want || !isKPlex(G2, Z->best, 1)) lzBad++; }
      if (D.useCache && D.cacheT >= 0) {
        int t = D.cacheT;
        vector<char> inPend(n, 0); for (int x : D.pending) inPend[x] = 1;
        for (int x = 0; x < n; x++) if (!D.cleared[x] && !inPend[x]) pendBad++;
        for (uint32_t s : plexesAtLeast(G, k, t)) {
          int f = -1; for (int x = 0; x < n; x++) if (s >> x & 1) if (f < 0 || D.piPos[x] < D.piPos[f]) f = x;
          certChecks++;
          bool excused = false;
          for (auto [a, b] : D.insLog) if (G.has(a, b) && (s >> a & 1) && (s >> b & 1)) excused = true;
          if (D.cleared[f] && !excused) { certBad++; fprintf(stderr, "INVALID CERT tr=%d mode=%d n=%d k=%d t=%d first=%d\n", tr, mode, n, k, t, f); }
        }
      }
    }
    fb += D.ds.fallbacks; lr += D.ds.levelReuse;
    delete Z;
  }
  printf("fallbacks=%lld levelReuse=%lld ", fb, lr); printf("checks=%lld bad=%lld certChecks=%lld certBad=%lld pendingInvariantBad=%lld lazymcBad=%lld\n", checks, bad, certChecks, certBad, pendBad, lzBad);
  return 0;
}
