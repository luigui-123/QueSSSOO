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

t_config* config_kernel;
t_log* log_kernel;

struct Cpu
{
    int socket_dispatch;
    int socket_interrupt;
    char* id;
    int ocupado;

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

void syscall_init_procc(char* tamanio, char* nombre, t_list* lista_new)
{
    struct pcb* proceso_nuevo;

    proceso_nuevo = malloc(sizeof(struct pcb));

    proceso_nuevo->PID = queue_size(lista_new);
    proceso_nuevo->tamanio = tamanio;
    proceso_nuevo->path = nombre;
    proceso_nuevo->PC = 0;

    queue_push(lista_new, proceso_nuevo);

}



void syscall_io(t_list* lista_io)
{



}

int peticion_memoria()
{
    char* puerto_memoria = config_get_string_value(config_kernel, "PUERTO_MEMORIA");
    char* ip_memoria = config_get_string_value(config_kernel, "IP_MEMORIA");
    int conexion_memoria = iniciar_conexion(ip_memoria, puerto_memoria,log_kernel);
    return conexion_memoria;
}

void escuchar_cpu(t_list* lista_cpu)
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


}

void escuchar_io(t_list* lista_io)
{
    char* puerto_escucha_io = config_get_string_value(config_kernel, "PUERTO_ESCUCHA_DISPATCH");
    int socket_io = iniciar_modulo(puerto_escucha_io, log_kernel);
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


void cambio_estado_ready(t_queue* lista_usada, t_queue* lista_ready, struct pcb* proceso)
{
       
    char* tamnio_proceso = proceso->tamanio;
    int conexion_memoria = peticion_memoria();
    enviar_mensaje(tamnio_proceso, conexion_memoria, log_kernel);
    if (recibir_mensaje(conexion_memoria, log_kernel) == "Ok")
    {
        queue_pop(lista_usada);
        queue_push(lista_ready, proceso);
    }  
    else
    {
        // Casteo el t_queu a T_list y sort moment

        //Se frena. --> Semaforo --> COngelar semaforo y esperar a que proceso entre a Finished
        //Revisar para escucha activa de otra manera.
        
    }
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

void planifacion_largo_plazo(t_queue* lista_new, t_queue* lista_ready, t_queue* lista_sus_ready)
{
    struct pcb* proceso;
    char* tipo_planificacion = config_get_string_value(config_kernel, "ALGORITMO_INGRESO_A_READY");
    if (tipo_planificacion == "FIFO")
    {
        if (!queue_is_empty(lista_sus_ready))
        {
            proceso = queue_peek(lista_sus_ready);
            cambio_estado_ready(lista_sus_ready, lista_ready ,proceso);

        }
        else if (!queue_is_empty(lista_new))
        {
            proceso = queue_peek(lista_new);
            cambio_estado_ready(lista_new, lista_ready ,proceso);
        }
        else
        {
            return;
        }
    }
    else if (tipo_planificacion == "PMCP")
    {
        struct pcb* proceso_chico;
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

bool buscar_disponible(void *elemento) 
{
    struct Cpu* cpu_especifica = (struct Cpu *) elemento;
    return cpu_especifica->ocupado == 0;
}

void escucha_cpu_especifica(int socket_cpu, struct pcb* proceso)
{
    t_paquete *proceso_a_ejecutar = crear_paquete();
    agregar_a_paquete(proceso_a_ejecutar, proceso, sizeof(struct pcb));
    enviar_paquete(proceso_a_ejecutar, socket_cpu, log_kernel);
}


void cambio_estado_execute(t_queue* lista_ready, t_queue* lista_execute, struct Cpu* cpu)
{
    struct pcb* proceso = queue_pop(lista_ready);
    queue_push(lista_execute, proceso);

    int socket_cpu = cpu->socket_dispatch;

    pthread_t escucha_cpu_stream;
    pthread_create(&escucha_cpu_stream, NULL, escucha_cpu_especifica, socket_cpu, proceso);
    pthread_detach(&escucha_cpu_stream);

}


void planificacion_corto_plazo(t_queue* lista_ready, t_queue* lista_execute, t_list* listas_cpu)
{

    char* tipo_planificacion = config_get_string_value(config_kernel, "ALGORITMO_INGRESO_A_READY");
    if (strcmp(tipo_planificacion, "FIFO") == 0)
    {
        struct Cpu* cpu_libre = (struct Cpu*) list_find(listas_cpu, buscar_disponible);
        if (cpu_libre != NULL)
        {
            cpu_libre->ocupado = 1;


        }   
        else
        {
            //pensar que puta mierda hago aca. 
            //Lo mejor seria un semaforo...
        }
    }
    else if (tipo_planificacion == "")
    {
        
    }
}

int main(int argc, char* argv[]) {
    //Procesos
    t_queue* lista_new = queue_create();
    t_queue* lista_ready = queue_create();
    t_queue* lista_sus_ready = queue_create();
    t_queue* lista_execute = queue_create();
    t_queue* lista_bloqued = queue_create();
    t_queue* lista_sus_bloqued = queue_create();
    t_queue* lista_finished = queue_create();

    //Listas de Modulos
    t_list* lista_cpu = list_create();
    t_list* lista_io = list_create();

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
