#include "communication.h"
#include "ctype.h"
#define PIPE_NAME "manager_pipe"

pthread_mutex_t lock;
int manager_fd;
char *username;

void limpar_buffer() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

void limpar_e_sair() {
    static int limpar = 0;
    if (limpar) return;  // Prevenir chamadas recursivas
    limpar = 1;

    // Enviar comando EXIT ao manager antes de encerrar
    if (manager_fd != -1 && username != NULL) {
        sair_plataforma(username);
    }

    // Fechar conexão com o manager
    if (manager_fd != -1) {
        close(manager_fd);
        manager_fd = -1;  // Marcar como fechado
    }

    // Remover o pipe do feed
    if (username) {
        char feed_pipe[50];
        sprintf(feed_pipe, "%s_pipe", username);
        unlink(feed_pipe);
    }

    // Destruir mutex se inicializado
    pthread_mutex_destroy(&lock);

    exit(0);  // Garantir que o feed encerre
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Uso: %s <username>\n", argv[0]);
        exit(1);
    }

    username = argv[1];
    char topico[MAX_TOPICO_NOME];
    char mensagem[MAX_MSG_LEN];
    int persistente, duracao;
    char comando[256];

    printf("Bem-vindo, %s!\n", username);

    char feed_pipe[50];
    sprintf(feed_pipe, "%s_pipe", username);

    // Criar named pipe exclusivo para o feed
    if (access(feed_pipe, F_OK) == -1) {
        if (mkfifo(feed_pipe, 0666) == -1) {
            perror("Erro ao criar FIFO do feed");
            exit(1);
        }
    }

    // Tentar conectar ao manager antes de continuar
    manager_fd = open(PIPE_NAME, O_WRONLY);
    if (manager_fd == -1) {
        perror("Manager não está disponível\n");
        unlink(
            feed_pipe);  // Remove o pipe se não conseguir conectar ao manager
        exit(1);
    }

    // Inicializar mutex
    pthread_mutex_init(&lock, NULL);

    // Enviar username e pipe_name para o manager
    Mensagem login_msg;
    login_msg.comando = LOGIN;
    strcpy(login_msg.username, username);
    strcpy(login_msg.pipe_name, feed_pipe);

    write(manager_fd, &login_msg, sizeof(login_msg));

    // Aguardar confirmação do manager
    int fd = open(feed_pipe, O_RDONLY);
    if (fd == -1) {
        perror("Erro ao abrir o pipe do feed\n");
        exit(1);
    }
    Mensagem resposta;
    read(fd, &resposta, sizeof(resposta));
    close(fd);

    // Se houver erro na autenticação, limpar recursos
    if (resposta.comando !=
        SUCESSO) {  // Mensagens de erro enviados para o feed
        close(manager_fd);
        pthread_mutex_destroy(&lock);
        unlink(feed_pipe);
        if (resposta.comando == USERNAME_EXISTE) {
            printf(
                "Erro: Username já está em uso. Por favor, escolha outro.\n");
        } else if (resposta.comando == LIMITE_UTILIZADORES) {
            printf(
                "Erro: Limite de utilizadores atingido. Tente novamente mais "
                "tarde.\n");
        } else if (resposta.comando == LIMITE_USERNAME) {
            printf("Erro: Username é muito longo.\nLimite: %d caracteres\n",
                   MAX_USERNAME);
        } else {
            printf("Erro ao estabelecer ligação.\n");
        }
        exit(1);
    }

    if (resposta.comando == SUCESSO) {
        printf("Ligação estabelecida com sucesso.\n");
    } else if (resposta.comando == USERNAME_EXISTE) {
        printf("Erro: Username já está em uso. Por favor, escolha outro.\n");
        close(manager_fd);
        unlink(feed_pipe);
        exit(1);
    } else if (resposta.comando == LIMITE_UTILIZADORES) {
        printf(
            "Erro: Limite de utilizadores atingido. Tente novamente mais "
            "tarde.\n");
        close(manager_fd);
        unlink(feed_pipe);
        exit(1);
    } else {
        printf("Erro ao estabelecer ligação.\n");
        close(manager_fd);
        unlink(feed_pipe);
        exit(1);
    }

    // Criar thread para ouvir mensagens do manager
    pthread_t listen_thread;
    pthread_create(&listen_thread, NULL, escuta_mensagens, (void *)feed_pipe);

    // Registrar função de cleanup para ser chamada em qualquer tipo de saída
    atexit(limpar_e_sair);

    signal(SIGINT, limpar_e_sair);   // Ctrl+C
    signal(SIGTERM, limpar_e_sair);  // Terminar processo
    signal(SIGPIPE, limpar_e_sair);  // Pipe fechado
    signal(SIGHUP, limpar_e_sair);   // Terminal fechado
    signal(SIGABRT, limpar_e_sair);  // Abortar processo

    while (1) {
        printf("Comando> ");
        if (fgets(comando, sizeof(comando), stdin) == NULL) {
            // Terminal foi fechado (EOF), executar cleanup
            limpar_e_sair();
            break;
        }
        if (comando[strlen(comando) - 1] != '\n') {
            limpar_buffer();
        }

        if (strncmp(comando, "msg", 3) == 0) {
            // Dividir o comando em partes
            char *cmd = strtok(comando, " ");  // Pega "msg"
            char *top = strtok(NULL, " ");     // Pega topico
            char *dur = strtok(NULL, " ");     // Pega duracao
            char *msg = strtok(NULL, "\n");    // Pega resto da mensagem

            if (!top || !dur || !msg) {
                printf(
                    "Erro: Comando incorreto. Uso: msg <topico> <duracao> "
                    "<mensagem>\n");
                continue;
            }

            int duracao_valida = 1;
            for (int i = 0; dur[i] != '\0'; i++) {
                if (!isdigit(dur[i])) {
                    printf("Erro: Duração deve ser um número inteiro\n");
                    duracao_valida = 0;
                    break;
                }
            }

            if (!duracao_valida) {
                continue;
            }

            // Converter duracao para inteiro
            duracao = atoi(dur);

            // Copiar topico e mensagem com verificação de tamanho
            strncpy(topico, top, MAX_TOPICO_NOME - 1);
            topico[MAX_TOPICO_NOME - 1] = '\0';

            strncpy(mensagem, msg, MAX_MSG_LEN - 1);
            mensagem[MAX_MSG_LEN - 1] = '\0';
            printf("%d\n", duracao);
            if (duracao < 0) {
                printf("Erro: A duração da mensagem tem de ser positiva.\n");
                continue;
            } else {
                if (duracao > 0) {
                    persistente = 1;
                } else {
                    persistente = 0;
                }
                envia_mensagens(username, topico, mensagem, persistente,
                                duracao);
            }

        } else if (strncmp(comando, "subscribe", 9) == 0) {
            sscanf(comando, "subscribe %s", topico);
            subscreve_topico(username, topico);
        } else if (strncmp(comando, "unsubscribe", 11) == 0) {
            sscanf(comando, "unsubscribe %s", topico);
            anula_topico(username, topico);
        } else if (strncmp(comando, "exit", 4) == 0) {
            // Enviar comando EXIT ao manager
            sair_plataforma(username);
            // Em seguida, limpar recursos e encerrar
            limpar_e_sair();
            break;
        } else if (strncmp(comando, "topics", 6) == 0) {
            mostra_topicos();
        } else {
            printf("Comando desconhecido: %s", comando);
        }
    }

    limpar_e_sair();  // Garantir cleanup mesmo se sair do loop
    return 0;
}

void *escuta_mensagens(void *arg) {
    char *feed_pipe = (char *)arg;  // Nome do pipe do Feed
    int fd;
    Mensagem msg;

    // Abre o pipe do Feed em modo leitura não bloqueante
    fd = open(feed_pipe, O_RDONLY | O_NONBLOCK);
    if (fd == -1) {
        perror("Erro ao abrir pipe");
        pthread_exit(NULL);
    }

    while (1) {
        ssize_t read_bytes = read(fd, &msg, sizeof(msg));
        if (read_bytes > 0) {
            if (msg.comando == MSG && strcmp(msg.username, "Sistema") == 0) {
                // Mensagens de erro ou notificações enviadas pelo sistema
                printf("\n[Erro] %s\n", msg.mensagem);
                printf("Comando> ");
                fflush(stdout);
            } else if (msg.comando == LIMITE_PERSISTENTES) {
                // Mensagens de erro ou notificações enviadas pelo sistema
                printf("\n[Erro] %s\n", msg.mensagem);
                printf("Comando> ");
                fflush(stdout);
            }

            else if (msg.comando == MSG) {
                // Exibe mensagens regulares recebidas de tópicos
                printf(
                    "\n[Nova mensagem] Tópico: %s, Autor: %s\nMensagem: %s\n",
                    msg.topico, msg.username, msg.mensagem);
                printf("Comando> ");
                fflush(stdout);

            } else if (msg.comando == MSG_TOPICO_BLOQUEADO) {
                // Mensagem recebida informando que o tópico está bloqueado
                printf(
                    "\n[Erro] O tópico %s está bloqueado. A mensagem não foi "
                    "enviada.\n",
                    msg.topico);
                printf("Comando> ");
                fflush(
                    stdout);  // Garante que o prompt seja exibido imediatamente
            } else if (msg.comando == TOPICO_BLOQUEADO) {
                // Mensagem recebida informandO que o tópico está bloqueado
                printf("\nO tópico %s foi bloqueado\n", msg.topico);
                printf("Comando> ");
                fflush(
                    stdout);  // Garante que o prompt seja exibido imediatamente
            }

            else if (msg.comando == TOPICO_DESBLOQUEADO) {
                // Mensagem recebida informando que o tópico está desbloqueado
                printf("\nO tópico %s foi desbloqueado\n", msg.topico);
                printf("Comando> ");
                fflush(
                    stdout);  // Garante que o prompt seja exibido imediatamente
            }

            else if (msg.comando == REMOVE_UTILIZADOR) {
                if (strcmp(feed_pipe, msg.pipe_name) == 0) {
                    printf(
                        "\nVocê foi removido da plataforma pelo "
                        "administrador.\n");
                    limpar_e_sair();
                } else {
                    printf(
                        "\n[Sistema] Utilizador %s foi removido da "
                        "plataforma.\n",
                        msg.username);
                    printf("Comando> ");
                    fflush(stdout);
                }
            } else if (msg.comando == FECHA_SISTEMA) {
                printf("\nO sistema foi encerrado pelo administrador.\n");
                limpar_e_sair();
            }
        } else if (read_bytes == -1 && errno != EAGAIN &&
                   errno != EWOULDBLOCK) {
            perror("Erro ao ler do pipe");
        }
        usleep(10000);  // Evita alto consumo de CPU
    }

    close(fd);  // Fecha o pipe no final
    return NULL;
}

void envia_mensagens(char *username, char *topico, char *mensagem,
                     int persistente, int duracao) {
    if (strlen(topico) > MAX_TOPICO_NOME) {
        printf("Erro: Nome do tópico muito longo (máx. %d caracteres)\n",
               MAX_TOPICO_NOME);
        return;
    }

    size_t msg_len = strlen(mensagem);
    if (msg_len > MAX_MSG_LEN - 1) {
        printf("Erro: Mensagem muito longa (máx. %d caracteres)\n",
               MAX_MSG_LEN);
        return;
    }

    // Prepara a estrutura da mensagem
    Mensagem msg = {0};  // Inicializa toda a estrutura com zeros
    msg.comando = MSG;
    strncpy(msg.topico, topico, MAX_TOPICO_NOME - 1);
    strncpy(msg.username, username, MAX_USERNAME - 1);
    msg.persistente = persistente;
    msg.tempo_de_vida = duracao;
    strncpy(msg.mensagem, mensagem, MAX_MSG_LEN - 1);
    snprintf(msg.pipe_name, sizeof(msg.pipe_name), "%s_pipe", username);

    pthread_mutex_lock(&lock);  // Garante sincronização no envio da mensagem
    if (write(manager_fd, &msg, sizeof(Mensagem)) != sizeof(Mensagem)) {
        perror("Erro ao enviar mensagem para o Manager");
    } else {
        printf("Mensagem enviada para validação pelo Manager.\n");
    }
    pthread_mutex_unlock(&lock);
}

// Subscreve topico
void subscreve_topico(char *username, char *topico) {
    Mensagem msg = {0};
    msg.comando = SUBSCRIBE;
    strcpy(msg.topico, topico);
    strcpy(msg.username, username);               // Username do utilizador
    sprintf(msg.pipe_name, "%s_pipe", username);  // Username do pipe

    pthread_mutex_lock(&lock);
    if (write(manager_fd, &msg, sizeof(msg)) != sizeof(msg)) {
        perror("Erro ao enviar comando de subscrição");
    } else {
        printf("Subscrição ao tópico %s efetuada com sucesso.\n", topico);
    }
    pthread_mutex_unlock(&lock);
}
// Anula subscricao topico
void anula_topico(char *username, char *topico) {
    Mensagem msg;
    msg.comando = UNSUBSCRIBE;
    strcpy(msg.topico, topico);
    strcpy(msg.username, username);

    pthread_mutex_lock(&lock);
    if (write(manager_fd, &msg, sizeof(msg)) != sizeof(msg)) {
        perror("Erro ao enviar comando de cancelamento");
    } else {
        printf("Cancelamento da subscrição ao tópico %s enviado.\n", topico);
    }
    pthread_mutex_unlock(&lock);
}

void sair_plataforma(char *username) {
    Mensagem msg;
    msg.comando = EXIT;
    strcpy(msg.username, username);

    pthread_mutex_lock(&lock);
    if (write(manager_fd, &msg, sizeof(msg)) != sizeof(msg)) {
        perror("Erro ao enviar comando de saída");
    } else {
        printf("Comando de saída enviado.\n");
    }
    pthread_mutex_unlock(&lock);
}

void mostra_topicos() {
    Mensagem msg;
    msg.comando = MOSTRA_TOPICOS;
    strcpy(msg.username, username);
    sprintf(msg.pipe_name, "%s_pipe", username);

    pthread_mutex_lock(&lock);
    if (write(manager_fd, &msg, sizeof(msg)) != sizeof(msg)) {
        perror("Erro ao enviar comando de listagem de tópicos");
        printf("Erro ao listar tópicos.\n");
    } else {
        printf("Aguardando lista de tópicos...\n");
    }
    pthread_mutex_unlock(&lock);

    // Receber a lista consolidada de tópicos
    char pipe_name[50];
    sprintf(pipe_name, "%s_pipe", username);

    int fd = open(pipe_name, O_RDONLY);
    if (fd == -1) {
        perror("Erro ao abrir pipe do feed");
        return;
    }

    Mensagem resposta;
    read(fd, &resposta, sizeof(Mensagem));  // Lê a mensagem consolidada
    close(fd);

    printf("%s", resposta.mensagem);  // Exibe a mensagem recebida
}
