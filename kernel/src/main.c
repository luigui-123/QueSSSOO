#include <utils/hello.h>
#include <commons/log.h>
#include <commons/config.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <commons/collections/list.h>
#include <commons/collections/queue.h>



struct pcb
{
    int PID;
    int PC = 0;
    int MT [7];
    int ME [7];

    char* tamanio;
    char* path;

    //trabajo en progeso.
};

struct cpu
{
    int socket_dispatch;
    int socket_interrupt;
    char* id;

};

struct io
{
    int socket_io;
    char* nombre;
};


t_queue* lista_new = queue_create();
t_queue* lista_ready = queue_create();
t_queue* lista_sus_ready = queue_create();
t_queue* lista_execute = queue_create();
t_queue* lista_bloqued = queue_create();
t_queue* lista_sus_bloqued = queue_create();

t_list* lista_cpu = list_create();

t_list* lista_io = list_create();

t_config* iniciar_config()
{
	t_config* nuevo_config = config_create("kernel.conf");
	return nuevo_config;
}

void init_procc(char* tamanio, char* nombre)
{
    struct pcb proceso_nuevo;

    proceso_nuevo.PID = queue_size(lista_new);
    proceso_nuevo.tamanio = tamanio;
    proceso_nuevo.path = nombre;
    
    queue_push(lista_new, proceso_nuevo);
}

int peticion_memoria(t_config config_kernel, t_log log_kernel)
{
    char* puerto_memoria = config_get_string_value(config_kernel, "PUERTO_MEMORIA");
    char* ip_memoria = config_get_string_value(config_kernel, "IP_MEMORIA");
    int conexion_memoria = iniciar_conexion(ip_memoria, puerto_memoria,log_kernel);
    return conexion_memoria;
}

void escuchar_cpu(int socket_dispatch_listen, int socket_interrupt, t_log* log_kernel)
{
    int socket_conectado_dispatch = establecer_conexion(socket_dispatch_listen, log_kernel);
    int socket_conectado_interrupt = establecer_conexion(socket_interrupt, log_kernel);

    char* id = recibir_mensaje(socket_conectado_dispatch, log_kernel);

    struct cpu nueva_cpu;
    nueva_cpu.socket_dispatch = socket_conectado_dispatch;
    nueva_cpu.socket_interrupt = socket_conectado_interrupt;

    nueva_cpu.id = id;

    list_add(lista_cpu, nueva_cpu);

}

void escuchar_io(int socket_io, t_log* log_kernel)
{
    int socket_conectado_io = establecer_conexion(socket_io, log_kernel);
    char* nombre = recibir_mensaje(socket_conectado_io, log_kernel);

    struct io nueva_io;
    nueva_io.socket_io = socket_conectado_io;
    nueva_io.nombre = nombre;

    list_add(lista_io, nueva_io);
}

int main(int argc, char* argv[]) {
    t_config* config_kernel = iniciar_config("kernel");
    t_log *log_kernel = log_create("kernel.log", "kernel", false, LOG_LEVEL_INFO);
    t_queue* cola_procesos = queue_create();
    
    char *nombreArchivo = NULL;
    char *tamanioProceso = NULL;

    if (argc < 3)
    {
        log_info(log_kernel, "Error, Parametros INvalidos");
        return 1;
    }
    nombreArchivo = argv[1];
    tamanioProceso = argv[2];


    // Crea socket de memoria y conectar
    
    char* puerto_memoria = config_get_string_value(config_kernel, "PUERTO_MEMORIA");
    char* ip_memoria = config_get_string_value(config_kernel, "IP_MEMORIA");
    int conexion_memoria = iniciar_conexion(ip_memoria, puerto_memoria,log_kernel);
    

    // Crea socket de dispatch y servidor
    /*
    char* puerto_escucha_dispatch = config_get_string_value(config_kernel, "PUERTO_ESCUCHA_DISPATCH");
    int socket_dispatch_listen = iniciar_modulo(puerto_escucha_dispatch, log_kernel);
    int socket_conectado_dispatch = establecer_conexion(socket_dispatch_listen, log_kernel);
    */

    // Crea socket de interrupcion y servidor
    /*
    char* puerto_escucha_interrupt = config_get_string_value(config_kernel, "PUERTO_ESCUCHA_INTERRUPT");
    int socket_interrupt = iniciar_modulo(puerto_escucha_interrupt, log_kernel);
    int socket_conectado_interrupt = establecer_conexion(socket_interrupt, log_kernel);
    */

    // Crea socket de io y servidor
    
    char* puerto_io = config_get_string_value(config_kernel, "PUERTO_ESCUCHA_IO");
    int socket_io = iniciar_modulo(puerto_io, log_kernel);
    int socket_conectado_io = establecer_conexion(socket_io, log_kernel);

    //recibir_mensaje(socket_conectado_io,log_kernel);

    reenviar_mensaje(socket_conectado_io,conexion_memoria,log_kernel);
    reenviar_mensaje(conexion_memoria,socket_conectado_io,log_kernel);
    
    
    //close(socket_interrupt);
    close(socket_io);
    close(socket_conectado_io);
    //close(socket_conectado_interrupt);
    //close(socket_conectado_io);
    close(conexion_memoria);
    //close(socket_io);
    log_destroy(log_kernel);
    config_destroy(config_kernel);

    return 0;
}
