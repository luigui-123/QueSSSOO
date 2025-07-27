#include <utils/hello.h>
#include <commons/log.h>
#include <commons/config.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <commons/collections/list.h>
#include <commons/collections/queue.h>
#include <pthread.h>
#include <string.h>
#include <semaphore.h>
#include <commons/temporal.h>
#include <commons/collections/dictionary.h>
#include <stdio.h>
#include <commons/string.h>
#include <inttypes.h>

#define PROCESO_NUEVO 6
#define SUSPENDER 7
#define DESUSPENDER 8
#define DUMP 9
#define FINALIZAR 10

struct pcb
{
    int PID;
    int PC;
    int64_t MT[7];
    int ME[7];
    int tamanio;
    char *path;
    double rafaga;
    t_temporal *tiempo_estado;
    sem_t mutex_rafaga;
};

// MT--> 0 New, 1 Ready, 2 Execute, 3 BLocked, 4 BLocked SUspended, 5 Ready suspended, 6 Exit

// Listas
t_list *lista_cpu;
t_list *lista_io;

// Misc
int contador_procesos;
t_config *config_kernel;
t_log *log_kernel;

// Queues de Procesos
t_queue *lista_new;
t_queue *lista_ready;
t_queue *lista_sus_ready;
t_queue *lista_execute;
t_queue *lista_bloqued;
t_queue *lista_sus_bloqued;
t_queue *lista_finished;

// Semaforos
sem_t semaforo_ready;
sem_t semaforo_bloqued;
sem_t semaforo_bloqued_sus;
sem_t semaforo_execute;
sem_t semaforo_new;
sem_t semaforo_ready_sus;

sem_t semaforo_cpu;
sem_t semaforo_io;

sem_t procesos_bloqueados;
sem_t cpu_disponibles;
sem_t procesos_creados;
sem_t proceso_desalojado;
sem_t memoria_ocupada;
sem_t procesos_listos;
sem_t proceso_terminando;

sem_t interrupciones;
struct Cpu
{
    int socket_dispatch;
    int socket_interrupt;
    char *id;
    struct pcb *proceso;
};
struct io
{
    int socket_io;
    char *nombre;
    sem_t usando_io; // IO Libre
    t_queue *espera;
};

struct peticion_io
{
    struct io *io_asociada;
    long tiempo;
    int pid;
};

t_config *iniciar_config(char *ruta)
{
    t_config *nuevo_config = config_create(ruta);
    return nuevo_config;
}

int peticion_memoria()
{
    char *puerto_memoria = config_get_string_value(config_kernel, "PUERTO_MEMORIA");
    char *ip_memoria = config_get_string_value(config_kernel, "IP_MEMORIA");
    int conexion_memoria = iniciar_conexion(ip_memoria, puerto_memoria);

    t_list *Handshake = recibir_paquete(conexion_memoria);

    list_destroy_and_destroy_elements(Handshake, free);

    return conexion_memoria;
}

void *comparar_rafaga(void *p1, void *p2)
{
    struct pcb *proceso1 = (struct pcb *)p1;
    struct pcb *proceso2 = (struct pcb *)p2;

    return proceso1->rafaga <= proceso2->rafaga ? proceso1 : proceso2;
}

struct Cpu *encontrar_cpu_especifica_en_ejecucion(t_list *lista, int pid)
{
    bool encontrar_proceso(void *elemento)
    {
        struct Cpu *cpu = (struct Cpu *)elemento;
        return cpu->proceso->PID == pid;
    }
    struct Cpu *cpu_buscada = ((struct Cpu *)list_find(lista, encontrar_proceso));
    return cpu_buscada;
}

bool buscar_disponible(void *elemento)
{
    struct Cpu *cpu_especifica = (struct Cpu *)elemento;
    return cpu_especifica->proceso == NULL;
}

void cambio_estado_ready(struct pcb *proceso)
{
    sem_wait(&semaforo_ready);
    queue_push(lista_ready, proceso);
    sem_post(&semaforo_ready);

    if (strcmp(config_get_string_value(config_kernel, "ALGORITMO_CORTO_PLAZO"), "SRT") == 0)
    {
        sem_wait(&semaforo_cpu);
        struct Cpu *cpu_libre = (struct Cpu *)list_find(lista_cpu, buscar_disponible);
        if (cpu_libre || list_is_empty(lista_execute->elements))
        {
            sem_post(&semaforo_cpu);
            sem_post(&procesos_listos);
            return;
        }

        struct pcb *proceso_a_desalojar = list_get_maximum(lista_execute->elements, comparar_rafaga);
        if (proceso->rafaga < proceso_a_desalojar->rafaga)
        {
            struct Cpu *cpu_a_desalojar = encontrar_cpu_especifica_en_ejecucion(lista_cpu, proceso_a_desalojar->PID);
            // Enviar cpu socket interrupt

            char *mensaje = "DESALOJAR";

            log_trace(log_kernel, "%d - Desalojado por algoritmo SJF/SRT", proceso_a_desalojar->PID);

            enviar_mensaje(mensaje, cpu_a_desalojar->socket_interrupt);
        }
        sem_post(&semaforo_cpu);
    }
    sem_post(&procesos_listos);
}

void cambio_estado_exit(struct pcb *proceso_a_terminar)
{
    proceso_a_terminar->ME[6] += 1;

    int conexion_memoria = peticion_memoria();

    t_paquete *paquete_proceso_terminado = crear_paquete();

    int *puntero_pid = malloc(sizeof(int));
    *puntero_pid = proceso_a_terminar->PID;

    int codigo = FINALIZAR;

    agregar_a_paquete(paquete_proceso_terminado, &codigo, sizeof(int));

    agregar_a_paquete(paquete_proceso_terminado, (void *)puntero_pid, sizeof(int));
    enviar_paquete(paquete_proceso_terminado, conexion_memoria);

    char *rta = recibir_mensaje(conexion_memoria);
    if (!strcmp(rta, "OK"))
    {
        free(rta);
        close(conexion_memoria);
        sem_post(&memoria_ocupada);

        log_trace(log_kernel, "%d - Finaliza el proceso", proceso_a_terminar->PID);

        // LOG innecesariamente largo del TP:
        log_trace(log_kernel,
                  "## (%d) - Métricas de estado: NEW (%d) (%02" PRId64 "), READY (%d) (%02" PRId64 "), EXECUTE (%d) (%02" PRId64 "), BLOCKED (%d) (%02" PRId64 "), SUSPENDED BLOCKED (%d) (%02" PRId64 "), SUSPENDED READY (%d) (%02" PRId64 ")",
                  proceso_a_terminar->PID,
                  proceso_a_terminar->ME[0], proceso_a_terminar->MT[0],
                  proceso_a_terminar->ME[1], proceso_a_terminar->MT[1],
                  proceso_a_terminar->ME[2], proceso_a_terminar->MT[2],
                  proceso_a_terminar->ME[3], proceso_a_terminar->MT[3],
                  proceso_a_terminar->ME[4], proceso_a_terminar->MT[4],
                  proceso_a_terminar->ME[5], proceso_a_terminar->MT[5]);

        queue_push(lista_finished, proceso_a_terminar);
        // temporal_destroy(proceso_a_terminar->tiempo_estado);

        eliminar_paquete(paquete_proceso_terminado);
    }
    else
        abort();
}

struct pcb *encontrar_proceso_especifico(t_queue *lista, int pid)
{
    bool encontrar_proceso(void *elemento)
    {
        struct pcb *proceso = (struct pcb *)elemento;
        return proceso->PID == pid;
    }
    struct pcb *proceso = (struct pcb *)list_find(lista->elements, encontrar_proceso);
    return proceso;
}

void syscall_init_procc(int tamanio, char *nombre)
{
    struct pcb *proceso_nuevo;

    proceso_nuevo = malloc(sizeof(struct pcb));
    if (tamanio < 0)
        return;

    if (!strcmp(nombre, ""))
        return;

    proceso_nuevo->PID = contador_procesos;
    proceso_nuevo->tamanio = tamanio;
    // proceso_nuevo->path = nombre;
    proceso_nuevo->path = string_duplicate(nombre);
    proceso_nuevo->PC = 0;

    for (int i = 0; i < 6; i++)
    {
        proceso_nuevo->ME[i] = 0;
        proceso_nuevo->MT[i] = 0;
    }

    proceso_nuevo->rafaga = config_get_double_value(config_kernel, "ESTIMACION_INICIAL");

    proceso_nuevo->tiempo_estado = temporal_create();
    proceso_nuevo->ME[0] += 1;
    sem_init(&proceso_nuevo->mutex_rafaga, 0, 1);

    sem_wait(&semaforo_new);
    queue_push(lista_new, proceso_nuevo);
    sem_post(&semaforo_new);

    contador_procesos += 1;

    log_trace(log_kernel, "%d Se crea el proceso - Estado: NEW", proceso_nuevo->PID);

    sem_post(&procesos_creados);
    return;
}

void syscall_dump_memory(int *pid_proceso)
{
    int pid = *pid_proceso;
    int conexion = peticion_memoria();
    t_paquete *paquete_dump = crear_paquete();

    int codigo = DUMP; // Codigo de Dump

    agregar_a_paquete(paquete_dump, &codigo, sizeof(int));

    agregar_a_paquete(paquete_dump, (void *)pid_proceso, sizeof(int));
    enviar_paquete(paquete_dump, conexion);
    free(pid_proceso);
    // Preguntar que significa "Error"
    eliminar_paquete(paquete_dump);

    char *mensaje = recibir_mensaje(conexion);

    if (!strcmp(mensaje, "NO"))
    {

        struct pcb *proceso_usado = encontrar_proceso_especifico(lista_bloqued, pid);
        if (proceso_usado != NULL)
        {
            log_trace(log_kernel, "%d - Pasa del estado Bloqueado al Estado Exit", pid);
            sem_wait(&semaforo_bloqued);
            list_remove_element(lista_bloqued->elements, proceso_usado);
            sem_post(&semaforo_bloqued);
            temporal_stop(proceso_usado->tiempo_estado);

            proceso_usado->MT[3] += temporal_gettime(proceso_usado->tiempo_estado);

            cambio_estado_exit(proceso_usado);
        }
        else
        {

            struct pcb *proceso_usado = encontrar_proceso_especifico(lista_sus_bloqued, pid);

            if (!proceso_usado)
                abort();

            sem_wait(&semaforo_bloqued_sus);
            list_remove_element(lista_sus_bloqued->elements, proceso_usado);
            sem_post(&semaforo_bloqued_sus);

            temporal_stop(proceso_usado->tiempo_estado);
            proceso_usado->MT[4] += temporal_gettime(proceso_usado->tiempo_estado);
            temporal_destroy(proceso_usado->tiempo_estado);
            proceso_usado->tiempo_estado = temporal_create();
            log_trace(log_kernel, "%d - Pasa del estado Suspendido Bloqueado al Estado Exit", proceso_usado->PID);

            cambio_estado_exit(proceso_usado);
        }
    }

    else
    {
        struct pcb *proceso_usado = encontrar_proceso_especifico(lista_bloqued, pid);

        if (proceso_usado != NULL)
        {
            sem_wait(&semaforo_bloqued);
            list_remove_element(lista_bloqued->elements, proceso_usado);
            sem_post(&semaforo_bloqued);
            temporal_stop(proceso_usado->tiempo_estado);

            proceso_usado->MT[3] += temporal_gettime(proceso_usado->tiempo_estado);
            log_trace(log_kernel, "%d - Pasa del estado Bloqueado al Estado Ready", pid);
            proceso_usado->ME[1] += 1;
            cambio_estado_ready(proceso_usado);
        }
        else
        {
            sem_wait(&semaforo_bloqued_sus);
            struct pcb *proceso_usado = encontrar_proceso_especifico(lista_sus_bloqued, pid);
            if (!proceso_usado)
                abort();
            list_remove_element(lista_sus_bloqued->elements, proceso_usado);

            sem_wait(&semaforo_ready_sus);
            queue_push(lista_sus_ready, proceso_usado);
            sem_post(&semaforo_ready_sus);

            sem_post(&semaforo_bloqued_sus);

            proceso_usado->ME[5] += 1;
            temporal_stop(proceso_usado->tiempo_estado);
            proceso_usado->MT[3] += temporal_gettime(proceso_usado->tiempo_estado);
            temporal_destroy(proceso_usado->tiempo_estado);
            proceso_usado->tiempo_estado = temporal_create();
            sem_post(&procesos_creados);
            log_trace(log_kernel, "%d - Pasa del estado Suspendido Bloqueado al Estado Suspended Ready", proceso_usado->PID);
        }
    }
    close(conexion);
}

void *syscall_io(void *peticion)
{
    struct peticion_io *peticion_nueva = (struct peticion_io *)peticion;

    sem_wait(&semaforo_io);
    queue_push((peticion_nueva->io_asociada->espera), peticion_nueva);
    sem_post(&semaforo_io);

    sem_post(&(peticion_nueva->io_asociada->usando_io));

    return NULL;
}

void *encontrar_proceso_pequeño(void *elemento, void *elemento2)
{
    struct pcb *proceso1 = (struct pcb *)elemento;
    struct pcb *proceso2 = (struct pcb *)elemento2;
    return proceso1->tamanio < proceso2->tamanio ? proceso1 : proceso2;
}

struct io *encontrar_io_especifico(t_list *lista, char *nombre)
{
    bool encontrar_io(void *elemento)
    {
        struct io *dispositivo = (struct io *)elemento;
        return strcmp(dispositivo->nombre, nombre) == 0;
    }
    sem_wait(&semaforo_io);
    struct io *dispositivo = list_find(lista, encontrar_io);
    sem_post(&semaforo_io);
    return dispositivo;
}


struct io *encontrar_io_libre(t_list *lista, char *nombre)
{
    bool encontrar_io(void *elemento)
    {
        struct io *dispositivo = (struct io *)elemento;
        int valor = -1;
        sem_getvalue(&dispositivo->usando_io, &valor);
        return (valor > 0 && strcmp(dispositivo->nombre, nombre) == 0);
    }
    sem_wait(&semaforo_io);
    struct io *dispositivo = list_find(lista, encontrar_io);
    sem_post(&semaforo_io);
    return dispositivo;
}

void planificador_io(struct io *io_asociada)
{
    while (1)
    {
        sem_wait(&(io_asociada->usando_io));

        sem_wait(&semaforo_io);
        struct peticion_io *peticion = (struct peticion_io *)queue_pop(io_asociada->espera);
        sem_post(&semaforo_io);
        t_paquete *paquete_io = crear_paquete(); // TODO Liberar
        int *puntero_pid = malloc(sizeof(int));
        *puntero_pid = peticion->pid;

        agregar_a_paquete(paquete_io, (void *)puntero_pid, sizeof(int));
        agregar_a_paquete(paquete_io, &peticion->tiempo, sizeof(int));
        if (enviar_paquete(paquete_io, peticion->io_asociada->socket_io))
        {
            free(puntero_pid);

            char* mensa=recibir_mensaje(peticion->io_asociada->socket_io);

            if (strcmp(mensa, "1"))
            {
                
                struct pcb *proceso_a_terminar = encontrar_proceso_especifico(lista_bloqued, peticion->pid);
                if (proceso_a_terminar)
                {
                    sem_wait(&semaforo_bloqued);
                    list_remove_element(lista_bloqued->elements, proceso_a_terminar);
                    sem_post(&semaforo_bloqued);
                    temporal_stop(proceso_a_terminar->tiempo_estado);

                    proceso_a_terminar->MT[3] += temporal_gettime(proceso_a_terminar->tiempo_estado);
                    temporal_destroy(proceso_a_terminar->tiempo_estado);
                    proceso_a_terminar->tiempo_estado = temporal_create();
                    log_trace(log_kernel, "%d - Pasa del estado Bloqued al Estado Exit", proceso_a_terminar->PID);
                }
                else
                {
                    proceso_a_terminar = encontrar_proceso_especifico(lista_sus_bloqued, peticion->pid);

                    sem_wait(&semaforo_bloqued_sus);
                    list_remove_element(lista_sus_bloqued->elements, proceso_a_terminar);
                    sem_post(&semaforo_bloqued_sus);
                    temporal_stop(proceso_a_terminar->tiempo_estado);

                    proceso_a_terminar->MT[4] += temporal_gettime(proceso_a_terminar->tiempo_estado);
                    temporal_destroy(proceso_a_terminar->tiempo_estado);
                    proceso_a_terminar->tiempo_estado = temporal_create();
                    log_trace(log_kernel, "%d - Pasa del estado Bloqued suspended al Estado Exit", proceso_a_terminar->PID);
                }
                cambio_estado_exit(proceso_a_terminar);
                continue;
            }
            // Agus free mensa
            free(mensa);

            log_trace(log_kernel, "%d - Finalizo IO y pasa a READY", peticion->pid);
            struct pcb *proceso = encontrar_proceso_especifico(lista_bloqued, peticion->pid);

            sem_wait(&semaforo_bloqued);
            if (proceso)
            {
                list_remove_element(lista_bloqued->elements, proceso);
                sem_post(&semaforo_bloqued);

                proceso->ME[1] += 1;
                temporal_stop(proceso->tiempo_estado);
                proceso->MT[3] += temporal_gettime(proceso->tiempo_estado);
                temporal_destroy(proceso->tiempo_estado);
                proceso->tiempo_estado = temporal_create();

                log_trace(log_kernel, "%d - Pasa del estado Bloqueado al Estado Ready", proceso->PID);
                cambio_estado_ready(proceso);
            }
            else
            {
                sem_post(&semaforo_bloqued);
                sem_wait(&semaforo_bloqued_sus);
                proceso = encontrar_proceso_especifico(lista_sus_bloqued, peticion->pid);
                if (!proceso)
                {
                    proceso = list_get(lista_bloqued->elements, 0);
                    abort();
                }
                list_remove_element(lista_sus_bloqued->elements, proceso);

                sem_wait(&semaforo_ready_sus);
                queue_push(lista_sus_ready, proceso);
                sem_post(&semaforo_ready_sus);

                sem_post(&semaforo_bloqued_sus);

                proceso->ME[5] += 1;
                temporal_stop(proceso->tiempo_estado);
                proceso->MT[3] += temporal_gettime(proceso->tiempo_estado);
                temporal_destroy(proceso->tiempo_estado);
                proceso->tiempo_estado = temporal_create();
                sem_post(&procesos_creados);
                log_trace(log_kernel, "%d - Pasa del estado Suspendido Bloqueado al Estado Suspended Ready", proceso->PID);
            }

            free(peticion);
            peticion = NULL;
        }
        else
        {
            sem_wait(&semaforo_io);
            list_remove_element(lista_io, peticion->io_asociada);
            sem_post(&semaforo_io);

            struct io *io_segunda = encontrar_io_especifico(lista_io, io_asociada->nombre);

            struct pcb *proceso = encontrar_proceso_especifico(lista_bloqued, peticion->pid);
            if (io_segunda)
            {
                if (proceso != NULL)
                {
                    sem_wait(&semaforo_bloqued);
                    list_remove_element(lista_bloqued->elements, proceso);
                    sem_post(&semaforo_bloqued);
                    temporal_stop(proceso->tiempo_estado);

                    proceso->MT[3] += temporal_gettime(proceso->tiempo_estado);
                    temporal_destroy(proceso->tiempo_estado);

                    proceso->tiempo_estado = temporal_create();
                }
                else
                {
                    sem_wait(&semaforo_bloqued_sus);
                    proceso = encontrar_proceso_especifico(lista_sus_bloqued, peticion->pid);
                    list_remove_element(lista_sus_bloqued->elements, proceso);
                    sem_post(&semaforo_bloqued_sus);
                    temporal_stop(proceso->tiempo_estado);

                    proceso->MT[4] += temporal_gettime(proceso->tiempo_estado);
                    temporal_destroy(proceso->tiempo_estado);

                    proceso->tiempo_estado = temporal_create();
                }
                cambio_estado_exit(proceso);
            }
            else
            {
                if (proceso != NULL)
                {

                    sem_wait(&semaforo_bloqued);
                    list_remove_element(lista_bloqued->elements, proceso);
                    sem_post(&semaforo_bloqued);
                    temporal_stop(proceso->tiempo_estado);

                    proceso->MT[3] += temporal_gettime(proceso->tiempo_estado);
                    proceso->tiempo_estado = temporal_create();
                }
                else
                {
                    sem_wait(&semaforo_bloqued_sus);
                    proceso = encontrar_proceso_especifico(lista_sus_bloqued, peticion->pid);

                    list_remove_element(lista_sus_bloqued->elements, proceso);
                    sem_post(&semaforo_bloqued_sus);
                    temporal_stop(proceso->tiempo_estado);

                    proceso->MT[4] += temporal_gettime(proceso->tiempo_estado);
                    proceso->tiempo_estado = temporal_create();
                }
                cambio_estado_exit(proceso);
                sem_wait(&semaforo_io);
                while (!queue_is_empty(io_asociada->espera))
                {
                    struct pcb *proceso_remover = queue_pop(io_asociada->espera);
                    struct pcb *proceso_a_terminar = encontrar_proceso_especifico(lista_bloqued, proceso_remover->PID);
                    if (proceso_a_terminar)
                    {
                        sem_wait(&semaforo_bloqued);
                        list_remove_element(lista_bloqued->elements, proceso_a_terminar);
                        sem_post(&semaforo_bloqued);
                        temporal_stop(proceso_a_terminar->tiempo_estado);

                        proceso_a_terminar->MT[3] += temporal_gettime(proceso_a_terminar->tiempo_estado);
                        temporal_destroy(proceso_a_terminar->tiempo_estado);
                        proceso_a_terminar->tiempo_estado = temporal_create();
                    }
                    else
                    {
                        proceso_a_terminar = encontrar_proceso_especifico(lista_sus_bloqued, proceso_remover->PID);

                        sem_wait(&semaforo_bloqued_sus);
                        list_remove_element(lista_sus_bloqued->elements, proceso_a_terminar);
                        sem_post(&semaforo_bloqued_sus);
                        temporal_stop(proceso_a_terminar->tiempo_estado);

                        proceso_a_terminar->MT[4] += temporal_gettime(proceso_a_terminar->tiempo_estado);
                        temporal_destroy(proceso_a_terminar->tiempo_estado);
                        proceso_a_terminar->tiempo_estado = temporal_create();
                    }

                    cambio_estado_exit(proceso_a_terminar);

                    list_remove_element(io_asociada->espera->elements, proceso);
                }
                sem_post(&semaforo_io);
            }
        }
        eliminar_paquete(paquete_io);
    }
}

void *escuchar_io()
{
    char *puerto_escucha_io = config_get_string_value(config_kernel, "PUERTO_ESCUCHA_IO");
    int socket_io = iniciar_modulo(puerto_escucha_io);
    while (1)
    {
        int socket_conectado_io = establecer_conexion(socket_io);
        if (socket_conectado_io < 0)
            abort();

        // Handshake
        char *nombre = recibir_mensaje(socket_conectado_io);

        char *mensaje = "me llego tu mensaje, un gusto io. Al infinito y más allá";

        enviar_mensaje(mensaje, socket_conectado_io);

        struct io *nueva_io;

        nueva_io = malloc(sizeof(struct io));

        nueva_io->socket_io = socket_conectado_io;
        nueva_io->nombre = nombre;
        struct io *existe = (struct io *)encontrar_io_especifico(lista_io, nombre);

        if (existe) // MODIFICAR PARA QUE ESUCCHE 2 IO-
        {
            nueva_io->espera = existe->espera;
            nueva_io->usando_io = existe->usando_io;
            sem_post(&nueva_io->usando_io);
        }
        else
        {
            sem_init(&(nueva_io->usando_io), 1, 0);
            nueva_io->espera = queue_create();

            pthread_t io_especifica;
            pthread_create(&io_especifica, NULL, (void *(*)(void *))planificador_io, (void *)nueva_io);
            pthread_detach(io_especifica);
        }

        sem_wait(&semaforo_io);
        list_add(lista_io, nueva_io);
        sem_post(&semaforo_io);
    }
    return NULL;
}

void suspender_proceso(struct pcb *proceso)
{

    sem_wait(&semaforo_bloqued);
    queue_pop(lista_bloqued);
    sem_post(&semaforo_bloqued);

    sem_wait(&semaforo_bloqued_sus);
    queue_push(lista_sus_bloqued, proceso);
    sem_post(&semaforo_bloqued_sus);

    // Peticion a memoria para hacer el Swap out.
    int conexion = peticion_memoria();
    t_paquete *paquete_dump = crear_paquete();
    int *puntero_pid = malloc(sizeof(int));
    *puntero_pid = proceso->PID;

    int codigo = SUSPENDER;

    agregar_a_paquete(paquete_dump, &codigo, sizeof(int));

    agregar_a_paquete(paquete_dump, (void *)puntero_pid, sizeof(int));
    enviar_paquete(paquete_dump, conexion);
    free(puntero_pid);

    recibir_mensaje(conexion);

    close(conexion);

    sem_post(&memoria_ocupada);

    eliminar_paquete(paquete_dump);
    return;
}

void *cronometrar_proceso(void *data)
{
    struct pcb *proceso = (struct pcb *)data;
    int tiempo = (config_get_int_value(config_kernel, "TIEMPO_SUSPENSION") * 1000);

    unsigned int cant_veces = proceso->ME[3];

    usleep(tiempo);

    sem_wait(&semaforo_bloqued);

    if (encontrar_proceso_especifico(lista_bloqued, proceso->PID) && cant_veces == proceso->ME[3])
    {
        proceso->ME[4] += 1;
        temporal_stop(proceso->tiempo_estado);
        proceso->MT[3] += temporal_gettime(proceso->tiempo_estado);
        proceso->tiempo_estado = temporal_create();
        log_trace(log_kernel, "%d - Pasa del estado Bloqueado al Estado Bloqueado suspendido", proceso->PID);
        sem_post(&semaforo_bloqued);
        suspender_proceso(proceso);

        return NULL;
    }
    else
    {
        sem_post(&semaforo_bloqued);

        return NULL;
    }
}

void *planicador_mediano_plazo(void *m)
{
    while (true)
    {
        sem_wait(&procesos_bloqueados);

        struct pcb *proceso = malloc(sizeof(struct pcb));
        sem_wait(&semaforo_bloqued);
        if (queue_is_empty(lista_bloqued))
            abort();
        proceso = list_get(lista_bloqued->elements, queue_size(lista_bloqued) - 1);
        sem_post(&semaforo_bloqued);

        // hacer config;

        pthread_t cronometro_bloqueado;
        pthread_create(&cronometro_bloqueado, NULL, cronometrar_proceso, (void *)proceso);
        pthread_detach(cronometro_bloqueado);
    }
}

void actualizar_rafaga(struct pcb *proceso, int rafaga_real)
{
    double alfa = config_get_double_value(config_kernel, "ALFA");
    sem_wait(&proceso->mutex_rafaga);
    proceso->rafaga = (alfa * (double)rafaga_real + (1 - alfa) * proceso->rafaga);
    sem_post(&proceso->mutex_rafaga);
    return;
}

void cambio_estado_bloqued(struct pcb *proceso_a_bloquear)
{

    sem_wait(&semaforo_bloqued);
    queue_push(lista_bloqued, proceso_a_bloquear);
    sem_post(&semaforo_bloqued);

    sem_post(&procesos_bloqueados);

    return;
}

void *planifacion_largo_plazo(void *l)
{
    char *tipo_planificacion = config_get_string_value(config_kernel, "ALGORITMO_INGRESO_A_READY");
    struct pcb *proceso = NULL;
    while (true)
    {
        sem_wait(&procesos_creados);

        sem_wait(&semaforo_ready_sus);
        bool lista_sus_ready_vacia = queue_is_empty(lista_sus_ready);
        sem_post(&semaforo_ready_sus);

        sem_wait(&semaforo_new);
        bool lista_new_vacia = queue_is_empty(lista_new);
        sem_post(&semaforo_new);

        if (strcmp(tipo_planificacion, "FIFO") == 0)
        {
            if (!lista_sus_ready_vacia)
            {
                sem_wait(&semaforo_ready_sus);
                proceso = (struct pcb *)queue_peek(lista_sus_ready);

                int conexion_memoria = peticion_memoria();

                t_paquete *paquete_proceso = crear_paquete();

                int *puntero_pid = malloc(sizeof(int));
                *puntero_pid = proceso->PID;

                int codigo = DESUSPENDER;
                agregar_a_paquete(paquete_proceso, &codigo, sizeof(int));

                agregar_a_paquete(paquete_proceso, (void *)puntero_pid, sizeof(int));
                enviar_paquete(paquete_proceso, conexion_memoria);

                char *rta = recibir_mensaje(conexion_memoria);
                eliminar_paquete(paquete_proceso);

                if (!strcmp(rta, "OK"))
                {
                    free(rta);
                    close(conexion_memoria);

                    proceso = (struct pcb *)queue_pop(lista_sus_ready);

                    sem_post(&semaforo_ready_sus);

                    log_trace(log_kernel, "%d - Pasa del estado Suspended Ready al Estado Ready", proceso->PID);
                    proceso->ME[1] += 1;
                    temporal_stop(proceso->tiempo_estado);
                    proceso->MT[5] += temporal_gettime(proceso->tiempo_estado);
                    temporal_destroy(proceso->tiempo_estado);
                    proceso->tiempo_estado = temporal_create();

                    cambio_estado_ready(proceso);
                }
                else
                {
                    close(conexion_memoria);
                    free(rta);
                    sem_post(&semaforo_ready_sus);
                    sem_wait(&memoria_ocupada);
                    sem_post(&procesos_creados);
                }
            }
            else if (!lista_new_vacia)
            {
                sem_wait(&semaforo_new);
                proceso = (struct pcb *)queue_peek(lista_new);

                int tamanio = proceso->tamanio;
                char *path_proceso = proceso->path;

                int conexion_memoria = peticion_memoria();
                t_paquete *paquete_proceso = crear_paquete();

                int *puntero_pid = malloc(sizeof(int));
                *puntero_pid = proceso->PID;

                int codigo = PROCESO_NUEVO;
                agregar_a_paquete(paquete_proceso, &codigo, sizeof(int));

                agregar_a_paquete(paquete_proceso, (void *)puntero_pid, sizeof(int));

                agregar_a_paquete(paquete_proceso, &tamanio, sizeof(int));
                agregar_a_paquete(paquete_proceso, path_proceso, string_length(path_proceso) + 1);

                enviar_paquete(paquete_proceso, conexion_memoria);

                char *rta = recibir_mensaje(conexion_memoria);

                eliminar_paquete(paquete_proceso);

                if (!strcmp(rta, "OK"))
                {
                    free(rta);

                    close(conexion_memoria);

                    proceso = (struct pcb *)queue_pop(lista_new);

                    sem_post(&semaforo_new);
                    proceso->ME[1] += 1;
                    temporal_stop(proceso->tiempo_estado);
                    proceso->MT[0] += temporal_gettime(proceso->tiempo_estado);
                    temporal_destroy(proceso->tiempo_estado);
                    proceso->tiempo_estado = temporal_create();

                    log_trace(log_kernel, "%d - Pasa del estado New al Estado Ready", proceso->PID);

                    cambio_estado_ready(proceso);
                }
                else
                {
                    close(conexion_memoria);

                    sem_post(&semaforo_new);
                    sem_wait(&memoria_ocupada);
                    sem_post(&procesos_creados);
                }
            }
            else
            {
                abort();
            }
        }
        else if (strcmp(tipo_planificacion, "PMCP") == 0)
        {
            bool pudo_incertar = false;
            while (!pudo_incertar)
            {
                if (!lista_sus_ready_vacia)
                {
                    sem_wait(&semaforo_ready_sus);
                    proceso = (struct pcb *)list_get_minimum(lista_sus_ready->elements, encontrar_proceso_pequeño);

                    int conexion_memoria = peticion_memoria();

                    t_paquete *paquete_proceso = crear_paquete();

                    int *puntero_pid = malloc(sizeof(int));
                    *puntero_pid = proceso->PID;

                    int codigo = DESUSPENDER;
                    agregar_a_paquete(paquete_proceso, &codigo, sizeof(int));

                    agregar_a_paquete(paquete_proceso, (void *)puntero_pid, sizeof(int));
                    enviar_paquete(paquete_proceso, conexion_memoria);

                    char *rta = recibir_mensaje(conexion_memoria);
                    eliminar_paquete(paquete_proceso);
                    if (!strcmp(rta, "OK"))
                    {
                        list_remove_element(lista_sus_ready->elements, proceso);
                        close(conexion_memoria);

                        free(rta);
                        sem_post(&semaforo_ready_sus);
                        proceso->ME[1] += 1;
                        temporal_stop(proceso->tiempo_estado);
                        proceso->MT[5] += temporal_gettime(proceso->tiempo_estado);
                        temporal_destroy(proceso->tiempo_estado);
                        proceso->tiempo_estado = temporal_create();

                        log_trace(log_kernel, "%d - Pasa del estado Suspended Ready al Estado Ready", proceso->PID);

                        cambio_estado_ready(proceso);
                        pudo_incertar = true;
                    }
                    else
                    {
                        free(rta);
                        close(conexion_memoria);

                        sem_post(&semaforo_ready_sus);

                        sem_wait(&memoria_ocupada);
                        sem_post(&procesos_creados);
                    }
                }

                else if (!lista_new_vacia)
                {
                    sem_post(&semaforo_new);

                    proceso = (struct pcb *)list_get_minimum(lista_new->elements, encontrar_proceso_pequeño);
                    int tamnio_proceso = proceso->tamanio;
                    char *path_proceso = proceso->path;
                    int conexion_memoria = peticion_memoria();

                    t_paquete *paquete_proceso = crear_paquete();

                    int *puntero_pid = malloc(sizeof(int));
                    *puntero_pid = proceso->PID;

                    int codigo = PROCESO_NUEVO;
                    agregar_a_paquete(paquete_proceso, &codigo, sizeof(int));

                    agregar_a_paquete(paquete_proceso, (void *)puntero_pid, sizeof(int));

                    agregar_a_paquete(paquete_proceso, &tamnio_proceso, sizeof(int));
                    agregar_a_paquete(paquete_proceso, path_proceso, strlen(path_proceso) + 1);

                    enviar_paquete(paquete_proceso, conexion_memoria);

                    char *rta = recibir_mensaje(conexion_memoria);
                    eliminar_paquete(paquete_proceso);
                    if (!strcmp(rta, "OK"))
                    {
                        list_remove_element(lista_new->elements, proceso);
                        close(conexion_memoria);

                        free(rta);
                        sem_post(&semaforo_new);
                        proceso->ME[1] += 1;
                        temporal_stop(proceso->tiempo_estado);
                        proceso->MT[0] += temporal_gettime(proceso->tiempo_estado);
                        temporal_destroy(proceso->tiempo_estado);
                        proceso->tiempo_estado = temporal_create();

                        log_trace(log_kernel, "%d - Pasa del estado New al Estado Ready", proceso->PID);

                        cambio_estado_ready(proceso);
                        pudo_incertar = true;
                    }
                    else
                    {
                        close(conexion_memoria);
                        sem_post(&semaforo_ready);
                        sem_wait(&memoria_ocupada);
                        sem_post(&procesos_creados);
                    }
                }
            }
        }
    }
}

void solicitud_exit(struct pcb *proceso_a_terminar)
{
    sem_wait(&semaforo_execute);
    list_remove_element(lista_execute->elements, proceso_a_terminar);

    temporal_stop(proceso_a_terminar->tiempo_estado);

    proceso_a_terminar->MT[2] += temporal_gettime(proceso_a_terminar->tiempo_estado);
    temporal_destroy(proceso_a_terminar->tiempo_estado);
    sem_post(&semaforo_execute);

    log_trace(log_kernel, "%d - Pasa del estado Execute al Estado Exit", proceso_a_terminar->PID);

    cambio_estado_exit(proceso_a_terminar);
}

void solicitud_bloqueo(struct pcb *proceso_a_bloquear, t_temporal *rafaga_real_actual)
{
    temporal_stop(rafaga_real_actual);

    int tiempo_real = temporal_gettime(rafaga_real_actual);

    actualizar_rafaga(proceso_a_bloquear, tiempo_real);

    log_trace(log_kernel, "%d - Pasa del estado Execute al Estado Bloquedo", proceso_a_bloquear->PID);

    sem_wait(&semaforo_execute);
    list_remove_element(lista_execute->elements, proceso_a_bloquear);
    proceso_a_bloquear->ME[3] += 1;
    temporal_stop(proceso_a_bloquear->tiempo_estado);
    proceso_a_bloquear->MT[2] += temporal_gettime(proceso_a_bloquear->tiempo_estado);
    temporal_destroy(proceso_a_bloquear->tiempo_estado);

    proceso_a_bloquear->tiempo_estado = temporal_create();
    sem_post(&semaforo_execute);

    cambio_estado_bloqued(proceso_a_bloquear);
}

void operar_proceso(struct Cpu *cpu)
{
    bool termino = false;
    t_temporal *rafaga_real_actual = temporal_create();
    do
    {
        t_list *paquete_syscall = recibir_paquete(cpu->socket_dispatch);

        int pid_proceso = *((int *)list_get(paquete_syscall, 1));
        int pc_proceso = *((int *)list_get(paquete_syscall, 2));
        int tipo = *((int *)list_get(paquete_syscall, 0));

        switch (tipo)
        {
        case 5:

            log_trace(log_kernel, "%d - solicitó syscall: Exit", pid_proceso);

            // struct pcb *proceso_a_terminar = encontrar_proceso_especifico(lista_execute, pid_proceso_usado);

            struct pcb *proceso_a_terminar = cpu->proceso;
            proceso_a_terminar->PC = pc_proceso;

            temporal_destroy(rafaga_real_actual);
            solicitud_exit(proceso_a_terminar);

            termino = true;
            break;

        case 1:
            // INit
            log_trace(log_kernel, "%d - solicitó syscall: Init Procc", pid_proceso);

            syscall_init_procc((*(int *)list_get(paquete_syscall, 4)), list_get(paquete_syscall, 3));

            enviar_mensaje("Creado", cpu->socket_dispatch);
            break;

        case 2:

            log_trace(log_kernel, "%d - solicitó syscall: Dump Memory", pid_proceso);

            struct pcb *proceso_a_bloquear = cpu->proceso;
            proceso_a_bloquear->PC = pc_proceso;

            solicitud_bloqueo(proceso_a_bloquear, rafaga_real_actual);

            temporal_destroy(rafaga_real_actual);

            pthread_t dump_proceso;
            int *pid = malloc(sizeof(int));
            *pid = pid_proceso;
            pthread_create(&dump_proceso, NULL, (void *(*)(void *))syscall_dump_memory, (void *)pid);
            pthread_detach(dump_proceso);
            // dump
            termino = true;
            break;

        case 3:
            // IO

            log_trace(log_kernel, "%d - solicitó syscall: IO", pid_proceso);

            struct pcb *proceso_usado = cpu->proceso;

            proceso_usado->PC = pc_proceso;

            // Revisar si la IO solicitada existe, si existe, asociar
            struct io *dispositivo_necesitado = encontrar_io_especifico(lista_io, (char *)list_get(paquete_syscall, 3));
            if (!dispositivo_necesitado || dispositivo_necesitado->socket_io == -1)
            {
                temporal_destroy(rafaga_real_actual);
                solicitud_exit(proceso_usado);

                termino = true;
            }
            else
            {

                struct peticion_io *peticion = malloc(sizeof(struct peticion_io));
                log_trace(log_kernel, "%d - Bloqueado por IO: %s", pid_proceso, dispositivo_necesitado->nombre);

                peticion->tiempo = *((int *)list_get(paquete_syscall, 4));
                int valor = -1;

                sem_getvalue(&dispositivo_necesitado->usando_io, &valor);
                peticion->io_asociada = dispositivo_necesitado;
                
                struct io *io_libre = NULL;
                

                io_libre = encontrar_io_libre(lista_io, dispositivo_necesitado->nombre);
                if ((valor == 0) && (io_libre != NULL)) 
                    peticion->io_asociada = io_libre;
            
            

                peticion->pid = pid_proceso;

                solicitud_bloqueo(proceso_usado, rafaga_real_actual);
                temporal_destroy(rafaga_real_actual);

                pthread_t sys_io;
                pthread_create(&sys_io, NULL, (void *(*)(void *))syscall_io, (void *)peticion);
                pthread_detach(sys_io);
            }

            termino = true;
            break;

        case 4:

            cpu->proceso->PC = pc_proceso;

            cambio_estado_ready(cpu->proceso);

            sem_wait(&semaforo_execute);
            list_remove_element(lista_execute->elements, cpu->proceso);

            temporal_stop(cpu->proceso->tiempo_estado);

            cpu->proceso->MT[2] += temporal_gettime(cpu->proceso->tiempo_estado);
            temporal_destroy(cpu->proceso->tiempo_estado);
            cpu->proceso->tiempo_estado = temporal_create();

            sem_post(&semaforo_execute);

            log_trace(log_kernel, "%d - Pasa del estado Execute al Estado Ready", pid_proceso);

            termino = true;

            break;
        }
        // ESTABA FUERA DEL DO WHILE
        list_destroy_and_destroy_elements(paquete_syscall,free);
        //eliminar_paquete(paquete_syscall);

    } while (termino != true);

    return;
}

void cambio_estado_execute(struct Cpu *cpu, struct pcb *proceso)
{
    cpu->proceso = proceso;

    queue_push(lista_execute, proceso);
    proceso->ME[2] += 1;
    temporal_stop(proceso->tiempo_estado);
    proceso->MT[1] = temporal_gettime(proceso->tiempo_estado);

    temporal_destroy(proceso->tiempo_estado);

    proceso->tiempo_estado = temporal_create();

    log_trace(log_kernel, "%d - Pasa del estado Ready al Estado Execute", proceso->PID);

    // Envia a CPU
    t_paquete *proceso_a_ejecutar = crear_paquete();

    agregar_a_paquete(proceso_a_ejecutar, &cpu->proceso->PID, sizeof(int));
    agregar_a_paquete(proceso_a_ejecutar, &cpu->proceso->PC, sizeof(int));

    enviar_paquete(proceso_a_ejecutar, cpu->socket_dispatch);
    sem_wait(&semaforo_cpu);

    cpu->proceso = proceso;
    sem_post(&semaforo_cpu);
    eliminar_paquete(proceso_a_ejecutar);

    return;
}

struct pcb *tomar_proceso(struct Cpu *cpu)
{
    char *tipo_planificacion = config_get_string_value(config_kernel, "ALGORITMO_CORTO_PLAZO");
    struct pcb *proceso;

    // Lo toma
    if (strcmp(tipo_planificacion, "FIFO") == 0)
    {
        sem_wait(&semaforo_ready);
        proceso = queue_pop(lista_ready);
        sem_post(&semaforo_ready);

        return proceso;
    }

    else if (strcmp(tipo_planificacion, "SJF") == 0 || (strcmp(tipo_planificacion, "SRT") == 0))
    {

        sem_wait(&semaforo_ready);
        proceso = list_get_minimum(lista_ready->elements, comparar_rafaga);
        list_remove_element(lista_ready->elements, proceso);
        sem_post(&semaforo_ready);

        return proceso;
    }
    else
    {
        abort();
    }
    return NULL;
}

// 1. Reviso si hay una cpu libre
// 2. B

void *planificador_cpu(void *cpu)
{
    struct Cpu *cpu_especifica = (struct Cpu *)cpu;

    while (true)
    {
        sem_wait(&procesos_listos); // Este no bloquea a nadie más que a sí mismo

        struct pcb *pcb = tomar_proceso(cpu_especifica);

        sem_wait(&semaforo_cpu);
        cambio_estado_execute(cpu, pcb); // Marca como en ejecución
        sem_post(&semaforo_cpu);

        operar_proceso(cpu_especifica);

        sem_wait(&semaforo_cpu);
        cpu_especifica->proceso = NULL;
        sem_post(&semaforo_cpu);
    }
}

// While true
//      Wait procesos listos
//          tomar_proceso(); --> Busca un proceso en ready (copiar-pegar y adaptar) y lo pone en execute (cambiar_proceso_execute())
//          esperar();

void *escuchar_cpu()
{
    char *puerto_escucha_dispatch = config_get_string_value(config_kernel, "PUERTO_ESCUCHA_DISPATCH");
    int socket_dispatch_listen = iniciar_modulo(puerto_escucha_dispatch);
    char *puerto_escucha_interrupt = config_get_string_value(config_kernel, "PUERTO_ESCUCHA_INTERRUPT");
    int socket_interrupt = iniciar_modulo(puerto_escucha_interrupt);

    while (1)
    {
        int socket_conectado_dispatch = establecer_conexion(socket_dispatch_listen);
        int socket_conectado_interrupt = establecer_conexion(socket_interrupt);

        // Handshake
        char *id = recibir_mensaje(socket_conectado_dispatch);
        char *mensaje = "me llego tu mensaje, un gusto cpu.";

        enviar_mensaje(mensaje, socket_conectado_dispatch);
        // Handshake Terminado
        sem_post(&semaforo_cpu);
        struct Cpu *nueva_cpu;

        nueva_cpu = malloc(sizeof(struct Cpu));

        nueva_cpu->socket_dispatch = socket_conectado_dispatch;
        nueva_cpu->socket_interrupt = socket_conectado_interrupt;

        nueva_cpu->id = id;
        nueva_cpu->proceso = NULL;

        list_add(lista_cpu, nueva_cpu);
        sem_post(&semaforo_cpu);

        pthread_t cpu_especifica;
        pthread_create(&cpu_especifica, NULL, (void *(*)(void *))planificador_cpu, (void *)nueva_cpu);
        pthread_detach(cpu_especifica);
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    // Procesos
    lista_new = queue_create();
    lista_ready = queue_create();
    lista_sus_ready = queue_create();
    lista_execute = queue_create();
    lista_bloqued = queue_create();
    lista_sus_bloqued = queue_create();
    lista_finished = queue_create();

    // Listas de Modulos
    lista_cpu = list_create();
    lista_io = list_create();

    // Inicalización de semaforos
    sem_init(&semaforo_ready, 1, 1);
    sem_init(&semaforo_bloqued, 1, 1);
    sem_init(&semaforo_bloqued_sus, 1, 1);
    sem_init(&semaforo_execute, 1, 1);
    sem_init(&semaforo_new, 1, 1);
    sem_init(&semaforo_ready_sus, 1, 1);
    sem_init(&semaforo_cpu, 1, 1);
    sem_init(&semaforo_io, 1, 1);

    sem_init(&procesos_creados, 1, 0);

    sem_init(&procesos_bloqueados, 1, 0);

    sem_init(&cpu_disponibles, 1, 0);
    sem_init(&proceso_desalojado, 1, 0);
    sem_init(&memoria_ocupada, 0, 1);
    sem_init(&procesos_listos, 1, 0);

    // Kernel "Core" 10/10 Joke
    config_kernel = iniciar_config("/home/utnso/Desktop/tp-2025-1c-RompeComputadoras/kernel/kernel.conf");

    log_kernel = log_create("kernel.log", "kernel", true, LOG_LEVEL_TRACE);
    contador_procesos = 0;

    char *nombreArchivo = "ESTABILIDAD_GENERAL";
    int tamanioProceso = 0;

    pthread_t servidor_cpu;
    pthread_create(&servidor_cpu, NULL, escuchar_cpu, NULL);
    pthread_detach(servidor_cpu);

    pthread_t servidor_io;
    pthread_create(&servidor_io, NULL, escuchar_io, NULL);
    pthread_detach(servidor_io);

    pthread_t planificador_largo;
    pthread_create(&planificador_largo, NULL, planifacion_largo_plazo, NULL);
    pthread_detach(planificador_largo);

    /*
        pthread_t planificador_corto;
        pthread_create(&planificador_corto, NULL, planificacion_corto_plazo, NULL);
        pthread_detach(planificador_corto);
    */
    pthread_t planificador_mediano;
    pthread_create(&planificador_mediano, NULL, planicador_mediano_plazo, NULL);
    pthread_detach(planificador_mediano);

    /*
    if (argc < 4)
    {
        log_trace(log_kernel, "Error, Parametros Invalidos");
        return 1;
    }
    nombreArchivo = argv[2];


    tamanioProceso = atoi(argv[3]);
    */
    log_info(log_kernel, "\n               ___\n             _//_\\\\\n           ,\"    //\".\n          /          \\\n        _/           |\n       (.-,--.       |\n       /o/  o \\     /\n       \\_\\    /  /\\/\\\n       (__`--'   ._)\n       /  `-.     |\n      (     ,`-.  |\n       `-,--\\_  ) |-.\n        _`.__.'  ,-' \\\n       |\\ )  _.-'    |\n       i-\\.'\\     ,--+.\n     .' .'   \\,-'/     \\\n    / /         /       \\\n    7_|         |       |\n    |/          \"i.___.j\"\n    /            |     |\\\n   /             |     | \\\n  /              |     |  |\n  |              |     |  |\n  |____          |     |-i'\n   |   \"\"\"\"----\"\"|     | |\n   \\           ,-'     |/\n    `.         `-,     |\n     |`-._      / /| |\\ \\\n     |    `-.   `' | ||`-'\n     |      |      `-'|\n     |      |         |\n     |      |         |\n     |      |         |\n     |      |         |\n     |      |         |\n     |      |         |\n     )`-.___|         |\n   .'`-.____)`-.___.-'(\n .'        .'-._____.-i\n/        .'           |h\n`-------/         .   |j\n        `--------' \"--'w\n ");

    getchar();
    syscall_init_procc(tamanioProceso, nombreArchivo);
    while (1)
        ;

    // esperar a que el cliente ingrese "Enter" y iniciar la planificación.

    // log_destroy(log_kernel);
    // config_destroy(config_kernel);

    return 0;
}
