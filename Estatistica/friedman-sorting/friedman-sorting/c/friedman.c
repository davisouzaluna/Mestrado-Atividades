/*
 * friedman.c - teste de Friedman do zero, so com a biblioteca padrao de C.
 *
 * Entrada: CSV "largo" (uma linha por bloco, uma coluna por tratamento).
 * As primeiras M colunas sao metadados e sao ignoradas (padrao M=4, como no bench).
 * Por padrao, MENOR valor = MELHOR (tempo). Use -H se maior for melhor (acuracia).
 *
 * Passos (os mesmos dos slides):
 *   1. ranquear os tratamentos dentro de cada bloco (empate = posto medio)
 *   2. somar os postos de cada tratamento: R_j
 *   3. Q = 12/(n k (k+1)) * soma(R_j^2) - 3 n (k+1), dividido pela correcao de empates C
 *   4. p-valor pela qui-quadrado com k-1 graus de liberdade
 *   + Iman-Davenport (F), W de Kendall e diferenca critica de Nemenyi.
 *
 * Uso: ./friedman arquivo.csv [-m colunas_de_metadados] [-H] [-v]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAXK 32
#define MAXN 4096
#define LINE 4096

/* ---------- funcoes especiais (Numerical Recipes, versao curta) ---------- */

/* Q(a,x): gama incompleta regularizada superior = P(X > 2x) para X ~ qui-quadrado(2a) */
static double gammaq(double a, double x) {
    if (x <= 0) return 1.0;
    double gln = lgamma(a);
    if (x < a + 1.0) {                       /* serie para P(a,x), depois 1-P */
        double ap = a, sum = 1.0 / a, del = sum;
        for (int n = 0; n < 500; n++) {
            ap += 1.0; del *= x / ap; sum += del;
            if (fabs(del) < fabs(sum) * 1e-15) break;
        }
        return 1.0 - sum * exp(-x + a * log(x) - gln);
    } else {                                 /* fracao continua para Q(a,x) */
        double b = x + 1.0 - a, c = 1.0 / 1e-300, d = 1.0 / b, h = d;
        for (int i = 1; i < 500; i++) {
            double an = -i * (i - a);
            b += 2.0;
            d = an * d + b; if (fabs(d) < 1e-300) d = 1e-300;
            c = b + an / c; if (fabs(c) < 1e-300) c = 1e-300;
            d = 1.0 / d;
            double del = d * c; h *= del;
            if (fabs(del - 1.0) < 1e-15) break;
        }
        return exp(-x + a * log(x) - gln) * h;
    }
}
static double chi2_sf(double q, double df) { return gammaq(df / 2.0, q / 2.0); }

/* fracao continua da beta incompleta */
static double betacf(double a, double b, double x) {
    double qab = a + b, qap = a + 1.0, qam = a - 1.0, c = 1.0, d = 1.0 - qab * x / qap;
    if (fabs(d) < 1e-300) d = 1e-300;
    d = 1.0 / d;
    double h = d;
    for (int m = 1; m <= 500; m++) {
        int m2 = 2 * m;
        double aa = m * (b - m) * x / ((qam + m2) * (a + m2));
        d = 1.0 + aa * d; if (fabs(d) < 1e-300) d = 1e-300;
        c = 1.0 + aa / c; if (fabs(c) < 1e-300) c = 1e-300;
        d = 1.0 / d; h *= d * c;
        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2));
        d = 1.0 + aa * d; if (fabs(d) < 1e-300) d = 1e-300;
        c = 1.0 + aa / c; if (fabs(c) < 1e-300) c = 1e-300;
        d = 1.0 / d;
        double del = d * c; h *= del;
        if (fabs(del - 1.0) < 1e-15) break;
    }
    return h;
}
static double betai(double a, double b, double x) {
    if (x <= 0) return 0.0;
    if (x >= 1) return 1.0;
    double bt = exp(lgamma(a + b) - lgamma(a) - lgamma(b) + a * log(x) + b * log(1.0 - x));
    if (x < (a + 1.0) / (a + b + 2.0)) return bt * betacf(a, b, x) / a;
    return 1.0 - bt * betacf(b, a, 1.0 - x) / b;
}
/* P(F > f) para F(d1, d2) */
static double f_sf(double f, double d1, double d2) { return betai(d2 / 2.0, d1 / 2.0, d2 / (d2 + d1 * f)); }

/* q_alfa de Nemenyi (Demsar 2006, Tab. 5): quantil da amplitude studentizada / sqrt(2), k = 2..10 */
static const double Q05[] = {0, 0, 1.960, 2.344, 2.569, 2.728, 2.850, 2.948, 3.031, 3.102, 3.164};
static const double Q10[] = {0, 0, 1.645, 2.052, 2.291, 2.460, 2.589, 2.693, 2.780, 2.855, 2.920};

/* ---------- leitura do CSV ---------- */
static double X[MAXN][MAXK];
static char names[MAXK][64];

int main(int argc, char **argv) {
    const char *path = NULL;
    int meta = 4, higher_better = 0, verbose = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-m") && i + 1 < argc) meta = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-H")) higher_better = 1;
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (argv[i][0] != '-') path = argv[i];
        else { fprintf(stderr, "uso: %s arquivo.csv [-m meta] [-H] [-v]\n", argv[0]); return 2; }
    }
    if (!path) { fprintf(stderr, "uso: %s arquivo.csv [-m meta] [-H] [-v]\n", argv[0]); return 2; }
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return 1; }

    char line[LINE];
    if (!fgets(line, sizeof line, f)) { fprintf(stderr, "arquivo vazio\n"); return 1; }
    int k = 0, col = 0;
    for (char *tok = strtok(line, ",\r\n"); tok; tok = strtok(NULL, ",\r\n"), col++)
        if (col >= meta) {
            if (k >= MAXK) { fprintf(stderr, "tratamentos demais (max %d)\n", MAXK); return 1; }
            snprintf(names[k++], sizeof names[0], "%s", tok);
        }
    if (k < 2) { fprintf(stderr, "preciso de pelo menos 2 tratamentos (confira -m)\n"); return 1; }

    int n = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '\n' || line[0] == '\r' || line[0] == '#') continue;
        if (n >= MAXN) { fprintf(stderr, "blocos demais (max %d)\n", MAXN); return 1; }
        col = 0;
        int j = 0;
        for (char *tok = strtok(line, ",\r\n"); tok; tok = strtok(NULL, ",\r\n"), col++)
            if (col >= meta && j < k) X[n][j++] = strtod(tok, NULL);
        if (j != k) { fprintf(stderr, "linha %d com %d valores (esperado %d)\n", n + 2, j, k); return 1; }
        n++;
    }
    fclose(f);
    if (n < 2) { fprintf(stderr, "preciso de pelo menos 2 blocos\n"); return 1; }

    /* ---- passo 1: postos dentro de cada bloco (com empates) ---- */
    static double rk[MAXN][MAXK];
    double R[MAXK] = {0}, tie_sum = 0.0;
    for (int b = 0; b < n; b++) {
        for (int j = 0; j < k; j++) {
            double better = 0, equal = 0;           /* quantos sao melhores / iguais */
            for (int l = 0; l < k; l++) {
                if (l == j) continue;
                if (X[b][l] == X[b][j]) equal++;
                else if (higher_better ? X[b][l] > X[b][j] : X[b][l] < X[b][j]) better++;
            }
            rk[b][j] = 1.0 + better + equal / 2.0;  /* posto medio */
        }
        /* grupos de empate: soma de (t^3 - t) */
        for (int j = 0; j < k; j++) {
            double t = 1;
            int first = 1;
            for (int l = 0; l < k; l++) if (l != j && X[b][l] == X[b][j]) { t++; if (l < j) first = 0; }
            if (first && t > 1) tie_sum += t * t * t - t;
        }
        for (int j = 0; j < k; j++) R[j] += rk[b][j];
    }

    if (verbose) {
        printf("Postos por bloco (1 = melhor)\n%-8s", "bloco");
        for (int j = 0; j < k; j++) printf(" %10s", names[j]);
        printf("\n");
        for (int b = 0; b < n; b++) {
            printf("%-8d", b + 1);
            for (int j = 0; j < k; j++) printf(" %10.1f", rk[b][j]);
            printf("\n");
        }
        printf("\n");
    }

    /* ---- passos 2 e 3 ---- */
    double sumR2 = 0;
    for (int j = 0; j < k; j++) sumR2 += R[j] * R[j];
    double Q0 = 12.0 / ((double)n * k * (k + 1)) * sumR2 - 3.0 * n * (k + 1);
    double C = 1.0 - tie_sum / ((double)n * k * ((double)k * k - 1.0));
    double Q = (C > 0) ? Q0 / C : 0.0;
    double df = k - 1;
    double p = chi2_sf(Q, df);
    double W = Q / (n * df);
    double FF = (Q < n * df) ? (n - 1.0) * Q / (n * df - Q) : INFINITY;
    double pF = isfinite(FF) ? f_sf(FF, df, df * (n - 1.0)) : 0.0;

    printf("blocos (n) = %d   tratamentos (k) = %d   %s\n\n", n, k,
           higher_better ? "maior = melhor" : "menor = melhor");
    printf("%-12s %10s %12s\n", "tratamento", "soma R_j", "posto medio");
    int ord[MAXK];
    for (int j = 0; j < k; j++) ord[j] = j;
    for (int a = 1; a < k; a++) {                    /* ordena por soma de postos */
        int v = ord[a], b = a - 1;
        while (b >= 0 && R[ord[b]] > R[v]) { ord[b + 1] = ord[b]; b--; }
        ord[b + 1] = v;
    }
    for (int a = 0; a < k; a++)
        printf("%-12s %10.1f %12.3f\n", names[ord[a]], R[ord[a]], R[ord[a]] / n);
    double total = 0;
    for (int j = 0; j < k; j++) total += R[j];
    printf("\nconferencia: soma de todos os postos = %.1f (esperado n*k*(k+1)/2 = %.1f)\n",
           total, n * k * (k + 1) / 2.0);

    printf("\n--- teste de Friedman ---\n");
    printf("Q = %.4f   (gl = %d, correcao de empates C = %.4f)\n", Q, k - 1, C);
    printf("p (qui-quadrado) = %.4g\n", p);
    if (isfinite(FF))
        printf("Iman-Davenport: F = %.4f   gl = (%d, %d)   p = %.4g\n", FF, k - 1, (k - 1) * (n - 1), pF);
    else
        printf("Iman-Davenport: F infinito (todos os blocos com a mesma ordem), p ~ 0\n");
    printf("W de Kendall = %.4f   (0 = nenhuma concordancia, 1 = concordancia total)\n", W);
    if (n <= 10 || k <= 5)
        printf("aviso: a regra pratica do Demsar para o qui-quadrado e n > 10 e k > 5; aqui n=%d, k=%d.\n", n, k);

    if (k <= 10) {
        double cd05 = Q05[k] * sqrt(k * (k + 1.0) / (6.0 * n));
        double cd10 = Q10[k] * sqrt(k * (k + 1.0) / (6.0 * n));
        printf("\n--- Nemenyi: diferenca critica entre postos medios ---\n");
        printf("DC(alfa=0.05) = %.3f    DC(alfa=0.10) = %.3f\n", cd05, cd10);
        printf("pares com |dif. de posto medio| > DC(0.05):\n");
        int any = 0;
        for (int a = 0; a < k; a++)
            for (int b = a + 1; b < k; b++) {
                double d = fabs(R[ord[a]] - R[ord[b]]) / n;
                if (d > cd05) { printf("  %-10s x %-10s  dif = %.3f\n", names[ord[a]], names[ord[b]], d); any = 1; }
            }
        if (!any) printf("  nenhum\n");
    }
    return 0;
}
