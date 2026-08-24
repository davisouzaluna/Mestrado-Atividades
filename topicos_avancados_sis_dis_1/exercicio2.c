//Essa questao é o exercicio 2 que consiste num sistema adaptativo, 
//Aula 24-08-2026
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

//SISTEMA GERENCIADO (Managed System)
// Contém o estado e a lógica principal da aplicação (o quiz).
typedef struct {
    int nivel; // 1 = Fácil, 2 = Médio, 3 = Difícil
    int a;
    int b;
    int resposta_correta;
    int resposta_usuario;
} SistemaGerenciado;

//SISTEMA GERENCIADOR (Managing System)
//Contém a lógica de controle, monitoramento e adaptação.
typedef struct {
    int acertos_consecutivos;
} SistemaGerenciador;

// O Sistema Gerenciado executa a geração do problema
void gerar_problema(SistemaGerenciado *quiz) {
    quiz->a = rand() % (quiz->nivel * 10) + 1;
    quiz->b = rand() % (quiz->nivel * 10) + 1;
    quiz->resposta_correta = quiz->a + quiz->b;
}

//O Sistema Gerenciador analisa o estado e adapta o Sistema Gerenciado
void executar_controle(SistemaGerenciado *quiz, SistemaGerenciador *controlador) {
    int acertou = (quiz->resposta_usuario == quiz->resposta_correta);

    if (acertou) {
        printf("Correto!\n");
        controlador->acertos_consecutivos++;
        
        //Regra de adaptação: sobe o nível do sistema gerenciado
        if (controlador->acertos_consecutivos >= 2 && quiz->nivel < 3) {
            quiz->nivel++;
            controlador->acertos_consecutivos = 0;
            printf("-> [Sistema Gerenciador] Desempenho alto detectado. Nivel aumentado para %d.\n", quiz->nivel);
        }
    } else {
        printf("Errado! A resposta correta era %d.\n", quiz->resposta_correta);
        controlador->acertos_consecutivos = 0;
        
        //Regra de adaptação: reduz o nível do sistema gerenciado
        if (quiz->nivel > 1) {
            quiz->nivel--;
            printf("-> [Sistema Gerenciador] Desempenho baixo detectado. Dificuldade reduzida para o nivel %d.\n", quiz->nivel);
        }
    }
}

int main() {
    srand(time(NULL));

    //Instancia os dois componentes da arquitetura adaptativa("gerenciado e gerenciador e inicia a struct")
    SistemaGerenciado quiz = { .nivel = 1 };
    SistemaGerenciador controlador = { .acertos_consecutivos = 0 };

    printf("--- Quiz Adaptativo (Arquitetura Gerenciado / Gerenciador) ---\n");

    while(1) {
        //1-O Sistema Gerenciado produz a interface/problema
        gerar_problema(&quiz);

        printf("\n[Nivel %d] Quanto e %d + %d? (Digite -1 para sair): ", quiz.nivel, quiz.a, quiz.b);
        if(scanf("%d", &quiz.resposta_usuario) != 1) break;
        if(quiz.resposta_usuario == -1) break;

        //2-O Sistema Gerenciador avalia e adapta o comportamento
        executar_controle(&quiz, &controlador);
    }

    printf("Fim do programa!\n");
    return 0;
}