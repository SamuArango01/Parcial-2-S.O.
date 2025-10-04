#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <pthread.h>

#define MAX_SALAS  16
#define MAX_USUARIOS_POR_SALA 64
#define MAX_TEXTO  256
#define MAX_NOMBRE 64

enum { T_JOIN=1, T_CONFIRM=2, T_MSG=3, T_LIST=4, T_USERS=5, T_LEAVE=6 };

struct mensaje {
    long mtype;                   
    int  type;                    
    char remitente[MAX_NOMBRE];   
    char sala[MAX_NOMBRE];        
    char texto[MAX_TEXTO];        
    int  q_personal;              
    int  q_sala;                 
};

struct sala {
    char nombre[MAX_NOMBRE];
    int cola_sala;  
    int num_usuarios;
    char usuarios[MAX_USUARIOS_POR_SALA][MAX_NOMBRE];
    int  q_personales[MAX_USUARIOS_POR_SALA]; 
};

static struct sala salas[MAX_SALAS];
static int num_salas = 0;
static int cola_global = -1; 

static pthread_t hilos_sala[MAX_SALAS];
static int idx_sala_arg[MAX_SALAS];
static volatile sig_atomic_t running = 1;

static void limpiar_recursos(int code);

static int buscar_sala(const char *nombre) {
    for (int i = 0; i < num_salas; i++) {
        if (strcmp(salas[i].nombre, nombre) == 0) return i;
    }
    return -1;
}

static int agregar_usuario(int is, const char *usuario, int q_personal) {
    struct sala *s = &salas[is];
    if (s->num_usuarios >= MAX_USUARIOS_POR_SALA) return -1;
    for (int i=0; i<s->num_usuarios; i++) {
        if (strcmp(s->usuarios[i], usuario) == 0) return 0; 
    }
    snprintf(s->usuarios[s->num_usuarios], MAX_NOMBRE, "%s", usuario);
    s->q_personales[s->num_usuarios] = q_personal;
    s->num_usuarios++;
    return 0;
}

static void quitar_usuario(int is, const char *usuario) {
    struct sala *s = &salas[is];
    for (int i=0; i<s->num_usuarios; i++) {
        if (strcmp(s->usuarios[i], usuario) == 0) {
            for (int j=i; j<s->num_usuarios-1; j++) {
                strcpy(s->usuarios[j], s->usuarios[j+1]);
                s->q_personales[j] = s->q_personales[j+1];
            }
            s->num_usuarios--;
            break;
        }
    }
}

static void *hilo_sala(void *arg) {
    int is = *(int*)arg;
    struct sala *s = &salas[is];
    struct mensaje msg;

    char historial[128];
    snprintf(historial, sizeof(historial), "historial_%s.txt", s->nombre);

    while (running) {
        ssize_t r = msgrcv(s->cola_sala, &msg, sizeof(struct mensaje) - sizeof(long), 0, 0);
        if (r == -1) continue;

        if (msg.type == T_MSG) {
            for (int i=0; i<s->num_usuarios; i++) {
                if (strcmp(s->usuarios[i], msg.remitente) == 0) continue;
                msg.mtype = 1;
                msg.type  = T_MSG;
                msgsnd(s->q_personales[i], &msg, sizeof(struct mensaje) - sizeof(long), 0);
            }
            FILE *f = fopen(historial, "a");
            if (f) {
                fprintf(f, "[%s] %s: %s\n", s->nombre, msg.remitente, msg.texto);
                fclose(f);
            }
            printf("[%s] %s: %s\n", s->nombre, msg.remitente, msg.texto);
            fflush(stdout);
        }
    }
    return NULL;
}

static int crear_sala(const char *nombre) {
    if (num_salas >= MAX_SALAS) return -1;
    struct sala *s = &salas[num_salas];
    memset(s, 0, sizeof(*s));
    snprintf(s->nombre, MAX_NOMBRE, "%s", nombre);


    int q = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
    if (q == -1) return -1;
    s->cola_sala = q;

    int idx = num_salas++;
    idx_sala_arg[idx] = idx;
    pthread_create(&hilos_sala[idx], NULL, hilo_sala, &idx_sala_arg[idx]);

    printf("Sala creada: %s (q=%d)\n", s->nombre, s->cola_sala);
    fflush(stdout);
    return idx;
}

static void sigint_handler(int sig){
    (void)sig;
    running = 0;
    limpiar_recursos(0);
    exit(0);
}

static void limpiar_recursos(int code){
    for (int i=0; i<num_salas; i++) {
        if (salas[i].cola_sala > 0) msgctl(salas[i].cola_sala, IPC_RMID, NULL);
    }
    if (cola_global > 0) msgctl(cola_global, IPC_RMID, NULL);
    if (code) exit(code);
}

int main() {
    signal(SIGINT, sigint_handler);

    key_t key_global = ftok("/tmp", 'A');
    if (key_global == -1) {
        perror("ftok global");
        exit(1);
    }
    cola_global = msgget(key_global, IPC_CREAT | 0666);
    if (cola_global == -1) {
        perror("msgget cola_global");
        exit(1);
    }

    printf("Servidor de chat iniciado. Esperando clientes...\n");
    fflush(stdout);

    struct mensaje msg;
    while (running) {
        ssize_t r = msgrcv(cola_global, &msg, sizeof(struct mensaje) - sizeof(long), 0, 0);
        if (r == -1) continue;

        if (msg.type == T_JOIN) {
            int is = buscar_sala(msg.sala);
            if (is == -1) is = crear_sala(msg.sala);
            if (is == -1) {
                struct mensaje err = { .mtype=1, .type=T_CONFIRM };
                snprintf(err.texto, sizeof(err.texto), "ERROR: no se pudo crear/unir a sala.");
                msgsnd(msg.q_personal, &err, sizeof(err) - sizeof(long), 0);
                continue;
            }
            agregar_usuario(is, msg.remitente, msg.q_personal);

            struct mensaje ack = {0};
            ack.mtype = 1;
            ack.type  = T_CONFIRM;
            snprintf(ack.remitente, MAX_NOMBRE, "%s", "SERVER");
            snprintf(ack.sala, MAX_NOMBRE, "%s", salas[is].nombre);
            snprintf(ack.texto, sizeof(ack.texto), "Te has unido a la sala: %s", salas[is].nombre);
            ack.q_sala = salas[is].cola_sala;
            msgsnd(msg.q_personal, &ack, sizeof(ack) - sizeof(long), 0);

            printf("Usuario %s entró a %s (q_sala=%d)\n", msg.remitente, salas[is].nombre, salas[is].cola_sala);
            fflush(stdout);
        }
        else if (msg.type == T_LIST) {
            struct mensaje resp = { .mtype=1, .type=T_CONFIRM };
            snprintf(resp.remitente, MAX_NOMBRE, "%s", "SERVER");
            strcpy(resp.texto, "Salas disponibles:\n");
            for (int i=0; i<num_salas; i++) {
                strncat(resp.texto, salas[i].nombre, sizeof(resp.texto)-strlen(resp.texto)-2);
                strncat(resp.texto, "\n", sizeof(resp.texto)-strlen(resp.texto)-1);
            }
            msgsnd(msg.q_personal, &resp, sizeof(resp) - sizeof(long), 0);
        }
        else if (msg.type == T_USERS) {
            int is = buscar_sala(msg.sala);
            struct mensaje resp = { .mtype=1, .type=T_CONFIRM };
            strncpy(resp.remitente, "SERVER", MAX_NOMBRE-1);

            if (is == -1) {
                snprintf(resp.texto, sizeof(resp.texto), "Sala '%s' no existe.", msg.sala);
            } else {
                snprintf(resp.texto, sizeof(resp.texto), "Usuarios en '%s':\n", salas[is].nombre);
                for (int i=0; i<salas[is].num_usuarios; i++) {
                    strncat(resp.texto, salas[is].usuarios[i], sizeof(resp.texto)-strlen(resp.texto)-2);
                    strncat(resp.texto, "\n", sizeof(resp.texto)-strlen(resp.texto)-1);
                }
            }
            msgsnd(msg.q_personal, &resp, sizeof(resp) - sizeof(long), 0);
        }
        else if (msg.type == T_LEAVE) {
            int is = buscar_sala(msg.sala);
            if (is != -1) {
                quitar_usuario(is, msg.remitente);
                printf("Usuario %s salió de %s\n", msg.remitente, msg.sala);
                fflush(stdout);
            }
        }
    }

    limpiar_recursos(0);
    return 0;
}
