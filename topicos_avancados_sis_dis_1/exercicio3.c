#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* ======
 * Aluno: Davi Souza de Luna
 * MAPLE-K --- atividade 3
 * Elemento gerenciado: a geracao das perguntas de soma, cuja
 * dificuldade depende do nivel guardado no Knowledge.
 * ----------------
 * Cada fase do loop vira uma funcao propria:
 *   K -> struct Knowledge
 *   M -> monitorar()
 *   A -> analisar()
 *   P -> planejar()
 *   E -> executar()
 * Assim fica mais fácil de ver as fases. Em tese a atividade 1 pode ser comentada para identificar as etapas do MAPE-K, mas preferi assim.
 *  * === */

/* K: knowledge
   Base de conhecimento compartilhada entre as fases. Guarda o
   estado que representa o desempenho do usuario ao longo do
   tempo. Sumarizando, é o nível e o acerto. */
typedef struct {
    int nivel;// 1=facil, 2=medio, 3=dificil
    int acertos_consecutivos;
} Knowledge;

typedef enum {
    ACAO_MANTER,
    ACAO_SUBIR,
    ACAO_DESCER
} Acao;

/*Elemento gerenciado 
   Gera a pergunta de acordo com o nivel de dificuldade atual,
   lido do Knowledge. */
void gerar_pergunta(Knowledge *k, int *a, int *b, int *resposta_correta) {
    *a = rand() % (k->nivel * 10) + 1;
    *b = rand() % (k->nivel * 10) + 1;
    *resposta_correta = *a + *b;
}

/*M-MONITOR
   Sensor que observa o sistema gerenciado (o usuario) e coleta
   o dado bruto de desempenho: a resposta digitada.
   Retorna 0 quando o usuario pede para sair ou a leitura falha. */
int monitorar(int *resposta_usuario) {
    if (scanf("%d", resposta_usuario) != 1) return 0;
    if (*resposta_usuario == -1) return 0;
    return 1;
}

/*A-ANALYZE
   Interpreta o dado monitorado, classifica o evento como acerto
   ou erro e atualiza o Knowledge. Retorna 1 se acertou, 0 se
   errou. */
int analisar(Knowledge *k, int resposta_usuario, int resposta_correta) {
    int acertou = (resposta_usuario == resposta_correta);

    if (acertou) {
        k->acertos_consecutivos++;
    } else {
        k->acertos_consecutivos = 0;
    }
    return acertou;
}

/* P-PLAN
   A partir do Knowledge ja atualizado, decide se e necessario
   adaptar o nivel e em qual direcao, sem alterar nada ainda. */
Acao planejar(Knowledge *k, int acertou) {
    if (acertou && k->acertos_consecutivos >= 2 && k->nivel < 3) {
        return ACAO_SUBIR;
    }
    if (!acertou && k->nivel > 1) {
        return ACAO_DESCER;
    }
    return ACAO_MANTER;
}

/*E-EXECUTE
   Aplica a acao planejada sobre o elemento gerenciado, mudando
   o nivel de dificuldade que a proxima pergunta vai usar. */
void executar(Knowledge *k, Acao acao) {
    switch (acao) {
        case ACAO_SUBIR:
            k->nivel++;
            k->acertos_consecutivos = 0;
            printf("-> [Adaptacao] Desempenho alto! Nivel aumentado para %d.\n", k->nivel);
            break;
        case ACAO_DESCER:
            k->nivel--;
            printf("-> [Adaptacao] Dificuldade reduzida para o nivel %d.\n", k->nivel);
            break;
        case ACAO_MANTER:
        default:
            break;
    }
}

// Aqui é semelhante, quase igual a atividade 1.
int main() {
    Knowledge k = {1, 0}; // estado inicial do Knowledge(K)
    int a, b, resposta_usuario, resposta_correta;

    srand(time(NULL));
    printf(" Quiz\n");

    while (1) {
        gerar_pergunta(&k, &a, &b, &resposta_correta);
        printf("\n[Nivel %d] Quanto e %d + %d? (Digite -1 para sair): ", k.nivel, a, b);

        if (!monitorar(&resposta_usuario)) break;

        if (resposta_usuario == resposta_correta) {
            printf("Correto!\n");
        } else {
            printf("Errado! A resposta correta era %d.\n", resposta_correta);
        }

        int acertou = analisar(&k, resposta_usuario, resposta_correta);
        Acao acao = planejar(&k, acertou);
        executar(&k, acao);
    }

    printf("Fim do programa!\n");
    return 0;
}