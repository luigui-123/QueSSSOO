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

struct pcb
{
    int PID;
    int PC;
    int MT [7];
    int ME [7];
    char* tamanio;
    char* path;
    double rafaga;
};

//Listas
t_list* lista_cpu;
t_list* lista_io;

//Misc
int contador_procesos;
t_config* config_kernel;
t_log* log_kernel;

//Queues de Procesos
t_queue* lista_new;
t_queue* lista_ready; 
t_queue* lista_sus_ready; 
t_queue* lista_execute; 
t_queue* lista_bloqued; 
t_queue* lista_sus_bloqued; 
t_queue* lista_finished;

//Semaforos
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


struct Cpu
{
    int socket_dispatch;
    int socket_interrupt;
    char* id;
    int ocupado;
    struct pcb* proceso;

};

struct pcb_execute
{
    struct pcb* proceso;
    struct Cpu* cpu_a_cargo;

};

struct io
{
    int socket_io;
    char* nombre;
    sem_t usando_io;
    t_queue* espera;
};

struct peticion_io
{
    struct io* io_asociada;
    long tiempo;
    int pid;
};


t_config* iniciar_config()
{
	t_config* nuevo_config = config_create("kernel.conf");
	return nuevo_config;
}


int peticion_memoria()
{
    char* puerto_memoria = config_get_string_value(config_kernel, "PUERTO_MEMORIA");
    char* ip_memoria = config_get_string_value(config_kernel, "IP_MEMORIA");
    int conexion_memoria = iniciar_conexion(ip_memoria, puerto_memoria,log_kernel);
    return conexion_memoria;
}

void syscall_init_procc(char* tamanio, char* nombre)
{
    struct pcb* proceso_nuevo;

    proceso_nuevo = malloc(sizeof(struct pcb));

    proceso_nuevo->PID = contador_procesos;
    proceso_nuevo->tamanio = tamanio;
    proceso_nuevo->path = nombre;
    proceso_nuevo->PC = 0;
    proceso_nuevo->rafaga = config_get_double_value(config_kernel, "ESTIMACION_INICIAL");
    sem_wait(&semaforo_new);
    queue_push(lista_new, proceso_nuevo);
    contador_procesos+=1;
    sem_post(&semaforo_new);
    sem_post(&procesos_creados);
    
}

char* syscall_dump_memory(int pid_proceso)
{
    int conexion = peticion_memoria();
    t_paquete* paquete_dump = crear_paquete();
    int* puntero_pid = malloc(sizeof(int));
    *puntero_pid = pid_proceso; 
    agregar_a_paquete(paquete_dump, (void*)puntero_pid, sizeof(int));
    enviar_paquete(paquete_dump, conexion, log_kernel);
    free(puntero_pid);
    //Preguntar que significa "Error"
    
    return recibir_mensaje(conexion, log_kernel);
}


void* syscall_io(void* peticion)
{
    struct peticion_io* peticion_nueva = (struct peticion_io*)peticion;

    queue_push((peticion_nueva->io_asociada->espera), peticion_nueva);

    sem_post(&(peticion_nueva->io_asociada->usando_io));


    //Revisar bien como se haria.
    //Controlador de IO, que este siempre haciendo lo de enviar paquetes de las diferentes IO?
    //Semaforo por IO?
    return NULL;
}



void* escuchar_cpu()
{
    char* puerto_escucha_dispatch = config_get_string_value(config_kernel, "PUERTO_ESCUCHA_DISPATCH");
    int socket_dispatch_listen = iniciar_modulo(puerto_escucha_dispatch, log_kernel);
    char* puerto_escucha_interrupt = config_get_string_value(config_kernel, "PUERTO_ESCUCHA_INTERRUPT");
    int socket_interrupt = iniciar_modulo(puerto_escucha_interrupt, log_kernel);

    while (1)
    {
        int socket_conectado_dispatch = establecer_conexion(socket_dispatch_listen, log_kernel);
        int socket_conectado_interrupt = establecer_conexion(socket_interrupt, log_kernel);
        
        //Handshake
        char* id = recibir_mensaje(socket_conectado_dispatch, log_kernel);
        char* mensaje = "me llego tu mensaje, un gusto cpu: ";
        strcat(mensaje, id);
        enviar_mensaje(mensaje,socket_conectado_dispatch,log_kernel);
        //Handshake Terminado
        
        struct Cpu* nueva_cpu;

        nueva_cpu = malloc(sizeof(struct Cpu));

        nueva_cpu->socket_dispatch = socket_conectado_dispatch;
        nueva_cpu->socket_interrupt = socket_conectado_interrupt;

        nueva_cpu->id = id;
        nueva_cpu->ocupado = 0;
        sem_post(&cpu_disponibles);

        list_add(lista_cpu, nueva_cpu);
         
        //crear hilo de conexión. Con Detach
        //Crear Proceso --> "Escuchar_CPU_Especifica" --> Datos conexiones, etc y de ahi
        // se pueden hacer las Syscalls. 
    }
    return NULL;

}

void cambio_estado_ready(struct pcb* proceso)
{
    sem_wait(&semaforo_ready);
    queue_push(lista_ready, proceso);
    sem_post(&semaforo_ready);

}



struct pcb* encontrar_proceso_especifico(t_queue* lista, int pid)
{
    bool encontrar_proceso(void* elemento)
    {
        struct pcb* proceso = (struct pcb*) elemento;
        return proceso->PID == pid;
    }
    struct pcb* proceso = (struct pcb*) list_find((t_list*)lista, encontrar_proceso);
    return proceso;
}


void* encontrar_proceso_pequeño(void* elemento, void* elemento2)
{
    struct pcb* proceso1 = (struct pcb*) elemento;
    struct pcb* proceso2 = (struct pcb*) elemento2;
    return proceso1->tamanio < proceso2->tamanio ? proceso1 : proceso2;
}

void planificador_io(struct io* io_asociada)
{
    while (1)
    {
        sem_wait(&(io_asociada->usando_io));
    
        struct peticion_io* peticion = (struct peticion_io*)queue_pop(io_asociada->espera);
        t_paquete* paquete_io = crear_paquete();
        int* puntero_pid = malloc(sizeof(int));
        *puntero_pid = peticion->pid; 

        agregar_a_paquete(paquete_io, (void*)puntero_pid, sizeof(int));
        agregar_a_paquete(paquete_io, (void*)peticion->tiempo, sizeof(int));
        enviar_paquete(paquete_io,peticion->io_asociada->socket_io, log_kernel);
        free(puntero_pid);


        recibir_paquete(peticion->io_asociada->socket_io, log_kernel); //O mensaje, no se xd


        //Verificar si esta en BLOQUED y suspendend blocked

        sem_wait(&semaforo_bloqued);
        struct pcb* proceso = encontrar_proceso_especifico(lista_bloqued, peticion->pid);
        list_remove_element ((t_list*)lista_bloqued, proceso);
        sem_post((&semaforo_bloqued));

        cambio_estado_ready(proceso);
        free(peticion);
        peticion=NULL;
    }
}

struct io* encontrar_io_especifico(t_list* lista, char* nombre)
{
    bool encontrar_io(void* elemento)
    {
        struct io* dispositivo = (struct io*) elemento;
        return dispositivo->nombre == nombre;
    }
    struct io* dispositivo = list_find((t_list*)lista, encontrar_io);
    return dispositivo;
}

void* escuchar_io()
{
    char* puerto_escucha_io = config_get_string_value(config_kernel, "PUERTO_ESCUCHA_IO");
    int socket_io = iniciar_modulo(puerto_escucha_io, log_kernel);
    while (1)
    {
    int socket_conectado_io = establecer_conexion(socket_io, log_kernel);

    //Handshake
    char* nombre = recibir_mensaje(socket_conectado_io, log_kernel);
    char* mensaje="me llego tu mensaje, un gusto io ";
    strcat(mensaje, nombre);
    enviar_mensaje(mensaje,socket_conectado_io,log_kernel);


    struct io* nueva_io;

    nueva_io = malloc(sizeof(struct io));

    nueva_io->socket_io = socket_conectado_io;
    nueva_io->nombre = nombre;
    struct io* existe = (struct io*)encontrar_io_especifico(lista_io, nombre);

    if (existe) //MODIFICAR PARA QUE ESUCCHE 2 IO-
    {
        nueva_io->espera = existe->espera;
        nueva_io->usando_io = existe->usando_io;
    }
    else
    {
        sem_init(&(nueva_io->usando_io), 1, 0);
        nueva_io->espera = queue_create();
    }
    
    pthread_t io_especifica;
    pthread_create(&io_especifica, NULL, (void *(*)(void *))planificador_io, (void*)nueva_io);
    

    list_add(lista_io, nueva_io);
    }
    return NULL;

}

void suspender_proceso(struct pcb* proceso)
{

    sem_wait(&semaforo_bloqued);
    queue_pop(lista_bloqued);
    sem_post(&semaforo_bloqued);
    
    sem_wait(&semaforo_bloqued_sus);
    queue_push(lista_sus_bloqued, proceso);
    sem_post(&semaforo_bloqued_sus);

    //Peticion a memoria para hacer el Swap out.
    int conexion = peticion_memoria();
    t_paquete* paquete_dump = crear_paquete();
    int* puntero_pid = malloc(sizeof(int));
    *puntero_pid = proceso->PID;

    agregar_a_paquete(paquete_dump, (void*)puntero_pid, sizeof(int));
    enviar_paquete(paquete_dump, conexion, log_kernel);
    free(puntero_pid);
    
}

void cronometrar_proceso(struct pcb* proceso)
{
    t_temporal * cronometro = temporal_create();
    
    usleep(50000);


    if (encontrar_proceso_especifico(lista_bloqued, proceso->PID))
    {
        suspender_proceso(proceso);
    }

}


void planicador_mediano_plazo()
{
    while (1)
    {
        sem_wait(&procesos_bloqueados);
        struct pcb* proceso = malloc(sizeof(struct pcb));
        sem_wait(&semaforo_bloqued);
        proceso = list_get((t_list*) lista_bloqued, queue_size(lista_bloqued));
        sem_post(&semaforo_bloqued);

        //hacer config;
        
        pthread_t cronometro_bloqueado;
        pthread_create(&cronometro_bloqueado, NULL, cronometrar_proceso, proceso);
        pthread_detach(&cronometro_bloqueado);

    }


}

actualizar_rafaga(struct pcb* proceso, int rafaga_real)
{
    double alfa = config_get_double_value(config_kernel, "ALFA");
    proceso->rafaga = (alfa * (double)rafaga_real + (1-alfa) * proceso->rafaga); 
}

void cambio_estado_bloqued(int pid, int pc)
{
    
    sem_wait(&semaforo_execute); 
    struct pcb* proceso_a_bloquear = encontrar_proceso_especifico(lista_execute, pid);
    list_remove_element ((t_list*)lista_execute, proceso_a_bloquear);
    sem_post(&semaforo_execute);
    

    proceso_a_bloquear->PC = pc;
    sem_wait(&semaforo_bloqued); 
    queue_push(lista_bloqued, proceso_a_bloquear);
    sem_post(&semaforo_bloqued);
    
}

void cambio_estado_exit(int pid)
{
    sem_wait(&semaforo_execute);
    struct pcb* proceso_a_terminar = encontrar_proceso_especifico(lista_execute, pid);
    list_remove_element ((t_list*)lista_execute, proceso_a_terminar);
    sem_post(&semaforo_execute); 

    queue_push(lista_finished, proceso_a_terminar);

}
/*

Preguntas / Aclaraciones
5. Semaforo para Finalizar proceso y consultar a memoria --> SIs

7. EStimar tiempo de rafaga y tiempo anterior?? --> estimación inicial en Config

*/ 



void planifacion_largo_plazo()
{
    char* tipo_planificacion = config_get_string_value(config_kernel, "ALGORITMO_INGRESO_A_READY");
    struct pcb* proceso;
    
    sem_wait(&procesos_creados);

    if (tipo_planificacion == "FIFO")
    {
        //cambio_estado_ready();
        if (queue_size(lista_sus_ready) > 0)
        {
            sem_wait(&semaforo_ready_sus);
            proceso = (struct pcb*) queue_pop(lista_sus_ready);

            char* tamnio_proceso = proceso->tamanio;
            int conexion_memoria = peticion_memoria();
            enviar_mensaje(tamnio_proceso, conexion_memoria, log_kernel);
            if (recibir_mensaje(conexion_memoria, log_kernel) == "Ok")
            {
                sem_post(&semaforo_ready_sus);

                cambio_estado_ready(proceso); //Y algo mas...
            }
            else
            {
                sem_post(&semaforo_ready_sus);
                //Semaforo de no hay mas espacio en memoria
            }
        }
        else if (queue_size(lista_new) > 0)
        {
            sem_wait(&semaforo_new);
            proceso =(struct pcb*)  queue_pop(lista_new);

            char* tamnio_proceso = proceso->tamanio;
            int conexion_memoria = peticion_memoria();
            enviar_mensaje(tamnio_proceso, conexion_memoria, log_kernel);
            if (recibir_mensaje(conexion_memoria, log_kernel) == "Ok")
            {
                sem_post(&semaforo_new);

                cambio_estado_ready(proceso); //Y algo mas...
            }
            else
            {
                sem_post(&semaforo_new);
                //Semaforo de no hay mas espacio en memoria
            }
        }
    }
    else if (tipo_planificacion == "PMCP")
    {
        if (!queue_is_empty(lista_sus_ready))
        {
            
            // Iterar cada elemento de ambas listas y sacar el mas pequeño.
            proceso = (struct pcb*) list_get_minimum(lista_sus_ready, encontrar_proceso_pequeño);
            char* tamnio_proceso = proceso->tamanio;
            int conexion_memoria = peticion_memoria();
            enviar_mensaje(tamnio_proceso, conexion_memoria, log_kernel);
            if (recibir_mensaje(conexion_memoria, log_kernel) == "Ok")
            {
                sem_post(&semaforo_ready_sus);

                cambio_estado_ready(proceso); //Y algo mas...
            }
            else
            {
                sem_post(&semaforo_ready_sus);
                //Semaforo de no hay mas espacio en memoria
            }


        }
        else if (!queue_size(lista_new) > 0)
        {
            proceso = (struct pcb*) list_get_minimum(lista_new, encontrar_proceso_pequeño);
            char* tamnio_proceso = proceso->tamanio;
            int conexion_memoria = peticion_memoria();
            enviar_mensaje(tamnio_proceso, conexion_memoria, log_kernel);
            if (recibir_mensaje(conexion_memoria, log_kernel) == "Ok")
            {
                sem_post(&semaforo_ready);

                cambio_estado_ready(proceso); //Y algo mas...
            }
            else
            {
                sem_post(&semaforo_ready);
                //Semaforo de no hay mas espacio en memoria
            }
        }
        //Luego achicar con función "Encontrar proceso mas pequeño".
        
    }


}

bool buscar_disponible(void *elemento) 
{
    struct Cpu* cpu_especifica = (struct Cpu *) elemento;
    return cpu_especifica->ocupado == 0;
}


void* escucha_cpu_especifica(void* cpu)
{

    t_paquete *proceso_a_ejecutar = crear_paquete();
    struct Cpu* cpu_especifica = (struct Cpu*) cpu;

    //Modificar para que se manden por separado    
    agregar_a_paquete(proceso_a_ejecutar, cpu_especifica->proceso->PID, sizeof(int));
    agregar_a_paquete(proceso_a_ejecutar, cpu_especifica->proceso->PC, sizeof(int));

    t_temporal * rafaga_real_actual = temporal_create();
    enviar_paquete(proceso_a_ejecutar, cpu_especifica->socket_dispatch, log_kernel);
        
        // [0] = Razón --> 0 = Exit, 1 = Init, 2 = Dump, 3 = I/O, 4 desalojo
        // [1] = Parametro Reservado para el PID
        // [2] = PC
        // [3...] = Parametros adicionales.
        // IO --> Disp y tiempo / InitPrco -> Archivo y tamanio
        // 

    t_list* paquete_recibido = recibir_paquete(cpu_especifica->socket_dispatch, log_kernel);

    while (list_get(paquete_recibido, 0) ==  1)
    {
    int pid_proceso_usado = *((int*)list_get(paquete_recibido, 1));
    int pc_proceso_usado = *((int*)list_get(paquete_recibido, 2));
    
        if 	(list_get(paquete_recibido, 0) ==  0) //Exit --> TERMINA proceso, no interrumpe
        {
            //revisar para Listas.
            //ESto mandarlo a Execute -> Exit
            log_info(log_kernel, ""); //Proceso para enviar metricas
            cambio_estado_exit(pid_proceso_usado);
            cpu_especifica->ocupado=0;
            
            sem_post(&cpu_disponibles);
            return NULL;
        }
        else if (*((int*)list_get(paquete_recibido, 0)) ==  1) //Init
        {
            syscall_init_procc(list_get(paquete_recibido, 3), list_get(paquete_recibido, 4));
            //Meter mensaje a CPU
            
        }
        else if (list_get(paquete_recibido, 0) ==  2) //Dump
        {
            temporal_stop(rafaga_real_actual);

            int tiempo_real = temporal_gettime(rafaga_real_actual);
            
            
            actualizar_rafaga(pid_proceso_usado, tiempo_real);


            cambio_estado_bloqued(pid_proceso_usado, pc_proceso_usado);
            pthread_t dump_proceso;
            pthread_create(&dump_proceso, NULL, (void *(*)(void *))syscall_dump_memory, (void*)pid_proceso_usado);
            pthread_detach(&dump_proceso);
            cpu_especifica->ocupado=0;
            sem_post(&cpu_disponibles);

            return NULL;
        }
        else if (list_get(paquete_recibido, 0) ==  3) //IO
        {
            temporal_stop(rafaga_real_actual);

            int tiempo_real = temporal_gettime(rafaga_real_actual);
            
            actualizar_rafaga(pid_proceso_usado, tiempo_real);

            //Revisar si la IO solicitada existe, si existe, asociar
            struct io* dispositivo_necesitado = encontrar_io_especifico(lista_io, (char*)list_get(paquete_recibido, 3));
            if (!dispositivo_necesitado)
            {
                cambio_estado_exit(pid_proceso_usado);
            }
            struct peticion_io* peticion = malloc(sizeof(struct peticion_io)); 
            
            peticion->tiempo = *((int*)list_get(paquete_recibido, 4));
            peticion->io_asociada = dispositivo_necesitado;
            peticion->pid = pid_proceso_usado;
            cambio_estado_bloqued(pid_proceso_usado, pc_proceso_usado);
            pthread_t sys_io;
            pthread_create(&sys_io, NULL, (void *(*)(void *))syscall_io, (void*)peticion);
            pthread_detach(&sys_io);



            cpu_especifica->ocupado=0;
            sem_post(&cpu_disponibles);
                //Cuando bloqueo --> enviar proceso a la lista de bloqueados Y a la lista de la IO
                //Agregar en IO, Lista_bloqueados y "ocupado". 
                //Se hace aparte la confirmación de existencia y se hace despues el proceso

            return NULL;
                //LO mismo que en Exit. Modificar para que sea unico proceso --> Cambio de Execute a Exit

        }
        else if (list_get(paquete_recibido, 0) ==  4) //DECHALOGO
        {
            temporal_stop(rafaga_real_actual);

            int tiempo_real = temporal_gettime(rafaga_real_actual);
                
            actualizar_rafaga(pid_proceso_usado, tiempo_real);
            struct pcb* proceso_a_desalojar = encontrar_proceso_especifico(lista_execute, pid_proceso_usado);

            cambio_estado_ready(proceso_a_desalojar);

        }
        t_list* paquete_recibido = recibir_paquete(cpu_especifica->socket_dispatch, log_kernel);

    }


    return NULL;
    
}


void cambio_estado_execute(struct Cpu* cpu, struct pcb* proceso)
{
    //mutex
    cpu->ocupado = 1;
    //AGREGAR SEM
    
    sem_wait(&semaforo_execute);
    queue_push(lista_execute, proceso);
    sem_post(&semaforo_execute); 
    //mutex

    cpu->proceso = proceso;

    pthread_t escucha_cpu_stream;
    pthread_create(&escucha_cpu_stream, NULL, escucha_cpu_especifica, (void*)cpu);
    //Struct o lista global?
    pthread_detach(escucha_cpu_stream);

}

void* comparar_rafaga(void* p1, void* p2)
{
    struct pcb* proceso1 = (struct pcb*) p1;
    struct pcb* proceso2 = (struct pcb*) p2;
    
    
    return proceso1->rafaga <= proceso2->rafaga ? proceso1 : proceso2;
}


struct Cpu* encontrar_proceso_especifico_en_ejecucion(t_list* lista, int pid)
{
    bool encontrar_proceso(void* elemento)
    {
        struct Cpu* cpu = (struct cpu*) elemento;
        return cpu->proceso->PID == pid;
    }
    struct cpu* cpu_buscada = ((struct cpu*)list_find(lista, encontrar_proceso));
    return cpu_buscada;
}


void planificacion_corto_plazo()
{

    char* tipo_planificacion = config_get_string_value(config_kernel, "ALGORITMO_INGRESO_A_READY");
    struct pcb* proceso;
    if (strcmp(tipo_planificacion, "FIFO") == 0)
    {
        sem_wait(&semaforo_cpu);
        sem_wait(&cpu_disponibles);

        struct Cpu* cpu_libre = (struct Cpu*) list_find(lista_cpu, buscar_disponible);
        if (cpu_libre != NULL)
        {
            sem_wait(&semaforo_ready);
            proceso = queue_pop(lista_ready);
            sem_post(&semaforo_ready);
            cambio_estado_execute(cpu_libre, proceso);
        }   
       //Lo mejor seria un semaforo cuando termine algun proceso o meter un while?
        sem_post(&semaforo_cpu);
    }
    else if (tipo_planificacion == "SJF sin Desalo")
    {
        sem_wait(&semaforo_cpu);
        sem_wait(&cpu_disponibles);

        struct Cpu* cpu_libre = (struct Cpu*) list_find(lista_cpu, buscar_disponible);
        if (cpu_libre != NULL)
        {
            proceso =  list_get_minimum(lista_ready, comparar_rafaga);
            sem_wait(&semaforo_ready);
            proceso = queue_pop(lista_ready);
            sem_post(&semaforo_ready);
            cambio_estado_execute(cpu_libre, proceso);
                        

        }
        sem_post(&semaforo_cpu);
    }
    else if (tipo_planificacion == "SJF con Desalo")
    {
        sem_wait(&semaforo_cpu);
        sem_wait(&cpu_disponibles);

        struct Cpu* cpu_libre = (struct Cpu*) list_find(lista_cpu, buscar_disponible);
        if (cpu_libre != NULL)
        {
            proceso =  list_get_minimum(lista_ready, comparar_rafaga);
            sem_wait(&semaforo_ready);
            proceso = queue_pop(lista_ready);
            sem_post(&semaforo_ready);
            cambio_estado_execute(cpu_libre, proceso);
        }
        else
        {
            proceso =  list_get_minimum(lista_ready, comparar_rafaga);
            struct pcb* proceso_a_desalojar = list_get_minimum(lista_execute, comparar_rafaga);

            if (proceso->rafaga < proceso_a_desalojar->rafaga)
            {
                //Call a CPU
                struct Cpu* cpu_buscada = encontrar_proceso_especifico_en_ejecucion(lista_cpu, proceso_a_desalojar->PID);
                char* mensaje_cpu = "DESALOJAR";

                enviar_mensaje(mensaje_cpu, cpu_buscada->socket_interrupt, log_kernel);

                cambio_estado_execute(cpu_buscada, proceso);


            }
        
        }
        sem_post(&semaforo_cpu);
    }

}


int main(int argc, char* argv[]) {
    //Procesos
    lista_new = queue_create();
    lista_ready = queue_create();
    lista_sus_ready = queue_create();
    lista_execute = queue_create();
    lista_bloqued = queue_create();
    lista_sus_bloqued = queue_create();
    lista_finished = queue_create();

    //Listas de Modulos
    lista_cpu = list_create();
    lista_io = list_create();

    //Inicalización de semaforos
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


    //Kernel "Core" 10/10 Joke
    config_kernel = iniciar_config("kernel");
    log_kernel = log_create("kernel.log", "kernel", false, LOG_LEVEL_INFO);
    contador_procesos = 0;
    
    char *nombreArchivo = NULL;
    char *tamanioProceso = NULL;

    if (argc < 3)
    {
        log_info(log_kernel, "Error, Parametros Invalidos");
        return 1;
    }
    nombreArchivo = argv[1];
    tamanioProceso = argv[2];

    pthread_t servidor_cpu;
    pthread_create(&servidor_cpu, NULL, escuchar_cpu, lista_cpu);

    pthread_t servidor_io;
    pthread_create(&servidor_io, NULL, escuchar_io, lista_io);

    //esperar a que el cliente ingrese "Enter" y iniciar la planificación.


    log_destroy(log_kernel);
    config_destroy(config_kernel);

    return 0;
}

//Agrega MALLOC
