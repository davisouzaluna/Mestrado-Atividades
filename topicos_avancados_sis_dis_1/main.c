#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main() {
    int nivel = 1;//1=facil, 2=medio, 3=dificil
    int acertos_consecutivos = 0;
    int a, b, resposta_usuario, resposta_correta;

    srand(time(NULL));
    printf(" Quiz\n");

    while(1) {
        //O alcance dos números muda conforme o nível de adaptação
        a = rand() % (nivel * 10) + 1;
        b = rand() % (nivel * 10) + 1;
        resposta_correta = a + b;

        printf("\n[Nivel %d] Quanto e %d + %d? (Digite -1 para sair): ", nivel, a, b);
        if(scanf("%d", &resposta_usuario) != 1) break;
        if(resposta_usuario == -1) break;

        if(resposta_usuario == resposta_correta) {
            printf("Correto!\n");
            acertos_consecutivos++;
            
            //Adaaaaapta: sobe de nível após 2 acertos seguidos
            if(acertos_consecutivos >= 2 && nivel < 3) {
                nivel++;
                acertos_consecutivos = 0;
                printf("-> [Adaptacaoo] Desempenho alto! Nivel aumentado para %d.\n", nivel);
            }
        } else {
            printf("Errado! A resposta correta era %d.\n", resposta_correta);
            acertos_consecutivos = 0;
            
            //Adaaaaapta: diminui o nível se errar
            if(nivel > 1) {
                nivel--;
                printf("-> [Adaptaacao] Dificuldade reduzida para o nivel %d.\n", nivel);
            }
        }
    }
    printf("Fim do programa!\n");
    return 0;
}