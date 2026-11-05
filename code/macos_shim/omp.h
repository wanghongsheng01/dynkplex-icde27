// Single-thread stand-in for <omp.h> (Apple clang ships no OpenMP); PMC is then run with one thread.
#pragma once
inline int omp_get_max_threads() { return 1; }
inline int omp_get_thread_num() { return 0; }
inline void omp_set_num_threads(int) {}
