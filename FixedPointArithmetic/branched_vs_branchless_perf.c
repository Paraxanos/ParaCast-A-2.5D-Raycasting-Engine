#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include <stdalign.h>
// Preventing compiler from reordering or eliminating loops
#define COMPILER_BARRIER() asm volatile("" ::: "memory")
// branched variation
static inline int32_t div_pow2_branch(int32_t n, int k) {
    int32_t bias = 0;
    if (n < 0) {
        bias = (1 << k) - 1;
    }
    return (n + bias) >> k;
}
// branchless variation
static inline int32_t div_pow2_branchless(int32_t n, int k) {
    int32_t bias = (n >> (k - 1)) & ((1U << k) - 1);
    return (n + bias) >> k;
}
int main(void) {
    const int N = 50000000; // 50 million iterations for strong benchmarking
    int k = 4;              // division constant:16
    // Allocate aligned buffer to avoid cache-line prefetch unnecessarily fetching extra cache lines, which will waste L1 cache efficiency
    int32_t *data = aligned_alloc(64, N * sizeof(int32_t));
    if (!data) { perror("aligned_alloc"); return 1; }
    // Fill with unpredictable positive/negative mix to thrash branch predictor
    srand(42);
    for (int i = 0; i < N; i++) {
        data[i] = (rand() % 20000) - 10000; // Range: -10000 to 9999
    }
    volatile int32_t sink = 0; // Forces compiler to keep results
    struct timespec start, end;
    // ─────────────────────────────────────────────────────────────
    // benchmark for branched
    // ─────────────────────────────────────────────────────────────
    clock_gettime(CLOCK_MONOTONIC, &start);
    COMPILER_BARRIER();
    for (int i = 0; i < N; i++) {
        sink += div_pow2_branch(data[i], k);
    }
    COMPILER_BARRIER();
    clock_gettime(CLOCK_MONOTONIC, &end);
    double branched_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);

    // ─────────────────────────────────────────────────────────────
    // benchmark for branchless
    // ─────────────────────────────────────────────────────────────
    clock_gettime(CLOCK_MONOTONIC, &start);
    COMPILER_BARRIER();
    for (int i = 0; i < N; i++) {
        sink += div_pow2_branchless(data[i], k);
    }
    COMPILER_BARRIER();
    clock_gettime(CLOCK_MONOTONIC, &end);
    double branchless_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);

    // ─────────────────────────────────────────────────────────────
    // RESULTS
    // ─────────────────────────────────────────────────────────────
    printf("Iterations: %d\n", N);
    printf("Branched:    %.3f ns/op (%.2f ms total)\n", branched_ns / N, branched_ns / 1e6);
    printf("Branchless:  %.3f ns/op (%.2f ms total)\n", branchless_ns / N, branchless_ns / 1e6);
    printf("Sink: %ld (prevents dead-code elimination)\n", (long)sink);
    free(data);
    return 0;
}