/*
 * bench.c - mede o tempo de 6 algoritmos de ordenacao em varios "blocos".
 *
 * Cada bloco e uma instancia de entrada (distribuicao x tamanho x semente).
 * Todos os algoritmos ordenam exatamente a mesma entrada dentro de um bloco.
 * Para cada par (bloco, algoritmo) guardamos a MEDIANA de R repeticoes, em ms.
 * A saida e uma matriz "larga" (uma linha por bloco, uma coluna por algoritmo),
 * que e o formato que o teste de Friedman espera: blocos x tratamentos.
 *
 * Uso:   ./bench [-r reps] [-s sementes] [-q] [-o arquivo.csv]
 *   -r  repeticoes por (bloco, algoritmo)        (padrao 5)
 *   -s  numero de sementes por (distribuicao, tamanho) (padrao 2)
 *   -q  modo rapido: tamanhos 1e3, 1e4, 1e5
 *   -o  arquivo de saida (padrao: stdout)
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

typedef uint32_t u32;

/* ---------- gerador pseudoaleatorio (splitmix64): mesmo resultado em qualquer maquina */
static uint64_t rng_state;
static u32 rnd32(void) {
    uint64_t z = (rng_state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return (u32)((z ^ (z >> 31)) >> 16);
}

/* ---------- geradores de entrada ---------- */
enum { D_RANDOM, D_SORTED, D_REVERSED, D_NEARLY, D_FEWUNIQ, D_SAWTOOTH, N_DIST };
static const char *DIST_NAME[N_DIST] = {"aleatoria", "ordenada", "inversa",
                                        "quase_ordenada", "poucos_valores", "dente_de_serra"};

static void gen(u32 *a, size_t n, int d) {
    size_t i;
    switch (d) {
    case D_RANDOM:   for (i = 0; i < n; i++) a[i] = rnd32(); break;
    case D_SORTED:   for (i = 0; i < n; i++) a[i] = (u32)i; break;
    case D_REVERSED: for (i = 0; i < n; i++) a[i] = (u32)(n - i); break;
    case D_NEARLY:
        for (i = 0; i < n; i++) a[i] = (u32)i;
        for (i = 0; i < n / 100 + 1; i++) {          /* 1% de trocas aleatorias */
            size_t x = rnd32() % n, y = rnd32() % n;
            u32 t = a[x]; a[x] = a[y]; a[y] = t;
        }
        break;
    case D_FEWUNIQ:  for (i = 0; i < n; i++) a[i] = rnd32() % 10; break;
    case D_SAWTOOTH: for (i = 0; i < n; i++) a[i] = (u32)(i % 128); break;
    }
}

/* ---------- algoritmos ---------- */
static void insertion(u32 *a, long lo, long hi) {
    for (long i = lo + 1; i <= hi; i++) {
        u32 v = a[i]; long j = i - 1;
        while (j >= lo && a[j] > v) { a[j + 1] = a[j]; j--; }
        a[j + 1] = v;
    }
}

static int cmp_u32(const void *x, const void *y) {
    u32 a = *(const u32 *)x, b = *(const u32 *)y;
    return (a > b) - (a < b);
}
static void alg_qsort(u32 *a, size_t n) { qsort(a, n, sizeof(u32), cmp_u32); }

static void sift_down(u32 *a, size_t root, size_t n) {
    for (;;) {
        size_t c = 2 * root + 1;
        if (c >= n) return;
        if (c + 1 < n && a[c + 1] > a[c]) c++;
        if (a[root] >= a[c]) return;
        u32 t = a[root]; a[root] = a[c]; a[c] = t;
        root = c;
    }
}
static void alg_heapsort(u32 *a, size_t n) {
    if (n < 2) return;
    for (size_t i = n / 2; i-- > 0;) sift_down(a, i, n);
    for (size_t end = n - 1; end > 0; end--) {
        u32 t = a[0]; a[0] = a[end]; a[end] = t;
        sift_down(a, 0, end);
    }
}

static void msort(u32 *a, u32 *tmp, size_t n) {
    if (n <= 16) { if (n > 1) insertion(a, 0, (long)n - 1); return; }
    size_t m = n / 2;
    msort(a, tmp, m);
    msort(a + m, tmp, n - m);
    if (a[m - 1] <= a[m]) return;                    /* metades ja em ordem */
    memcpy(tmp, a, m * sizeof(u32));
    size_t i = 0, j = m, k = 0;
    while (i < m && j < n) a[k++] = (tmp[i] <= a[j]) ? tmp[i++] : a[j++];
    while (i < m) a[k++] = tmp[i++];
}
static void alg_mergesort(u32 *a, size_t n) {
    u32 *tmp = malloc((n / 2 + 1) * sizeof(u32));
    if (!tmp) { perror("malloc"); exit(1); }
    msort(a, tmp, n);
    free(tmp);
}

static void qs(u32 *a, long lo, long hi) {
    while (hi - lo > 16) {
        long mid = lo + (hi - lo) / 2;
        u32 x = a[lo], y = a[mid], z = a[hi];
        u32 p = (x < y) ? ((y < z) ? y : (x < z ? z : x)) : ((x < z) ? x : (y < z ? z : y));
        long i = lo, j = hi;
        while (i <= j) {
            while (a[i] < p) i++;
            while (a[j] > p) j--;
            if (i <= j) { u32 t = a[i]; a[i] = a[j]; a[j] = t; i++; j--; }
        }
        if (j - lo < hi - i) { qs(a, lo, j); lo = i; }   /* recursao so no lado menor */
        else                 { qs(a, i, hi); hi = j; }
    }
    insertion(a, lo, hi);
}
static void alg_quicksort(u32 *a, size_t n) { if (n > 1) qs(a, 0, (long)n - 1); }

static void alg_shellsort(u32 *a, size_t n) {
    long gaps[40]; int ng = 0;
    static const long ciura[] = {1, 4, 10, 23, 57, 132, 301, 701};
    for (int i = 0; i < 8; i++) gaps[ng++] = ciura[i];
    while (ng < 40 && gaps[ng - 1] * 9 / 4 < (long)n) { gaps[ng] = gaps[ng - 1] * 9 / 4; ng++; }
    for (int g = ng - 1; g >= 0; g--) {
        long h = gaps[g];
        if (h >= (long)n) continue;
        for (long i = h; i < (long)n; i++) {
            u32 v = a[i]; long j = i;
            while (j >= h && a[j - h] > v) { a[j] = a[j - h]; j -= h; }
            a[j] = v;
        }
    }
}

static void alg_radix(u32 *a, size_t n) {            /* LSD, 4 passadas de 8 bits */
    u32 *b = malloc(n * sizeof(u32));
    if (!b) { perror("malloc"); exit(1); }
    u32 *src = a, *dst = b;
    for (int shift = 0; shift < 32; shift += 8) {
        size_t cnt[256] = {0};
        for (size_t i = 0; i < n; i++) cnt[(src[i] >> shift) & 255]++;
        if (cnt[(src[0] >> shift) & 255] == n) continue;   /* byte constante: pula */
        size_t s = 0;
        for (int c = 0; c < 256; c++) { size_t t = cnt[c]; cnt[c] = s; s += t; }
        for (size_t i = 0; i < n; i++) dst[cnt[(src[i] >> shift) & 255]++] = src[i];
        u32 *t = src; src = dst; dst = t;
    }
    if (src != a) memcpy(a, src, n * sizeof(u32));
    free(b);
}

typedef void (*sort_fn)(u32 *, size_t);
#define K 6
static const char *ALG_NAME[K] = {"qsort", "heapsort", "mergesort", "quicksort", "shellsort", "radix"};
static const sort_fn ALG_FN[K] = {alg_qsort, alg_heapsort, alg_mergesort, alg_quicksort, alg_shellsort, alg_radix};

/* ---------- medicao ---------- */
static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}
static int cmp_double(const void *x, const void *y) {
    double a = *(const double *)x, b = *(const double *)y;
    return (a > b) - (a < b);
}
static int is_sorted(const u32 *a, size_t n) {
    for (size_t i = 1; i < n; i++) if (a[i - 1] > a[i]) return 0;
    return 1;
}
static uint64_t checksum(const u32 *a, size_t n) {
    uint64_t s = 0;
    for (size_t i = 0; i < n; i++) s += (uint64_t)a[i] * 2654435761u;  /* soma independe da ordem */
    return s;
}

int main(int argc, char **argv) {
    int reps = 5, seeds = 2, quick = 0;
    const char *outpath = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-r") && i + 1 < argc) reps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) seeds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) outpath = argv[++i];
        else if (!strcmp(argv[i], "-q")) quick = 1;
        else { fprintf(stderr, "uso: %s [-r reps] [-s sementes] [-q] [-o arquivo.csv]\n", argv[0]); return 2; }
    }
    if (reps < 1) reps = 1;
    if (seeds < 1) seeds = 1;
    size_t sizes_full[] = {10000, 100000, 1000000}, sizes_quick[] = {1000, 10000, 100000};
    size_t *sizes = quick ? sizes_quick : sizes_full;

    FILE *out = outpath ? fopen(outpath, "w") : stdout;
    if (!out) { perror("fopen"); return 1; }
    fprintf(out, "bloco,distribuicao,tamanho,semente");
    for (int a = 0; a < K; a++) fprintf(out, ",%s", ALG_NAME[a]);
    fprintf(out, "\n");

    size_t maxn = sizes[2];
    u32 *base = malloc(maxn * sizeof(u32)), *work = malloc(maxn * sizeof(u32));
    double *times = malloc(reps * sizeof(double));
    if (!base || !work || !times) { perror("malloc"); return 1; }

    int block = 0, total = N_DIST * 3 * seeds;
    for (int d = 0; d < N_DIST; d++)
        for (int si = 0; si < 3; si++)
            for (int seed = 1; seed <= seeds; seed++) {
                size_t n = sizes[si];
                rng_state = 0x1234ABCDULL * (uint64_t)(d + 1) + (uint64_t)n * 7919u + (uint64_t)seed * 104729u;
                gen(base, n, d);
                uint64_t cs = checksum(base, n);
                block++;
                fprintf(out, "b%02d,%s,%zu,%d", block, DIST_NAME[d], n, seed);
                for (int a = 0; a < K; a++) {
                    for (int r = 0; r < reps; r++) {
                        memcpy(work, base, n * sizeof(u32));
                        double t0 = now_ms();
                        ALG_FN[a](work, n);
                        times[r] = now_ms() - t0;
                        if (r == 0 && (!is_sorted(work, n) || checksum(work, n) != cs)) {
                            fprintf(stderr, "ERRO: %s ordenou errado (%s, n=%zu)\n", ALG_NAME[a], DIST_NAME[d], n);
                            return 1;
                        }
                    }
                    qsort(times, reps, sizeof(double), cmp_double);
                    fprintf(out, ",%.4f", times[reps / 2]);
                }
                fprintf(out, "\n");
                fflush(out);
                fprintf(stderr, "\rbloco %d/%d", block, total);
            }
    fprintf(stderr, "\nok\n");
    if (outpath) fclose(out);
    free(base); free(work); free(times);
    return 0;
}
