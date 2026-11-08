#!/usr/bin/env python3
"""
friedman_analysis.py - analise completa de comparacao de algoritmos com o teste de Friedman.

Le o CSV "largo" gerado por c/bench (uma linha por bloco, uma coluna por algoritmo) e faz:
  1. postos dentro de cada bloco, soma e posto medio
  2. estatistica Q, p-valor (qui-quadrado), correcao de empates, Iman-Davenport, W de Kendall
  3. conferencia com scipy.stats.friedmanchisquare
  4. post-hoc: Nemenyi (diferenca critica) e Wilcoxon pareado com correcao de Holm
  5. diagrama de diferenca critica (como o das Figs. 7 a 10 do artigo) e mapa de postos

Exemplos:
  python friedman_analysis.py ../data/resultados.csv
  python friedman_analysis.py ../data/resultados.csv --algos qsort mergesort heapsort --where tamanho=100000 --verbose
"""
import argparse
import itertools
import math
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from scipy import stats

META = ["bloco", "distribuicao", "tamanho", "semente"]


# ----------------------------------------------------------------------------
# Teste de Friedman, implementado a partir das formulas
# ----------------------------------------------------------------------------
def friedman(X, lower_is_better=True):
    """X: matriz n (blocos) x k (tratamentos). Retorna um dicionario com todos os passos."""
    X = np.asarray(X, dtype=float)
    n, k = X.shape
    # passo 1: postos dentro de cada bloco (1 = melhor), empate = posto medio
    ranks = np.vstack([stats.rankdata(row if lower_is_better else -row) for row in X])
    # passo 2: soma de postos por tratamento
    R = ranks.sum(axis=0)
    # passo 3: estatistica Q
    Q0 = 12.0 / (n * k * (k + 1)) * np.sum(R ** 2) - 3.0 * n * (k + 1)
    # correcao de empates: C = 1 - soma(t^3 - t) / (n k (k^2 - 1))
    tie = 0.0
    for row in X:
        _, counts = np.unique(row, return_counts=True)
        tie += np.sum(counts ** 3 - counts)
    C = 1.0 - tie / (n * k * (k ** 2 - 1))
    Q = Q0 / C
    df = k - 1
    p = stats.chi2.sf(Q, df)
    # Iman-Davenport: F com (k-1, (k-1)(n-1)) graus de liberdade
    F = (n - 1) * Q / (n * (k - 1) - Q) if Q < n * (k - 1) else math.inf
    pF = stats.f.sf(F, df, df * (n - 1)) if math.isfinite(F) else 0.0
    W = Q / (n * (k - 1))
    return dict(n=n, k=k, ranks=ranks, R=R, mean_ranks=R / n, Q=Q, Q_sem_correcao=Q0, C=C,
                df=df, p=p, F=F, df2=df * (n - 1), p_F=pF, W=W)


def exact_pvalue(Q, n, k, limit=2_000_000):
    """p-valor exato por enumeracao das (k!)^n tabelas de postos (so para n e k pequenos; sem empates)."""
    if math.factorial(k) ** n > limit:
        return None
    counts = {}
    perms = list(itertools.permutations(range(1, k + 1)))
    for combo in itertools.product(perms, repeat=n):
        R = tuple(sum(c[j] for c in combo) for j in range(k))
        counts[R] = counts.get(R, 0) + 1
    total = sum(counts.values())
    hit = 0
    for R, v in counts.items():
        q = 12.0 / (n * k * (k + 1)) * sum(r * r for r in R) - 3.0 * n * (k + 1)
        if q >= Q - 1e-9:
            hit += v
    return hit / total


def friedman_from_mean_ranks(mean_ranks, n):
    """Q e Iman-Davenport a partir dos postos medios (formula do Demsar, 2006, sem empates)."""
    r = np.asarray(mean_ranks, dtype=float)
    k = len(r)
    chi2_f = 12.0 * n / (k * (k + 1)) * (np.sum(r ** 2) - k * (k + 1) ** 2 / 4.0)
    f_f = (n - 1) * chi2_f / (n * (k - 1) - chi2_f)
    return chi2_f, f_f


def nemenyi_cd(k, n, alpha=0.05):
    """Diferenca critica de Nemenyi: CD = q_alpha * sqrt(k (k+1) / (6 n))."""
    q = stats.studentized_range.ppf(1 - alpha, k, np.inf) / math.sqrt(2)
    return q * math.sqrt(k * (k + 1) / (6.0 * n)), q


def holm(pvals):
    """Correcao de Holm. Retorna os p ajustados na mesma ordem da entrada."""
    p = np.asarray(pvals, dtype=float)
    m = len(p)
    order = np.argsort(p)
    adj = np.empty(m)
    running = 0.0
    for i, idx in enumerate(order):
        running = max(running, (m - i) * p[idx])
        adj[idx] = min(1.0, running)
    return adj


def pairwise_wilcoxon_holm(X, names):
    """Wilcoxon pareado (bilateral) para todos os pares, com Holm."""
    pairs = list(itertools.combinations(range(len(names)), 2))
    raw = []
    for a, b in pairs:
        d = X[:, a] - X[:, b]
        raw.append(1.0 if np.all(d == 0) else stats.wilcoxon(X[:, a], X[:, b]).pvalue)
    adj = holm(raw)
    return [(names[a], names[b], r, h) for (a, b), r, h in zip(pairs, raw, adj)]


# ----------------------------------------------------------------------------
# Diagrama de diferenca critica
# ----------------------------------------------------------------------------
def cliques_from_matrix(order, not_different):
    """Grupos maximos de algoritmos consecutivos (na ordem dos postos) sem diferenca significativa."""
    k = len(order)
    groups = []
    for i in range(k):
        j = i
        while j + 1 < k and all(not_different[order[a]][order[j + 1]] for a in range(i, j + 1)):
            j += 1
        if j > i:
            groups.append((i, j))
    # remove grupos contidos em outros
    return [g for g in groups if not any(h != g and h[0] <= g[0] and g[1] <= h[1] for h in groups)]


def cd_diagram(mean_ranks, names, not_different, title, path, cd=None):
    """Diagrama de diferenca critica: melhor (posto 1) a direita; barras vermelhas ligam grupos sem diferenca."""
    k = len(names)
    order = list(np.argsort(mean_ranks))
    half = math.ceil(k / 2)
    row = 0.34                                   # altura de cada rotulo, em polegadas
    H = 1.5 + row * half + (0.35 if cd is not None else 0)
    fig, ax = plt.subplots(figsize=(9, H))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, H)
    ax.axis("off")
    x0, x1 = 1.4, 8.6
    y_axis = H - 0.55 - (0.35 if cd is not None else 0)

    def px(r):
        return x1 - (r - 1) / max(k - 1, 1) * (x1 - x0)

    ax.plot([x0, x1], [y_axis, y_axis], color="black", lw=1.2)
    for t in range(1, k + 1):
        ax.plot([px(t), px(t)], [y_axis, y_axis + 0.08], color="black", lw=1)
        ax.text(px(t), y_axis + 0.12, str(t), ha="center", va="bottom", fontsize=10)
    for pos, idx in enumerate(order):
        r = mean_ranks[idx]
        right = pos < half
        y = y_axis - 0.45 - row * (pos if right else pos - half)
        xt = 8.9 if right else 1.1
        ax.plot([px(r), px(r)], [y_axis, y], color="black", lw=0.9)
        ax.plot([px(r), xt - 0.08 if right else xt + 0.08], [y, y], color="black", lw=0.9)
        ax.text(xt, y, f"{names[idx]}  ({r:.2f})" if right else f"({r:.2f})  {names[idx]}",
                ha="left" if right else "right", va="center", fontsize=10)
    for gi, (i, j) in enumerate(cliques_from_matrix(order, not_different)):
        xa, xb = px(mean_ranks[order[i]]), px(mean_ranks[order[j]])
        ax.plot([xa - 0.05, xb + 0.05], [y_axis - 0.2 - 0.09 * gi] * 2, color="#AF0421", lw=4, solid_capstyle="round")
    if cd is not None:
        w = cd / max(k - 1, 1) * (x1 - x0)
        ax.plot([x0, x0 + w], [H - 0.28] * 2, color="black", lw=1.5)
        ax.plot([x0, x0], [H - 0.34, H - 0.22], color="black", lw=1.5)
        ax.plot([x0 + w, x0 + w], [H - 0.34, H - 0.22], color="black", lw=1.5)
        ax.text(x0 + w + 0.15, H - 0.28, f"CD = {cd:.2f}", ha="left", va="center", fontsize=9)
    ax.set_title(title, fontsize=10, loc="left", pad=2)
    fig.savefig(path, dpi=200, bbox_inches="tight")
    plt.close(fig)


def rank_heatmap(ranks, names, labels, path):
    fig, ax = plt.subplots(figsize=(1.0 + 0.9 * len(names), 1.0 + 0.22 * len(labels)))
    im = ax.imshow(ranks, aspect="auto", cmap="RdYlGn_r", vmin=1, vmax=len(names))
    ax.set_xticks(range(len(names)), names, rotation=30, ha="right")
    ax.set_yticks(range(len(labels)), labels, fontsize=7)
    ax.set_title("Posto de cada algoritmo em cada bloco (1 = mais rapido)", fontsize=10)
    fig.colorbar(im, ax=ax, shrink=0.7)
    fig.tight_layout()
    fig.savefig(path, dpi=200)
    plt.close(fig)


# ----------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv")
    ap.add_argument("--algos", nargs="+", help="usar so estes algoritmos (colunas)")
    ap.add_argument("--where", nargs="*", default=[], metavar="COL=VAL", help="filtrar blocos, ex.: tamanho=100000")
    ap.add_argument("--alpha", type=float, default=0.05)
    ap.add_argument("--higher-is-better", action="store_true", help="padrao: menor valor = melhor (tempo)")
    ap.add_argument("--out", default="saida", help="pasta de saida (figuras e relatorio)")
    ap.add_argument("--verbose", action="store_true", help="mostra a matriz de postos passo a passo")
    args = ap.parse_args()

    df = pd.read_csv(args.csv)
    for cond in args.where:
        col, val = cond.split("=", 1)
        df = df[df[col].astype(str) == val]
    meta = [c for c in META if c in df.columns]
    algos = args.algos or [c for c in df.columns if c not in meta]
    X = df[algos].to_numpy(dtype=float)
    if meta:
        labels = df[meta[:3]].astype(str).agg(" ".join, axis=1).tolist()
    else:
        labels = [str(i + 1) for i in range(len(df))]
    os.makedirs(args.out, exist_ok=True)

    res = friedman(X, lower_is_better=not args.higher_is_better)
    n, k = res["n"], res["k"]
    out = []

    def say(s=""):
        print(s)
        out.append(s)

    say(f"# Friedman: {k} algoritmos em {n} blocos")
    say("")
    if args.verbose:
        say("## Passo 1: postos dentro de cada bloco (1 = melhor)")
        say(pd.DataFrame(res["ranks"], columns=algos, index=labels).round(1).to_string())
        say("")
    say("## Passo 2: soma e posto medio")
    tab = pd.DataFrame({"soma_R": res["R"], "posto_medio": res["mean_ranks"]}, index=algos).sort_values("posto_medio")
    say(tab.round(3).to_string())
    say(f"conferencia: soma de todos os postos = {res['R'].sum():.1f} (esperado n*k*(k+1)/2 = {n*k*(k+1)/2:.1f})")
    say("")
    say("## Passo 3 e 4: estatistica e p-valor")
    say(f"Q = {res['Q']:.4f}  (gl = {res['df']}, C = {res['C']:.4f})  p = {res['p']:.3g}")
    say(f"Iman-Davenport: F = {res['F']:.4f}  gl = ({res['df']}, {res['df2']})  p = {res['p_F']:.3g}")
    say(f"W de Kendall = {res['W']:.4f}")
    # conferencia com scipy
    sc = stats.friedmanchisquare(*[X[:, j] for j in range(k)])
    ok = abs(sc.statistic - res["Q"]) < 1e-8
    say(f"scipy.stats.friedmanchisquare: Q = {sc.statistic:.4f}, p = {sc.pvalue:.3g}  -> {'confere' if ok else 'DIFERENTE'}")
    pe = exact_pvalue(res["Q"], n, k) if res["C"] == 1.0 else None
    if pe is not None:
        say(f"p exato (enumeracao de todas as tabelas de postos) = {pe:.4g}")
    if n <= 10 or k <= 5:
        say(f"aviso: regra pratica de Demsar para o qui-quadrado: n > 10 e k > 5 (aqui n={n}, k={k}).")
    say("")

    alpha = args.alpha
    say(f"## Post-hoc (alfa = {alpha})")
    reject = res["p"] < alpha
    if not reject:
        say("O Friedman nao rejeitou H0; em principio, nao se faz post-hoc.")
    mr = res["mean_ranks"]
    nem_nd = {i: {j: True for j in range(k)} for i in range(k)}
    cd = None
    if k >= 2:
        cd, q = nemenyi_cd(k, n, alpha)
        say(f"Nemenyi: q_alfa = {q:.3f}, CD = {cd:.3f}")
        for i, j in itertools.combinations(range(k), 2):
            d = abs(mr[i] - mr[j])
            nem_nd[i][j] = nem_nd[j][i] = d <= cd
    wil = pairwise_wilcoxon_holm(X if not args.higher_is_better else -X, algos)
    wil_nd = {i: {j: True for j in range(k)} for i in range(k)}
    say("")
    say(f"{'par':<28}{'dif. posto':>11}{'Nemenyi':>10}{'p Wilcoxon':>12}{'p Holm':>10}{'Holm':>8}")
    for (a, b, praw, padj) in wil:
        i, j = algos.index(a), algos.index(b)
        d = abs(mr[i] - mr[j])
        sig_n = d > cd
        sig_w = padj < alpha
        wil_nd[i][j] = wil_nd[j][i] = not sig_w
        say(f"{a + ' x ' + b:<28}{d:>11.3f}{'difere' if sig_n else '-':>10}{praw:>12.3g}{padj:>10.3g}{'difere' if sig_w else '-':>8}")
    say("")

    cd_diagram(mr, algos, wil_nd, f"Wilcoxon + Holm (alfa={alpha}), n={n}", os.path.join(args.out, "cd_wilcoxon_holm.png"))
    cd_diagram(mr, algos, nem_nd, f"Nemenyi (alfa={alpha}), n={n}", os.path.join(args.out, "cd_nemenyi.png"), cd=cd)
    rank_heatmap(res["ranks"], algos, labels, os.path.join(args.out, "mapa_de_postos.png"))
    with open(os.path.join(args.out, "relatorio.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print(f"\nfiguras e relatorio em: {args.out}/")


if __name__ == "__main__":
    sys.exit(main())
