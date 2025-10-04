# Parcial-2-S.O.

Perfecto ✅
Aquí tienes el contenido **listo para copiar y pegar** directamente en tu `README.md` de GitHub.
Incluye formato Markdown, emojis y se ve bien tanto en modo claro como oscuro 👇

---

````markdown
# 💬 Chat por Colas de Mensajes System V (C)

**AUTORES:** Alyson Henao, Emily Cardona, Samuel Moncada y Samuel Arango

---

## 📌 Descripción

Este proyecto implementa un **chat multi-sala** en C usando **colas de mensajes System V**.  
- Un **servidor** central administra las **salas**, mantiene la **lista de usuarios** conectados por sala y **retransmite** los mensajes.  
- Cada **cliente** crea su **cola personal** para recibir mensajes, se conecta al **servidor global** y puede **unirse a salas**, listar salas, listar usuarios y **enviar mensajes** a su sala activa.

---

## 🧱 Arquitectura

- **Cola Global del Servidor** (`ftok("/tmp", 'A')`): punto de encuentro para JOIN, LIST, USERS, LEAVE.  
- **Cola por Sala:** cada sala tiene su cola propia para mensajería en tiempo real.  
- **Cola Personal del Cliente:** creada con `IPC_PRIVATE`, usada para recibir mensajes directos y confirmaciones.  

**Hilos:**
- Servidor → un **hilo por sala** que retransmite los mensajes.
- Cliente → un **hilo receptor** que escucha su **cola personal**.

**Persistencia de historial:**
Cada sala genera su propio archivo `historial_<sala>.txt` (por ejemplo, `historial_Saka.txt`).

---

## 🧾 Estructura de mensajes

```c
struct mensaje {
    long mtype;          // tipo de mensaje (System V)
    int  type;           // T_JOIN, T_CONFIRM, T_MSG, T_LIST, T_USERS, T_LEAVE
    char remitente[64];  // nombre del usuario
    char sala[64];       // nombre de la sala
    char texto[256];     // contenido del mensaje
    int  q_personal;     // id de cola personal del cliente
    int  q_sala;         // id de cola de la sala (devuelto al cliente)
};
````

**Códigos `type`:**

| Tipo        | Significado                           |
| ----------- | ------------------------------------- |
| `T_JOIN`    | Solicitud de unión o creación de sala |
| `T_CONFIRM` | Confirmación del servidor             |
| `T_MSG`     | Mensaje de usuario                    |
| `T_LIST`    | Listado de salas                      |
| `T_USERS`   | Listado de usuarios                   |
| `T_LEAVE`   | Notificación de salida                |

---

## 💻 Comandos del cliente

| Comando       | Descripción                            |
| ------------- | -------------------------------------- |
| `join <Sala>` | Crea o se une a una sala               |
| `/list`       | Lista todas las salas activas          |
| `/users`      | Muestra los usuarios de la sala actual |
| `/leave`      | Sale de la sala                        |
| `<texto>`     | Envía un mensaje al chat de la sala    |

> ⚠️ Si aún no llega la confirmación (`T_CONFIRM`), el cliente mostrará un aviso.

---

## ⚙️ Compilación

Requisitos: **GCC**, **pthread** y un sistema Unix/Linux con **System V IPC**.

```bash
gcc -Wall -O2 -pthread -o servidor servidor.c
gcc -Wall -O2 -pthread -o cliente  cliente.c
```

---

## ▶️ Ejecución

1️⃣ **Inicia el servidor**

```bash
./servidor
```

2️⃣ **Abre clientes (en otras terminales)**

```bash
./cliente Aly
./cliente Emily
./cliente Samu
```

3️⃣ **Chatea**

```
> join Saka
> Hola a todos
> /users
> /leave
```

> El servidor crea automáticamente la sala si no existe y guarda su historial en `historial_Saka.txt`.

---

## 🧠 Flujo de eventos (JOIN y chat)

1. Cliente crea su cola personal.
2. Envía `T_JOIN` al servidor global.
3. Servidor crea la sala (si no existe) y devuelve `T_CONFIRM` con su `q_sala`.
4. Cliente envía `T_MSG` a esa cola de sala.
5. Hilo de la sala (en servidor) retransmite a todos los usuarios de la sala y guarda el mensaje en el historial.

---

## 🗂️ Estructura del servidor

```c
struct sala {
    char nombre[64];
    int  cola_sala;
    int  num_usuarios;
    char usuarios[64][64];
    int  q_personales[64];
};
```

* Máximo 16 salas y 64 usuarios por sala (configurable).
* Cada sala lanza un **hilo independiente** que escucha mensajes `T_MSG`.

---

## 🛑 Señales y limpieza

* **Cliente:** captura `Ctrl+C`, envía `T_LEAVE` y borra su cola personal (`msgctl(IPC_RMID)`).
* **Servidor:** elimina todas las colas al cerrar (`SIGINT`).

---

## 📝 Logs e historiales

Cada sala genera un archivo de texto:

```
historial_<sala>.txt
```

Ejemplo (`historial_Saka.txt`):

```
[Saka] Aly: Hola
[Saka] Emily: ¿Cómo estás?
[Saka] Samu: Bien, gracias :)
```

---

## 🚑 Solución de problemas

| Problema                                  | Causa probable                         | Solución                                  |
| ----------------------------------------- | -------------------------------------- | ----------------------------------------- |
| “Aún no llega la confirmación de la sala” | Servidor no está ejecutándose          | Inicia el servidor antes del cliente      |
| Colas “huérfanas”                         | Cierre abrupto                         | `ipcs -q` y luego `ipcrm -q <id>`         |
| Permisos de `ftok`                        | `/tmp` inaccesible                     | Verifica que `/tmp` existe y es accesible |
| No se reciben mensajes                    | Diferente `ftok` en cliente y servidor | Usa la misma clave `'A'`                  |

---

## 🧩 Posibles mejoras

* Autenticación de usuarios.
* Avisos automáticos de JOIN/LEAVE en salas.
* Interfaz TUI o GUI para el cliente.
* Persistencia de usuarios y salas en archivo o base de datos.
* Soporte remoto con sockets TCP (gateway IPC↔TCP).

---

## 📁 Estructura del repositorio

```
.
├── README.md
├── servidor.c
├── cliente.c
└── historial_Saka.txt
```

---

## 🧪 Ejemplo rápido

**Servidor**

```bash
./servidor
```

**Cliente 1**

```bash
./cliente Ana
> join Sala1
> Hola!
```

**Cliente 2**

```bash
./cliente Luis
> join Sala1
> /users
> Hola Ana
```

**Salida esperada:**

```
Ana: Hola!
Luis: Hola Ana
```

**Servidor:**

```
Servidor iniciado...
Sala creada: Sala1
Usuario Ana entró a Sala1
Usuario Luis entró a Sala1
[Sala1] Ana: Hola!
[Sala1] Luis: Hola Ana
```

---

💡 **Proyecto desarrollado en C con colas de mensajes System V, hilos POSIX y comunicación asíncrona entre procesos.**

```

---

¿Quieres que le agregue al final una sección de “Créditos institucionales” o una firma tipo “Proyecto de Pensamiento Computacional - Universidad Nacional de Colombia”? Puedo formatearla para que quede lista para subir.
```
