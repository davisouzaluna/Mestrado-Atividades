CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra
PY      ?= python3

all: c/bench c/friedman

c/bench: c/bench.c
	$(CC) $(CFLAGS) -o $@ $<

c/friedman: c/friedman.c
	$(CC) $(CFLAGS) -o $@ $< -lm

# benchmark completo (cerca de 30 s): gera a matriz blocos x algoritmos
data/resultados.csv: c/bench
	./c/bench -o $@

# tudo: mede, roda o Friedman em C e a analise completa em Python
run: all data/resultados.csv
	./c/friedman data/resultados.csv
	$(PY) python/friedman_analysis.py data/resultados.csv --out saida

# versao rapida (poucos segundos), bom para testar
rapido: all
	./c/bench -q -r 3 -o data/resultados_rapido.csv
	./c/friedman data/resultados_rapido.csv
	$(PY) python/friedman_analysis.py data/resultados_rapido.csv --out saida_rapido

# exemplo pequeno dos slides: 3 algoritmos x 6 blocos, passo a passo
mini: all
	$(PY) python/friedman_analysis.py data/resultados_exemplo.csv --algos qsort heapsort mergesort --where tamanho=100000 semente=1 --verbose --out saida_mini

test: all
	$(PY) -m unittest discover -s tests -v
	./c/friedman data/toy_3x6.csv | grep -q "Q = 8.3333" && echo "C: exemplo 3x6 confere (Q = 8.3333)"

clean:
	rm -f c/bench c/friedman
	rm -rf saida saida_rapido saida_mini data/resultados.csv data/resultados_rapido.csv

.PHONY: all run rapido mini test clean
