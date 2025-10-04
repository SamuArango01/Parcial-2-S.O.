#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <pthread.h>

#define MAX_TEXTO  256
#define MAX_NOMBRE 64

enum { T_JOIN=1, T_CONFIRM=2, T_MSG=3, T_LIST=4, T_USERS=5, T_LEAVE=6 };

struct mensaje {
    long mtype; // Esto siempre va a ser 1 porque el System V lo pide pero no se usa para nada lit
    int  type; // Los tipos de mensajes son estos de arriba (T_JOIN, T_MSG, etc.)     
    char remitente[MAX_NOMBRE]; // nombre usuario
    char sala[MAX_NOMBRE]; // nombre de la sala
    char texto[MAX_TEXTO]; // lo que se envio 
    int  q_personal; // id de cola personal del cliente
    int  q_sala; // id de cola de la sala (para ACK de JOIN) el ACK es cuando te unes a una sala
};

static int cola_global = -1; // id de la cola global del servidor (empieza en -1 porque no hay ninguna obvio)
static int cola_personal = -1; // id de la cola personal del cliente (empieza en -1 porque no hay ninguna)
static int cola_sala_actual = -1; // id de la cola de la sala en la que está el cliente (empieza en -1 porque no hay ninguna)
static char usuario[MAX_NOMBRE]; // el nombre del usuario
static char sala_actual[MAX_NOMBRE] = ""; // el nombre de la sala en la que está el cliente (empieza en vacio porque no hay ninguna)
static volatile sig_atomic_t running = 1; // Esto es para cuando se pare de correr el codigo elimine las colas de mensajes y no queden ocupando memoria

static void trim(char *s){  // toda esta funcion se encarga de borrar espacios en blanco al inicio y final
    // ademas hace que no se envie mensajes en blanco o con un salto de linea
    char *p = s; 
    while (*p && isspace((unsigned char)*p)) p++; // 
    if (p != s) memmove(s, p, strlen(p)+1);
    // derecha
    size_t n = strlen(s);
    while (n>0 && isspace((unsigned char)s[n-1])) n--;
    s[n] = 0;
}

static void enviar_global(int type, const char *sala, const char *texto){ // esta funcion se encarga de enviar mensajes a la cola global del servidor
    struct mensaje msg = {0}; // inicializa el mensaje
    msg.mtype = 1; // siempre es 1 porque el System V lo pide
    msg.type  = type; // Se ve que tipo de mensaje que se va a enviar
    snprintf(msg.remitente, MAX_NOMBRE, "%s", usuario); // se copia el nombre del usuario al mensaje
    if (sala)  snprintf(msg.sala, MAX_NOMBRE, "%s", sala); // si hay sala se copia al mensaje
    if (texto) snprintf(msg.texto, MAX_TEXTO,  "%s", texto); // si hay texto se copia al mensaje
    msg.q_personal = cola_personal; // Se guarda el id de la cola personal del cliente en el mensaje
    msgsnd(cola_global, &msg, sizeof(struct mensaje) - sizeof(long), 0); // se envia el mensaje a la cola global del servidor
}

static void enviar_sala(const char *texto){ // Esta funcion se encarga de enviar mensajes a la cola de la sala en la que esta el cliente
    if (cola_sala_actual == -1 || sala_actual[0] == '\0') { // si no hay sala o no hay cola de sala
        printf("No estás en ninguna sala.\n"); // pues se le avisa al usuario
        return;
    }
    struct mensaje msg = {0}; // se inicializa el mensaje
    msg.mtype = 1; // siempre es 1 porque el System V lo pide
    msg.type  = T_MSG; // el tipo de mensaje es T_MSG porque es un mensaje normal de texto
    snprintf(msg.remitente, MAX_NOMBRE, "%s", usuario); // se copia el nombre del usuario al mensaje
    snprintf(msg.sala,      MAX_NOMBRE, "%s", sala_actual); // se copia el nombre de la sala al mensaje
    snprintf(msg.texto,     MAX_TEXTO,  "%s", texto); // se copia el texto al mensaje
    msgsnd(cola_sala_actual, &msg, sizeof(struct mensaje) - sizeof(long), 0); // se envia el mensaje a la cola de la sala en la que esta el cliente
}

static void *receptor(void *arg){ // esta funcion se encarga de recibir mensajes de la cola personal del cliente y mostrarlos por pantalla 
    (void)arg; // Sin esto cuando usamos el pthread_create su interfaz debe recibir un void* pero no lo usamos asi se evita la advertencia del compilador
    struct mensaje in; // se crea una variable de tipo mensaje para recibir los mensajes
    while (running) { // running es la variable de volatile sig_atomic_t que se usa para saber si el programa sigue corriendo o no
        ssize_t r = msgrcv(cola_personal, &in, sizeof(struct mensaje) - sizeof(long), 0, 0); // se recibe el mensaje de la cola personal del cliente
        //la interfaz de msgrcv es (int msgrcv(int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg);
        //int msqid: es el id de la cola de mensajes, void *msgp: es un puntero donde se va a guardar el mensaje recibido, size_t msgsz: es el tamaño del mensaje, long msgtyp: es el tipo de mensaje que se quiere recibir (0 para recibir cualquier tipo), int msgflg: son las opciones para recibir el mensaje (0 para bloquear hasta que llegue un mensaje)
        if (r == -1) continue; // si hay un error al recibir el mensaje se continua con el siguiente ciclo

        if (in.type == T_CONFIRM) { // si el tipo de mensaje es T_CONFIRM es porque es una confirmación de que se ha unido a una sala, ejemplo "Te has unido a la sala tin"
            // Si es confirmación de JOIN, viene q_sala
            if (in.q_sala > 0) { // si el id de la cola de la sala es mayor que 0
                cola_sala_actual = in.q_sala; // se guarda el id de la cola de la sala en la variable global cola_sala_actual
            }
            printf("\n%s\n> ", in.texto); // se imprime ese mensaje de confirmación por pantalla
            fflush(stdout); // esto hace que si o si se muestre el mensaje y no quede en el buffer(buffer es la memoria temporal donde se almacenan datos mientras se mueven de un lugar a otro)
        } else if (in.type == T_MSG) { // si el tipo de mensaje es T_MSG es porque es un mensaje normal de texto
            printf("\n%s: %s\n> ", in.remitente, in.texto); // se imprime el mensaje por pantalla con el formato "remitente: texto"
            fflush(stdout); // lo mismo que el otro 
        }
    }
    return NULL;
}

static void salir_ordenado(int code){ // esta funcion se encarga de salir del programa de forma ordenada, eliminando las colas de mensajes y avisando al servidor que el cliente se va
    if (sala_actual[0] != '\0') { // si hay una sala actual
        enviar_global(T_LEAVE, sala_actual, NULL); // se envia un mensaje al servidor avisando que el cliente se va de la sala
    }
    if (cola_personal > 0) { // si hay una cola personal
        msgctl(cola_personal, IPC_RMID, NULL); // se elimina la cola personal
    }
    running = 0; // Aca ponemos running en 0 porque queremos que el receptor deje de correr 
    exit(code);
}

static void sigint_handler(int sig){ // esta funcion se encarga de manejar la señal SIGINT (Ctrl+C) para salir del programa de forma ordenada
    (void)sig; // Sin esto cuando usamos signal su interfaz debe recibir un int pero no lo usamos asi se evita la advertencia del compilador
    salir_ordenado(0); 
}

int main(int argc, char **argv){ 
    if (argc != 2) { // si no se pasa el nombre de usuario por linea de comandos (o sea ./cliente Samuel)
        fprintf(stderr, "Uso: %s <nombre_usuario>\n", argv[0]); // se muestra un mensaje de error
        return 1;
    }
    strncpy(usuario, argv[1], MAX_NOMBRE-1); // se copia el nombre de usuario a la variable global usuario
    usuario[MAX_NOMBRE-1] = '\0'; 
    signal(SIGINT, sigint_handler);

    key_t key_global = ftok("/tmp", 'A'); // ftok genera una clave unica para la cola de mensajes global
    if (key_global == -1) { perror("ftok"); return 1; } // si de pronto hay un error al generar la clave se muestra un mensaje de error

    cola_global = msgget(key_global, 0666); // mssget se conecta a la cola global del servidor
    if (cola_global == -1) { perror("msgget cola_global"); return 1; } // si de pronto hay un error al conectar a la cola se muestra un mensaje de error

    cola_personal = msgget(IPC_PRIVATE, IPC_CREAT | 0666); // se crea una cola de mensajes personal para el cliente (el 0666 son los permisos de lectura y escritura)
    if (cola_personal == -1) { perror("msgget cola_personal"); return 1; }

    pthread_t th; 
    pthread_create(&th, NULL, receptor, NULL);

    enviar_global(T_LIST, NULL, NULL); // se envia un mensaje al servidor pidiendo la lista de salas disponibles

    printf("Bienvenido, %s. Comandos: \n", usuario); // se muestra un mensaje de bienvenida
    printf("  join <Sala>   | /list   | /users   | /leave   | <texto>\n"); // se muestra un mensaje con los comandos disponibles

    char linea[512]; // se crea un buffer para leer las lineas de texto que el usuario va a escribir
    while (running) { // mientras el programa este corriendo
        printf("> "); 
        if (!fgets(linea, sizeof(linea), stdin)) break; // se lee una linea de texto del usuario
        linea[strcspn(linea, "\n")] = 0; // se elimina el salto de linea al final de la linea
        trim(linea); // se eliminan los espacios en blanco al inicio y final de la linea
        if (strlen(linea) >= MAX_TEXTO) { // si la linea es mas larga que el maximo permitido
            linea[MAX_TEXTO - 1] = '\0'; // se recorta la linea al maximo permitido
            printf("(Aviso) Mensaje recortado a %d caracteres.\n", MAX_TEXTO - 1);
        }

        if (linea[0] == '\0') continue; // si la linea esta vacia se continua con la siguiente iteracion del ciclo

        if (strncmp(linea, "join ", 5) == 0) { // si la linea empieza con "join " es porque el usuario quiere unirse a una sala
            char sala[MAX_NOMBRE]; memset(sala, 0, sizeof(sala)); // se crea un buffer para el nombre de la sala y se inicializa a 0
            sscanf(linea+5, "%63s", sala); // se lee el nombre de la sala, el linea+5 es para saltarse el "join " que son 5 caracteres
            if (sala[0] == '\0') { printf("Uso: join <Sala>\n"); continue; } // si no se especifico una sala se muestra un mensaje de error
            strncpy(sala_actual, sala, MAX_NOMBRE-1); // se copia el nombre de la sala a la variable global sala_actual
            sala_actual[MAX_NOMBRE-1] = '\0';
            cola_sala_actual = -1; // se pone la cola de la sala actual en -1 para indicar que no se ha unido a ninguna sala aun
            enviar_global(T_JOIN, sala_actual, NULL); // se envia un mensaje al servidor pidiendo unirse a la sala
        }
        else if (strcmp(linea, "/list") == 0) { // si la linea es "/list" es porque el usuario quiere ver la lista de salas disponibles
            enviar_global(T_LIST, NULL, NULL); // se envia un mensaje al servidor pidiendo la lista de salas disponibles
        }
        else if (strcmp(linea, "/users") == 0) { // si la linea es "/users" es porque el usuario quiere ver la lista de usuarios en la sala actual
            if (sala_actual[0] == '\0') { printf("No estás en ninguna sala.\n"); continue; } // si no hay una sala actual se muestra un mensaje de error
            enviar_global(T_USERS, sala_actual, NULL); // se envia un mensaje al servidor pidiendo la lista de usuarios en la sala actual
        }
        else if (strcmp(linea, "/leave") == 0) { // si la linea es "/leave" es porque el usuario quiere salir de la sala actual
            if (sala_actual[0] == '\0') { printf("No estás en ninguna sala.\n"); continue; } // si no hay una sala actual se muestra un mensaje de error
            enviar_global(T_LEAVE, sala_actual, NULL); // se envia un mensaje al servidor avisando que el cliente se va de la sala
            printf("Has salido de la sala %s\n", sala_actual); 
            sala_actual[0] = '\0';
            cola_sala_actual = -1;
        }
        else {
            if (sala_actual[0] == '\0') { // si no hay una sala actual
                printf("No estás en ninguna sala. Usa: join <Sala>\n");
                continue;
            }
            if (cola_sala_actual == -1) {
                printf("Aún no llega la confirmación de la sala. Intenta de nuevo en un momento.\n");
                continue;
            }
            enviar_sala(linea); // se envia el mensaje a la sala actual
        }
    }

    salir_ordenado(0); // se sale del programa
    return 0;
}
