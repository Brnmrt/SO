#ifndef COMMUNICATION_H
#define COMMUNICATION_H

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MAX_TOPICOS 4        // alterar para 20
#define MAX_UTILIZADORES 10  // alterar para 10
#define MAX_MSG_LEN 300
#define MAX_TOPICO_NOME 20
#define MAX_USERNAME 30
#define MAX_MSGS_PERSISTENTES 5  // Por topico

// enumeracao para enviar mensagens de confirmacao sobre as operacoes realizadas
// de confirmacao
typedef enum {
    LOGIN,
    SUCESSO,
    MSG,
    SUBSCRIBE,
    UNSUBSCRIBE,
    EXIT,
    LIMITE_PERSISTENTES,
    LOCK,
    MSG_TOPICO_BLOQUEADO,
    TOPICO_BLOQUEADO,
    TOPICO_DESBLOQUEADO,
    UNLOCK,
    MOSTRAR_UTILIZADORES,
    REMOVE_UTILIZADOR,
    FECHA_SISTEMA,
    MOSTRA_TOPICOS,
    MOSTRA_TOPICO,
    USERNAME_EXISTE,
    LIMITE_UTILIZADORES,
    LIMITE_USERNAME
} TipoComando;

// Estrutura da mensagem enviada pelo feed para o manager
typedef struct {
    TipoComando comando;           // Tipo de comando
    char topico[MAX_TOPICO_NOME];  // Nome do tópico
    char username[MAX_USERNAME];   // Nome do utilizador
    char pipe_name[50];            // Nome do pipe do feed
    int persistente;               // Indica se a mensagem é persistente
    int tempo_de_vida;  // Tempo de vida em segundos para mensagens persistentes
    size_t msg_len;     // Tamanho real da mensagem
    char mensagem[MAX_MSG_LEN];  // Conteúdo da mensagem
    int removida;                // Flag para indicar se a mensagem foi removida
} Mensagem;

// Estrutura que representa um utilizador na plataforma
typedef struct {
    char username[MAX_USERNAME];  // Nome do utilizador
    int conectado;                // Estado de conexão do utilizador
    char pipe_name[50];  // Nome do pipe associado ao feed do utilizador
    char topicos_subscritos[MAX_TOPICOS]
                           [MAX_TOPICO_NOME];  // Lista de tópicos subscritos
    int num_subscritos;                        // Número de tópicos subscritos
} Utilizador;

// Estrutura que representa um tópico
typedef struct {
    char nome[MAX_TOPICO_NOME];  // Nome do tópico
    int bloqueado;               // Indica se o tópico está bloqueado
} Topico;

void *gere_cliente(void *arg);
int valida_utilizador(char *username, char *pipe_name);
void gere_mensagem(Mensagem *msg);
void mostrar_utilizadores();
void remove_utilizador(char *username);
void bloqueia_topico(char *username, char *topico);
void desbloqueia_topico(char *username, char *topico);
void guarda_mensagens_persistentes();
void carrega_mensagens_persistentes();
void fecha_sistema();
int procura_lugar_vazio_persistentes(char *topico, int *topic_index,
                                     int *msg_index);
void distribui_mensagem(Mensagem *msg);
void remove_mensagens_expiradas();
void *admin_comandos(void *arg);
int verifica_username_disponivel(char *username);
void envia_confirmacao(char *pipe_name, TipoComando status);
void *escuta_mensagens(void *arg);
void envia_mensagens(char *username, char *topico, char *mensagem,
                     int persistent, int duration);
void subscreve_topico(char *username, char *topico);
void anula_topico(char *username, char *topico);
void sair_plataforma(char *username);
void mostra_topicos();
void mostra_topicos_mensagens(char *topico);
void limpar_e_sair();
void mostra_topicos_feed(char *requester_pipe);
int conta_mensagens_persistentes(int topic_index);
#endif
