#include "communication.h"

#define PIPE_NAME "manager_pipe"
// #define MSG_FILE "mensagens_persistentes.txt" // Remover esta linha pois
// usamos ambinete variável
#define FIFO_MODE 0666  // Definir modo de permissão para o FIFO

Utilizador utilizadores[MAX_UTILIZADORES];
Topico topicos[MAX_TOPICOS];  // Lista de tópicos
Mensagem
    mensagens_persistentes[MAX_TOPICOS]
                          [MAX_MSGS_PERSISTENTES];  // Mensagens persistentes
                                                    // por tópico
int num_utilizadores = 0, num_topicos = 0;
pthread_mutex_t lock;

// Initialize mensagens_persistentes array
void inicializa_mensagens_persistentes() {
    for (int i = 0; i < MAX_TOPICOS; i++) {
        topicos[i].nome[0] =
            '\0';  // Inicia o nome do tópico com uma string vazia
        topicos[i].bloqueado = 0;  // Inicia o tópico como desbloqueado
        for (int j = 0; j < MAX_MSGS_PERSISTENTES; j++) {
            mensagens_persistentes[i][j].removida = 1;  // Inicia como removida
            mensagens_persistentes[i][j].tempo_de_vida =
                0;  // Inicia o tempo de vida como 0
            mensagens_persistentes[i][j].topico[0] =
                '\0';  // Inicia o nome do tópico com uma string vazia
        }
    }
    num_topicos = 0;  // Inicia o número de tópicos como 0
}

int main() {
    // Verificar se o FIFO já existe antes de criá-lo
    if (access(PIPE_NAME, F_OK) == -1) {
        if (mkfifo(PIPE_NAME, FIFO_MODE) == -1) {
            perror("Erro ao criar FIFO");
            exit(1);
        }
    }

    pthread_mutex_init(&lock, NULL);  // Inicializar o mutex

    inicializa_mensagens_persistentes();  // Inicializar mensagens persistentes

    carrega_mensagens_persistentes();  // Carregar mensagens persistentes

    int fd;

    printf("Manager iniciado...\n");

    // Iniciar thread para remover mensagens expiradas periodicamente
    pthread_t exp_thread;
    pthread_create(&exp_thread, NULL, (void *)remove_mensagens_expiradas, NULL);
    pthread_detach(exp_thread);

    // Criar um named pipe para receber conexões dos feeds
    if (access(PIPE_NAME, F_OK) == -1) {
        if (mkfifo(PIPE_NAME, FIFO_MODE) == -1) {
            perror("Erro ao criar FIFO");
            exit(1);
        }
    }

    // Inicializar estruturas para comandos administrativos
    pthread_t admin_tid;
    pthread_create(&admin_tid, NULL, admin_comandos, NULL);

    // Registar função de cleanup para ser chamada em qualquer tipo de saída
    atexit(fecha_sistema);

    // Adicionar handlers para mais sinais
    signal(SIGINT, fecha_sistema);   // Ctrl+C
    signal(SIGTERM, fecha_sistema);  // Terminar processo
    signal(SIGPIPE, fecha_sistema);  // Pipe fechado
    signal(SIGHUP, fecha_sistema);   // Terminal fechado
    signal(SIGABRT, fecha_sistema);  // Abortar processo

    while (1) {
        fd = open(PIPE_NAME, O_RDONLY);
        if (fd == -1) {
            perror("Erro ao abrir o pipe");
            exit(1);
        }

        // Ler mensagem do feed
        Mensagem msg;
        if (read(fd, &msg, sizeof(msg)) > 0) {
            pthread_t tid;
            // Criar uma thread para processar a mensagem
            Mensagem *new_msg = malloc(sizeof(Mensagem));
            *new_msg = msg;
            pthread_create(&tid, NULL, gere_cliente, (void *)new_msg);
            pthread_detach(tid);
        }
        close(fd);
    }

    pthread_mutex_destroy(&lock);  // Destruir o mutex no final
    return 0;
}

// Função para gerir os feeds
void *gere_cliente(void *arg) {  // Mensagens de erro enviados para o manager
    Mensagem *msg = (Mensagem *)arg;

    pthread_mutex_lock(&lock);
    if (msg->comando == LOGIN) {
        int result = valida_utilizador(msg->username, msg->pipe_name);
        if (result == 1) {
            envia_confirmacao(msg->pipe_name, SUCESSO);
            printf("Utilizador %s adicionado com sucesso.\n", msg->username);
        } else if (result == 2) {
            envia_confirmacao(msg->pipe_name, USERNAME_EXISTE);
            printf("Erro: Utilizador %s já existe.\n", msg->username);
        } else if (result == 3) {
            envia_confirmacao(msg->pipe_name, LIMITE_UTILIZADORES);
            printf("Erro: Limite de utilizadores atingido.\n");
        } else if (result == 4) {
            envia_confirmacao(msg->pipe_name, LIMITE_USERNAME);
            printf(
                "Erro: Não foi possivel adicionar o novo Utilizador.\nUsername "
                "muito longo \n limite: %d caracteres\n",
                MAX_USERNAME);
        }
    } else {
        gere_mensagem(msg);
    }
    pthread_mutex_unlock(&lock);

    free(msg);
    return NULL;
}

int procura_ou_cria_topico(char *nome_topico, char *pipe_name, int subscricao) {
    // Procurar se o tópico já existe
    for (int i = 0; i < num_topicos; i++) {
        if (strcmp(topicos[i].nome, nome_topico) == 0) {
            return i;  // Retorna o índice do tópico existente
        }
    }

    // Verificar se o limite de tópicos foi atingido ao criar um novo tópico
    if (num_topicos >= MAX_TOPICOS) {
        return -1;  // Indica que o limite foi atingido
    }

    // Criar novo tópico
    strcpy(topicos[num_topicos].nome, nome_topico);
    topicos[num_topicos].bloqueado = 0;

    return num_topicos++;  // Retorna o índice do novo tópico
}

void gere_mensagem(Mensagem *msg) {
    switch (msg->comando) {
        case MSG: {
            // Verificar ou criar o tópico sem indicar que é uma subscrição
            int topico_idx =
                procura_ou_cria_topico(msg->topico, msg->pipe_name, 0);
            if (topico_idx == -1) {
                // Enviar mensagem de erro para o feed
                Mensagem msg_erro;
                msg_erro.comando = MSG;
                strcpy(msg_erro.username, "Sistema");
                snprintf(
                    msg_erro.mensagem, sizeof(msg_erro.mensagem),
                    "Mensagem não enviada: Número máximo de tópicos atingido.");
                int fd = open(msg->pipe_name, O_WRONLY);
                if (fd != -1) {
                    write(fd, &msg_erro, sizeof(Mensagem));
                    close(fd);
                }
                return;
            }

            // Distribuir a mensagem para os subscritores
            distribui_mensagem(msg);
            break;
        }

        case SUBSCRIBE: {
            // Verificar ou criar o tópico indicando que é uma subscrição
            int topico_idx =
                procura_ou_cria_topico(msg->topico, msg->pipe_name, 1);
            if (topico_idx == -1) {
                // Enviar mensagem de erro para o feed
                Mensagem msg_erro;
                msg_erro.comando = SUBSCRIBE;
                strcpy(msg_erro.username, "Sistema");
                snprintf(msg_erro.mensagem, sizeof(msg_erro.mensagem),
                         "Subscrição nao aceite: Número máximo de tópicos "
                         "atingido.");
                int fd = open(msg->pipe_name, O_WRONLY);
                if (fd != -1) {
                    write(fd, &msg_erro, sizeof(Mensagem));
                    close(fd);
                } else {
                    perror("Erro ao abrir pipe para enviar notificação");
                }
                return;
            }

            // Adicionar o tópico à lista de subscritos do utilizador
            for (int i = 0; i < num_utilizadores; i++) {
                if (strcmp(utilizadores[i].username, msg->username) == 0) {
                    // Adicionar o tópico à lista do utilizador
                    strcpy(
                        utilizadores[i]
                            .topicos_subscritos[utilizadores[i].num_subscritos],
                        msg->topico);
                    utilizadores[i].num_subscritos++;

                    // Enviar mensagens persistentes do tópico para o utilizador
                    for (int k = 0; k < MAX_MSGS_PERSISTENTES; k++) {
                        if (!mensagens_persistentes[topico_idx][k].removida) {
                            int fd = open(utilizadores[i].pipe_name, O_WRONLY);
                            if (fd != -1) {
                                write(fd,
                                      &mensagens_persistentes[topico_idx][k],
                                      sizeof(Mensagem));
                                close(fd);
                            }
                        }
                    }
                    // Adicione esta linha para imprimir a mensagem de
                    // confirmação
                    printf("Utilizador %s subscreveu ao tópico %s\n",
                           msg->username, msg->topico);
                    break;
                }
            }
            break;
        }

        case UNSUBSCRIBE:
            printf("Utilizador %s cancelou a subscrição do tópico %s\n",
                   msg->username, msg->topico);
            for (int i = 0; i < num_utilizadores; i++) {
                if (strcmp(utilizadores[i].username, msg->username) == 0) {
                    for (int j = 0; j < utilizadores[i].num_subscritos; j++) {
                        if (strcmp(utilizadores[i].topicos_subscritos[j],
                                   msg->topico) == 0) {
                            for (int k = j;
                                 k < utilizadores[i].num_subscritos - 1; k++) {
                                strcpy(
                                    utilizadores[i].topicos_subscritos[k],
                                    utilizadores[i].topicos_subscritos[k + 1]);
                            }
                            utilizadores[i].num_subscritos--;
                            break;
                        }
                    }
                    break;
                }
            }
            break;

        case EXIT:
            printf("Utilizador %s saiu da plataforma.\n", msg->username);
            remove_utilizador(msg->username);
            break;

        case MOSTRAR_UTILIZADORES:
            mostrar_utilizadores();
            break;

        case REMOVE_UTILIZADOR:
            remove_utilizador(msg->username);
            break;

        case FECHA_SISTEMA:
            fecha_sistema();
            break;

        case MOSTRA_TOPICOS: {
            // Enviar tópicos para o feed que requisitou
            char feed_pipe[100];
            snprintf(feed_pipe, sizeof(feed_pipe), "%s_pipe", msg->username);
            mostra_topicos_feed(feed_pipe);
            break;
        }

        default:
            printf("Comando desconhecido recebido.\n");
            break;
    }
}

void mostra_topicos_feed(char *requester_pipe) {
    Mensagem msg;
    msg.comando = MOSTRA_TOPICOS;

    if (num_topicos == 0) {
        strcpy(msg.mensagem, "Não existem tópicos no sistema.\n");
    } else {
        strcpy(msg.mensagem, "\nLista de tópicos disponíveis:\n");
        for (int i = 0; i < num_topicos; i++) {
            char estado[20];
            if (topicos[i].bloqueado) {
                strcpy(estado, "Bloqueado");
            } else {
                strcpy(estado, "Desbloqueado");
            }
            char buffer[200];
            snprintf(buffer, sizeof(buffer),
                     "Tópico: %s, Mensagens Persistentes: %d, Estado: %s\n",
                     topicos[i].nome, conta_mensagens_persistentes(i), estado);
            strcat(msg.mensagem, buffer);
        }
    }

    int fd = open(requester_pipe, O_WRONLY);
    if (fd != -1) {
        write(fd, &msg, sizeof(Mensagem));
        close(fd);
    } else {
        perror("Erro ao abrir pipe para enviar lista de tópicos");
    }
}

// Mostra os tópicos e as mensagens persistentes associadas
void mostra_topicos() {
    printf("Lista de tópicos:\n");
    if (num_topicos == 0) {
        printf("Não existem tópicos.\n");
        return;
    }

    for (int i = 0; i < num_topicos; i++) {
        char *estado;
        if (topicos[i].bloqueado) {
            estado = "Bloqueado";
        } else {
            estado = "Desbloqueado";
        }

        printf("Tópico: %s, Mensagens Persistentes: %d, Estado: %s\n",
               topicos[i].nome, conta_mensagens_persistentes(i), estado);
    }
}

int procura_lugar_vazio_persistentes(char *topico, int *topic_index,
                                     int *msg_index) {
    // Procura o índice do tópico ou cria um novo caso não exista.
    *topic_index = procura_ou_cria_topico(topico, "", 0);
    if (*topic_index == -1) {
        return -1;  // nao ha espaco para novos topicos
    }

    // Percorre a lista de mensagens persistentes do tópico para encontrar um
    // espaço vazio.
    for (int j = 0; j < MAX_MSGS_PERSISTENTES; j++) {
        // Verifica se o slot de mensagem está marcado como removido
        // (disponível).
        if (mensagens_persistentes[*topic_index][j].removida == 1) {
            *msg_index = j;  // Armazena o índice do espaço vazio encontrado.
            return 0;
        }
    }

    // Retorna erro se não houver slots disponíveis para mensagens neste tópico.
    return -1;
}

// Nesta funcao é
int valida_utilizador(char *username, char *pipe_name) {
    // Verificar se o username é maior que o permitido
    if (strlen(username) >= MAX_USERNAME) {
        return 4;  // Código de erro para username muito longo
    }
    // verificar se o username ja existe
    for (int i = 0; i < num_utilizadores; i++) {
        if (strcmp(utilizadores[i].username, username) == 0 &&
            utilizadores[i].conectado) {
            return 2;  // Username já existe
        }
    }

    // verificar se o numero de limite de utilizadores foi atingido
    if (num_utilizadores >= MAX_UTILIZADORES) {
        return 3;  // Limite de utilizadores atingido
    }

    // Se tudo passar adicionar o novo utilizador
    strcpy(utilizadores[num_utilizadores].username, username);
    utilizadores[num_utilizadores].conectado = 1;
    strcpy(utilizadores[num_utilizadores].pipe_name, pipe_name);
    utilizadores[num_utilizadores].num_subscritos = 0;
    num_utilizadores++;
    return 1;  // Utilizador registado com sucesso
}

void remove_utilizador(char *username) {
    int utilizador_index = -1;
    char removed_pipe_name[50];

    // Procurar e assinalar o utilizador como desconectado
    for (int i = 0; i < num_utilizadores; i++) {
        if (strcmp(utilizadores[i].username, username) == 0 &&
            utilizadores[i].conectado) {
            utilizadores[i].conectado = 0;
            utilizador_index = i;
            strcpy(removed_pipe_name, utilizadores[i].pipe_name);
            printf("Utilizador %s removido.\n", username);

            // notificar o utilizador que foi removido
            Mensagem msg;
            msg.comando = REMOVE_UTILIZADOR;
            strcpy(msg.username, username);
            strcpy(msg.pipe_name, removed_pipe_name);
            num_utilizadores--;
            int fd = open(removed_pipe_name, O_WRONLY);
            if (fd != -1) {
                write(fd, &msg, sizeof(msg));
                close(fd);
            }
            break;
        }
    }

    if (utilizador_index != -1) {
        // Notificar todos os outros utilizadores
        Mensagem msg_notificacao;
        msg_notificacao.comando = REMOVE_UTILIZADOR;
        strcpy(msg_notificacao.username, username);
        strcpy(msg_notificacao.pipe_name,
               "");  // Pipe vazia para notificar os outros utilizadores

        for (int i = 0; i < num_utilizadores; i++) {
            if (i != utilizador_index && utilizadores[i].conectado) {
                int fd = open(utilizadores[i].pipe_name, O_WRONLY);
                if (fd != -1) {
                    write(fd, &msg_notificacao, sizeof(msg_notificacao));
                    close(fd);
                }
            }
        }
    }
}

void bloqueia_topico(char *username, char *topico) {
    for (int i = 0; i < num_topicos; i++) {
        if (strcmp(topicos[i].nome, topico) == 0) {
            topicos[i].bloqueado = 1;
            printf("Tópico %s bloqueado por %s.\n", topico, username);

            // Notifica todos os utilizadores subscritos que o topico foi
            // bloqueado
            Mensagem msg_notificacao;
            msg_notificacao.comando = TOPICO_BLOQUEADO;
            strcpy(msg_notificacao.username, "Sistema");
            strcpy(msg_notificacao.topico, topico);
            sprintf(msg_notificacao.mensagem,
                    "O tópico %s foi bloqueado pelo administrador", topico);

            for (int j = 0; j < num_utilizadores; j++) {
                if (utilizadores[j].conectado) {
                    for (int k = 0; k < utilizadores[j].num_subscritos; k++) {
                        if (strcmp(utilizadores[j].topicos_subscritos[k],
                                   topico) == 0) {
                            int fd = open(utilizadores[j].pipe_name, O_WRONLY);
                            if (fd != -1) {
                                write(fd, &msg_notificacao, sizeof(Mensagem));
                                close(fd);
                            }
                            break;
                        }
                    }
                }
            }
            return;
        }
    }
    printf("Tópico %s não encontrado.\n", topico);
}

void desbloqueia_topico(char *username, char *topico) {
    for (int i = 0; i < num_topicos; i++) {
        if (strcmp(topicos[i].nome, topico) == 0) {
            topicos[i].bloqueado = 0;
            printf("Tópico %s desbloqueado.\n", topico);

            // Notifica todos os utilizadores subscritos que o topico foi
            // desbloqueado
            Mensagem msg_notificacao;
            msg_notificacao.comando = TOPICO_DESBLOQUEADO;
            strcpy(msg_notificacao.username, "Sistema");
            strcpy(msg_notificacao.topico, topico);
            sprintf(msg_notificacao.mensagem,
                    "O tópico %s foi desbloqueado pelo administrador", topico);

            for (int j = 0; j < num_utilizadores; j++) {
                if (utilizadores[j].conectado) {
                    for (int k = 0; k < utilizadores[j].num_subscritos; k++) {
                        if (strcmp(utilizadores[j].topicos_subscritos[k],
                                   topico) == 0) {
                            int fd = open(utilizadores[j].pipe_name, O_WRONLY);
                            if (fd != -1) {
                                write(fd, &msg_notificacao, sizeof(Mensagem));
                                close(fd);
                            }
                            break;
                        }
                    }
                }
            }
            return;
        }
    }
    printf("Tópico %s não encontrado.\n", topico);
}

void mostrar_utilizadores() {
    printf("Lista de utilizadores conectados:\n");
    for (int i = 0; i < num_utilizadores; i++) {
        if (utilizadores[i].conectado) {
            printf("%s\n", utilizadores[i].username);
        }
    }
}

void distribui_mensagem(Mensagem *msg) {
    printf("Distribuindo mensagem do tópico %s para os subscritores.\n",
           msg->topico);

    // Verifica se a mensagem é persistente e deve ser armazenada
    if (msg->persistente) {
        int total_persistentes = 0;
        for (int i = 0; i < num_topicos; i++) {
            for (int j = 0; j < MAX_MSGS_PERSISTENTES; j++) {
                if (!mensagens_persistentes[i][j].removida) {
                    total_persistentes++;
                }
            }
        }

        if (total_persistentes >= (num_topicos * MAX_MSGS_PERSISTENTES)) {
            printf("Erro: Limite global de mensagens persistentes atingido.\n");

            // Envia feedback ao remetente
            Mensagem msg_erro = {0};
            msg_erro.comando = LIMITE_PERSISTENTES;
            strcpy(msg_erro.username, "Sistema");
            snprintf(msg_erro.mensagem, sizeof(msg_erro.mensagem),
                     "Erro: Limite global de mensagens persistentes atingido.");
            strcpy(msg_erro.topico, msg->topico);
            strcpy(msg_erro.pipe_name, msg->pipe_name);

            int fd = open(msg->pipe_name, O_WRONLY);
            if (fd != -1) {
                write(fd, &msg_erro, sizeof(msg_erro));
                close(fd);
            } else {
                perror("Erro ao abrir pipe para enviar notificação de erro");
            }
            return;
        }

        int topic_index, msg_index;

        // Procura um espaço disponível para armazenar a mensagem persistente
        if (procura_lugar_vazio_persistentes(msg->topico, &topic_index,
                                             &msg_index) == 0) {
            mensagens_persistentes[topic_index][msg_index] =
                *msg;  // Armazena a mensagem
            mensagens_persistentes[topic_index][msg_index].removida =
                0;  // Marca como ativa
            printf("Mensagem persistente guardada no tópico %s\n", msg->topico);
        } else {
            printf(
                "Erro: Não foi possível guardar a mensagem persistente (sem "
                "espaço)\n");
        }
    }

    // Verifica se o tópico associado está bloqueado
    for (int i = 0; i < num_topicos; i++) {
        if (strcmp(topicos[i].nome, msg->topico) ==
            0) {  // Encontra o tópico correspondente
            if (topicos[i].bloqueado) {
                printf("O tópico %s está bloqueado. Mensagem não enviada.\n",
                       msg->topico);

                // Cria uma mensagem de notificação para o remetente
                Mensagem msg_notificacao;
                msg_notificacao.comando = MSG_TOPICO_BLOQUEADO;
                strcpy(msg_notificacao.topico, msg->topico);
                strcpy(msg_notificacao.username, "Sistema");
                strcpy(msg_notificacao.pipe_name,
                       msg->pipe_name);  // Define o pipe de retorno
                // Envia a notificação para o remetente
                int fd = open(msg->pipe_name, O_WRONLY);
                if (fd != -1) {
                    write(fd, &msg_notificacao, sizeof(Mensagem));
                    close(fd);
                } else {
                    perror("Erro ao abrir pipe para enviar notificação");
                }
                return;  // Interrompe a distribuição
            }
            break;  // Tópico encontrado e não está bloqueado
        }
    }

    // Distribui a mensagem para todos os utilizadores inscritos no tópico
    for (int i = 0; i < num_utilizadores; i++) {
        if (utilizadores[i].conectado) {  // Apenas para utilizadores conectados
            for (int j = 0; j < utilizadores[i].num_subscritos; j++) {
                if (strcmp(utilizadores[i].topicos_subscritos[j],
                           msg->topico) == 0) {
                    // Encontra um utilizador subscrito no tópico
                    int fd = open(utilizadores[i].pipe_name,
                                  O_WRONLY);  // Abre o pipe do utilizador
                    if (fd != -1) {
                        write(fd, msg, sizeof(Mensagem));  // Envia a mensagem
                        close(fd);
                        printf("Mensagem enviada para %s: %s\n",
                               utilizadores[i].username, msg->mensagem);
                    } else {
                        perror("Erro ao abrir pipe para enviar mensagem");
                    }
                    break;  // Envia para um utilizador e passa para o próximo
                }
            }
        }
    }
}

// Função para guardar mensagens persistentes num ficheiro
void guarda_mensagens_persistentes() {
    // Obter o nome do ficheiro a partir da variável de ambiente
    char *filename = getenv("MSG_FICH");
    if (!filename) {
        printf("Variável de ambiente MSG_FICH não definida\n");
        return;
    }

    // Abrir o ficheiro em modo de escrita (criar ou truncar se necessário)
    int file = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (file == -1) {
        perror("Erro ao abrir ficheiro de mensagens persistentes");
        return;
    }
    char buffer[1024];
    // Percorrer todos os tópicos e mensagens persistentes
    for (int i = 0; i < num_topicos; i++) {
        for (int j = 0; j < MAX_MSGS_PERSISTENTES; j++) {
            // Apenas guardar mensagens não removidas e com tempo de vida válido
            if (!mensagens_persistentes[i][j].removida &&
                mensagens_persistentes[i][j].tempo_de_vida > 0) {
                // Formatar os dados da mensagem para o ficheiro
                int len = snprintf(
                    buffer, sizeof(buffer), "%s %s %d %s\n",
                    topicos[i].nome,                        // Nome do tópico
                    mensagens_persistentes[i][j].username,  // Autor da mensagem
                    mensagens_persistentes[i][j]
                        .tempo_de_vida,                      // Tempo restante
                    mensagens_persistentes[i][j].mensagem);  // Conteúdo
                // Escrever os dados formatados no ficheiro
                write(file, buffer, len);
            }
        }
    }
    // Fechar o ficheiro após a escrita
    close(file);
}

void carrega_mensagens_persistentes() {
    // Obtém o nome do ficheiro a partir da variável de ambiente
    char *filename = getenv("MSG_FICH");
    if (!filename) {
        printf("Variável de ambiente MSG_FICH não definida\n");
        return;
    }

    // Abre o ficheiro em modo leitura
    FILE *file = fopen(filename, "r");
    if (!file) {
        return;  // O ficheiro pode não existir na primeira execução
    }

    char line[1024];
    // Lê cada linha do ficheiro até o final
    while (fgets(line, sizeof(line), file)) {
        // Remove o caractere de nova linha (newline), se presente
        line[strcspn(line, "\n")] = 0;

        // Divide a linha em partes (tópico, username, tempo de vida, mensagem)
        char *topico_ptr = strtok(line, " ");
        char *username_ptr = strtok(NULL, " ");
        char *tempo_de_vida_ptr = strtok(NULL, " ");
        char *mensagem_ptr =
            strtok(NULL, "");  // Captura o restante da linha como mensagem

        // Verifica se todos os campos necessários estão presentes
        if (!topico_ptr || !username_ptr || !tempo_de_vida_ptr ||
            !mensagem_ptr) {
            continue;  // Ignora linhas inválidas
        }

        // Cria uma estrutura de mensagem com os dados carregados
        Mensagem msg = {0};
        msg.comando = MSG;  // Define o comando como mensagem
        strncpy(msg.topico, topico_ptr,
                MAX_TOPICO_NOME - 1);  // Copia o nome do tópico
        strncpy(msg.username, username_ptr,
                MAX_USERNAME - 1);  // Copia o nome do utilizador
        msg.tempo_de_vida =
            atoi(tempo_de_vida_ptr);  // Converte o tempo de vida para inteiro
        strncpy(msg.mensagem, mensagem_ptr,
                MAX_MSG_LEN - 1);  // Copia o conteúdo da mensagem
        msg.persistente = 1;       // Marca a mensagem como persistente
        msg.removida = 0;          // Marca a mensagem como ativa

        // Procura um slot disponível para armazenar a mensagem no tópico
        // correspondente
        int topico_idx, msg_idx;
        if (procura_lugar_vazio_persistentes(msg.topico, &topico_idx,
                                             &msg_idx) == 0) {
            mensagens_persistentes[topico_idx][msg_idx] =
                msg;  // Armazena a mensagem no slot
        }
    }

    // Fecha o ficheiro após a leitura
    fclose(file);
}

void remove_mensagens_expiradas() {
    while (1) {
        sleep(1);                   // Esperar 1 segundo entre verificações
        pthread_mutex_lock(&lock);  // Lock mutex
        for (int i = 0; i < MAX_TOPICOS; i++) {
            for (int j = 0; j < MAX_MSGS_PERSISTENTES; j++) {
                if (mensagens_persistentes[i][j].removida == 0 &&
                    mensagens_persistentes[i][j].tempo_de_vida > 0) {
                    mensagens_persistentes[i][j].tempo_de_vida--;
                    if (mensagens_persistentes[i][j].tempo_de_vida <= 0) {
                        mensagens_persistentes[i][j].removida =
                            1;  // Marcar como removida
                        printf("Mensagem expirada removida do tópico %s\n",
                               mensagens_persistentes[i][j].topico);
                    }
                }
            }
        }
        pthread_mutex_unlock(&lock);  // Unlock mutex
    }
}

void fecha_sistema() {
    static int limpar = 0;
    if (limpar) return;  // Prevenir chamadas recursivas
    limpar = 1;

    printf("Sistema encerrado.\n");

    // Notifica todos os utilizadores que o sistema foi encerrado
    for (int i = 0; i < num_utilizadores; i++) {
        if (utilizadores[i].conectado) {
            Mensagem msg;
            msg.comando = FECHA_SISTEMA;
            int fd = open(utilizadores[i].pipe_name, O_WRONLY);
            if (fd != -1) {
                write(fd, &msg, sizeof(msg));
                close(fd);
            }
        }
    }

    guarda_mensagens_persistentes();

    // Limpa os pipes
    unlink(PIPE_NAME);

    // Remove os pipes dos utilizadores ainda conectados
    for (int i = 0; i < num_utilizadores; i++) {
        if (utilizadores[i].conectado) {
            unlink(utilizadores[i].pipe_name);
        }
    }

    // Se não for chamada por atexit, terminar o programa
    if (errno != EINTR) {
        exit(0);
    }
}

void *admin_comandos(void *arg) {
    char comando[100];
    while (1) {
        printf("Admin> ");
        fgets(comando, sizeof(comando), stdin);

        // Processar o comando
        if (strncmp(comando, "users", 5) == 0) {
            mostrar_utilizadores();
        } else if (strncmp(comando, "remove", 6) == 0) {
            char username[MAX_USERNAME];
            sscanf(comando, "remove %s", username);
            remove_utilizador(username);
        } else if (strncmp(comando, "lock", 4) == 0) {
            char topico[MAX_TOPICO_NOME];
            sscanf(comando, "lock %s", topico);
            bloqueia_topico("admin", topico);
        } else if (strncmp(comando, "unlock", 6) == 0) {
            char topico[MAX_TOPICO_NOME];
            sscanf(comando, "unlock %s", topico);
            desbloqueia_topico("admin", topico);
        } else if (strncmp(comando, "topics", 6) == 0) {
            mostra_topicos();
        } else if (strncmp(comando, "show", 4) == 0) {
            char topico[MAX_TOPICO_NOME];
            sscanf(comando, "show %s", topico);
            mostra_topicos_mensagens(topico);
        } else if (strncmp(comando, "close", 5) == 0) {
            fecha_sistema();
        } else {
            printf("Comando desconhecido: %s", comando);
        }
    }
    return NULL;
}

void mostra_topicos_mensagens(char *topico) {
    for (int i = 0; i < num_topicos; i++) {
        if (strcmp(topicos[i].nome, topico) == 0) {
            printf("Mensagens do tópico %s:\n", topico);
            for (int j = 0; j < MAX_MSGS_PERSISTENTES; j++) {
                if (!mensagens_persistentes[i][j].removida) {
                    printf("Autor: %s, Mensagem: %s, Duração: %d\n",
                           mensagens_persistentes[i][j].username,
                           mensagens_persistentes[i][j].mensagem,
                           mensagens_persistentes[i][j].tempo_de_vida);
                }
            }
            return;
        }
    }
    printf("Tópico %s não encontrado.\n", topico);
}

int conta_mensagens_persistentes(int topic_index) {
    int contador = 0;
    for (int j = 0; j < MAX_MSGS_PERSISTENTES; j++) {
        if (!mensagens_persistentes[topic_index][j].removida) {
            contador++;
        }
    }
    return contador;
}

int verifica_username_disponivel(char *username) {
    for (int i = 0; i < num_utilizadores; i++) {
        if (strcmp(utilizadores[i].username, username) == 0 &&
            utilizadores[i].conectado) {
            return 0;  // Username ja existe
        }
    }
    return 1;  // Username nao existe
}

// Evia confirmacoes para o feed
void envia_confirmacao(char *pipe_name, TipoComando status) {
    // Abre o pipe nomeado no modo de escrita
    int fd = open(pipe_name, O_WRONLY);
    if (fd == -1) {
        // Em caso de erro ao abrir o pipe, imprime uma mensagem de erro
        perror("Erro ao abrir pipe para enviar confirmação");
        return;
    }

    // Cria uma mensagem de confirmação com o comando especificado
    Mensagem resposta;
    resposta.comando = status;  // Define o comando da mensagem

    // Escreve a mensagem no pipe
    write(fd, &resposta, sizeof(resposta));

    // Fecha o pipe após o envio
    close(fd);
}