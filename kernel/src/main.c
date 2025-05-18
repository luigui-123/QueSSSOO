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


struct pcb
{
    int PID;
    int PC;
    int MT [7];
    int ME [7];
    char* tamanio;
    char* path;
    //trabajo en progeso.
};

t_list* lista_cpu;
t_list* lista_io;

t_config* config_kernel;
t_log* log_kernel;
t_queue* lista_new;
t_queue* lista_ready; 
t_queue* lista_sus_ready; 
t_queue* lista_execute; 
t_queue* lista_bloqued; 
t_queue* lista_sus_bloqued; 
t_queue* lista_finished;

struct Cpu
{
    int socket_dispatch;
    int socket_interrupt;
    char* id;
    int ocupado;

};
struct pcb_execute
{
    int pid;
    int pc;
    struct Cpu* cpu_a_cargo;

};

struct io
{
    int socket_io;
    char* nombre;
};

t_config* iniciar_config()
{
	t_config* nuevo_config = config_create("kernel.conf");
	return nuevo_config;
}

void syscall_init_procc(char* tamanio, char* nombre)
{
    struct pcb* proceso_nuevo;

    proceso_nuevo = malloc(sizeof(struct pcb));

    proceso_nuevo->PID = queue_size(lista_new);
    proceso_nuevo->tamanio = tamanio;
    proceso_nuevo->path = nombre;
    proceso_nuevo->PC = 0;

    queue_push(lista_new, proceso_nuevo);

}



int syscall_io(char* dispositivo, int tiempo, int pid)
{

}

int peticion_memoria()
{
    char* puerto_memoria = config_get_string_value(config_kernel, "PUERTO_MEMORIA");
    char* ip_memoria = config_get_string_value(config_kernel, "IP_MEMORIA");
    int conexion_memoria = iniciar_conexion(ip_memoria, puerto_memoria,log_kernel);
    return conexion_memoria;
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

        list_add(lista_cpu, nueva_cpu);
         
        //crear hilo de conexión. Con Detach
        //Crear Proceso --> "Escuchar_CPU_Especifica" --> Datos conexiones, etc y de ahi
        // se pueden hacer las Syscalls. 
    }
    return NULL;

}

void* escuchar_io()
{
    char* puerto_escucha_io = config_get_string_value(config_kernel, "PUERTO_ESCUCHA_DISPATCH");
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

    list_add(lista_io, nueva_io);
    }
    return NULL;

}


void cambio_estado_ready(struct pcb* proceso)
{
    sem_t semaforo_ready;
    sem_init(&semaforo_ready, 0, 1);
    if (!queue_is_empty(lista_sus_ready))
    {
        //mutex
        sem_wait(&semaforo_ready);
        proceso = queue_pop(lista_sus_ready);
        char* tamnio_proceso = proceso->tamanio;
        int conexion_memoria = peticion_memoria();
        enviar_mensaje(tamnio_proceso, conexion_memoria, log_kernel);
        if (recibir_mensaje(conexion_memoria, log_kernel) == "Ok")
        {
            queue_pop(lista_sus_ready);
            queue_push(lista_ready, proceso);
        }  
        else
        {
        // Casteo el t_queu a T_list y sort moment
        //Se frena. --> Semaforo --> COngelar semaforo y esperar a que proceso entre a Finished
        //Revisar para escucha activa de otra manera.
        }
        sem_post(&semaforo_ready);
    }

    if (!queue_is_empty(lista_new))
    {
        sem_wait(&semaforo_ready);
        proceso = queue_pop(lista_sus_ready);
        char* tamnio_proceso = proceso->tamanio;
        int conexion_memoria = peticion_memoria();
        enviar_mensaje(tamnio_proceso, conexion_memoria, log_kernel);
        if (recibir_mensaje(conexion_memoria, log_kernel) == "Ok")
        {
            queue_pop(lista_sus_ready);
            queue_push(lista_ready, proceso);
        }  
        else
        {
        // Casteo el t_queu a T_list y sort moment
        //Se frena. --> Semaforo --> COngelar semaforo y esperar a que proceso entre a Finished
        //Revisar para escucha activa de otra manera.
        }
        sem_post(&semaforo_ready);
    }

}
void cambio_estado_bloqued(struct pcb* proceso)
{
    return;
}
/*

Preguntas / Aclaraciones
1. Escuchar CPU especifica --> Sis
3. Memoria siempre tiene todos los procesos? -> Sis
4. "Productor Consumidor" --> Sis
5. Semaforo para Finalizar proceso y consultar a memoria --> SIs
6. Cuando se realiza la planificación a largo plazo y la de Corto plazo? --> Siempre
7. EStimar tiempo de rafaga y tiempo anterior?? --> estimación inicial en Config
8. Como devolver la lista de INstrucciones de memoria (1 a 1) o Struct X? --> 1 a 1
9. Memoria devuelve tamaño para comprar o al reves? --> Kernel a Memoria (Paquete)
10. Como devolver proceso terminado? --> Lo dejas en lista
11. Tipo de archivo de pseudocodigo y salto de linea y leer linea? --> formato string


CPUs multiples --> lista al entrar al execute --> CPU = cant cpu
Exectue --> EScucha especificamente a la CPU
*/ 

void planifacion_largo_plazo()
{
    struct pcb* proceso;
    char* tipo_planificacion = config_get_string_value(config_kernel, "ALGORITMO_INGRESO_A_READY");
    if (tipo_planificacion == "FIFO")
    {
        cambio_estado_ready(proceso);
    }
    else if (tipo_planificacion == "PMCP")
    {
        if (!queue_is_empty(lista_sus_ready))
        {
            /*
                        -------> list_add_sorted(lista_ready,) Casteo

            for (int i=0; i<queue_size(lista_sus_ready))
            {
                proceso_chico = queue_pop(lista_sus_ready);
                if (proceso_chico->tamanio < queue_peek(lista_sus_ready))
                {
                    queue_push(lista_sus_ready, queue_pop(lista_sus_ready));
                }
                else
                {
                    queue_push(lista_sus_ready, proceso_chico);
                    proceso_chico = queue_pop(lista_sus_ready); preguntar que estamos guardando
                }   
            }
            */ 
        }

        
    }

}

int buscar_disponible(void *elemento) 
{
    struct Cpu* cpu_especifica = (struct Cpu *) elemento;
    return cpu_especifica->ocupado == 0;
}

int encontrar_proceso(int id, void* elemento)
{
    struct pcb* proceso = (struct pcb*) elemento;
    return proceso->PID == id;
}

void* escucha_cpu_especifica(void* proceso_pasado)
{
    t_paquete *proceso_a_ejecutar = crear_paquete();
    struct pcb_execute* proceso = (struct pcb_execute*) proceso_pasado;
    agregar_a_paquete(proceso_a_ejecutar, proceso, sizeof(struct pcb_execute));     
    enviar_paquete(proceso_a_ejecutar, proceso->cpu_a_cargo->socket_dispatch, log_kernel);
        t_list* paquete_recibido = recibir_paquete(proceso->cpu_a_cargo->socket_dispatch, log_kernel);
        
        // [0] = Razón --> 0 = Exit, 1 = Init, 2 = Dump, 3 = I/O, 4 interrupcion
        // [1] = Parametro Reservado para el PID
        // [2] = PC
        // [3...] = Parametros adicionales.
        // IO --> Disp y tiempo / InitPrco -> Archivo y tamanio
        //  
        int tamanio_lista = list_size; 
        int pid_proceso_usado = (int)list_get(paquete_recibido, 1);
        if 	(list_get(paquete_recibido, 0) ==  0) //Exit --> TERMINA proceso, no interrumpe
        {
            //mandar_proceso de nuevo a ready 
            //revisar para Listas.
            log_info(log_kernel, ""); //Proceso para enviar metricas
            sem_t semaphore_execute_out;
            sem_wait(&semaphore_execute_out);
                list_remove_element ((t_list*)lista_execute, list_find((t_list*)lista_execute, encontrar_proceso(pid_proceso_usado, lista_execute)));
                proceso->cpu_a_cargo->ocupado=0;
            sem_post(&semaphore_execute_out); 
            return NULL;
        }
        else if (list_get(paquete_recibido, 0) ==  1) //Init
        {
            syscall_init_procc(list_get(paquete_recibido, 3), list_get(paquete_recibido, 4));
        }
        else if (list_get(paquete_recibido, 0) ==  2) //Dump
        {
            
        }
        else if (list_get(paquete_recibido, 0) ==  3) //IO
        {
            int existe_io = syscall_io((char*)list_get(paquete_recibido, 3), (int)list_get(paquete_recibido, 4), pid_proceso_usado);
            if (existe_io) //true
            {
                sem_t semaphore_execute_io;
                sem_wait(&semaphore_execute_io);
                struct pcb* proceso_a_bloquear = list_find((t_list*)lista_execute, encontrar_proceso(pid_proceso_usado, lista_execute));

                cambio_estado_bloqued(proceso_a_bloquear); //hacer función
                list_remove_element ((t_list*)lista_execute, list_find((t_list*)lista_execute, encontrar_proceso(pid_proceso_usado, lista_execute)));
                proceso->cpu_a_cargo->ocupado=0;
                sem_post(&semaphore_execute_io); 
            }
        }
}


void cambio_estado_execute(struct Cpu* cpu)
{
    //mutex
    sem_t semaphore_execute;

    sem_wait(&semaphore_execute);
        struct pcb* proceso = queue_pop(lista_ready);
        queue_push(lista_execute, proceso);
    sem_post(&semaphore_execute); 
    //mutex

    struct pcb_execute* proceso_a_ejecutar;
    proceso_a_ejecutar->pid = proceso->PID;
    proceso_a_ejecutar->cpu_a_cargo = cpu;
    proceso_a_ejecutar->pid = proceso->PC;

    pthread_t escucha_cpu_stream;
    pthread_create(&escucha_cpu_stream, NULL, escucha_cpu_especifica, (void*)proceso_a_ejecutar);
    //Struct o lista global?
    pthread_detach(escucha_cpu_stream);

}


void planificacion_corto_plazo()
{

    char* tipo_planificacion = config_get_string_value(config_kernel, "ALGORITMO_INGRESO_A_READY");
    if (strcmp(tipo_planificacion, "FIFO") == 0)
    {
        struct Cpu* cpu_libre = (struct Cpu*) list_find(lista_cpu, buscar_disponible);
        if (cpu_libre != NULL) //Cambiar a WHile?
        {
            cpu_libre->ocupado = 1;


        }   
        else
        {
            //Lo mejor seria un semaforo...
        }
    }
    else if (tipo_planificacion == "")
    {
        
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

    //Kernel "Core"
    config_kernel = iniciar_config("kernel");
    log_kernel = log_create("kernel.log", "kernel", false, LOG_LEVEL_INFO);
    
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
