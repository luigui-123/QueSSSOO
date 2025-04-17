#include <utils/hello.h>
#include <commons/log.h>
#include <commons/config.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>


struct PCB
{
    int PID;
    int PC;
    int MT [7];
    //trabajo en progeso.
};

t_config* iniciar_config()
{
	t_config* nuevo_config = config_create("kernel.conf");
	return nuevo_config;
}

int iniciar_socket_servidor(char* puerto, t_log* log)
{
    int socket = iniciar_modulo(puerto, log);
    return socket;
}

int conectar_modulo(char* puerto,char* ip, t_log* log)
{

    int conexion = iniciar_conexion(ip, puerto,log);
    return conexion;
}

int main(int argc, char* argv[]) {

    t_config* config_kernel = iniciar_config("kernel");
    t_log *log_kernel = log_create("kernel.log", "kernel", false, LOG_LEVEL_INFO);

    // Crea socket de dispatch y servidor
    
    char* puerto_escucha_dispatch = config_get_string_value(config_kernel, "PUERTO_ESCUCHA_DISPATCH");
    int socket_dispatch_listen = iniciar_modulo(puerto_escucha_dispatch, log_kernel);
    int socket_conectado_dispatch = establecer_conexion(socket_dispatch_listen, log_kernel);
    

    // Crea socket de interrupcion y servidor
    /*
    char* puerto_escucha_interrupt = config_get_string_value(config_kernel, "PUERTO_ESCUCHA_INTERRUPT");
    int socket_interrupt = iniciar_modulo(puerto_escucha_interrupt, log_kernel);
    int socket_conectado_interrupt = establecer_conexion(socket_interrupt, log_kernel);
    */

    // Crea socket de io y servidor
    /*
    char* puerto_io = config_get_string_value(config_kernel, "PUERTO_ESCUCHA_IO");
    int socket_io = iniciar_modulo(puerto_io, log_kernel);
    int socket_conectado_io = establecer_conexion(socket_io, log_kernel);
    /*

    // Crea socket de memoria y conectar
    /*
    char* puerto_memoria = config_get_string_value(config_kernel, "PUERTO_MEMORIA");
    char* ip_memoria = config_get_string_value(config_kernel, "IP_MEMORIA");
    int conexion_memoria = iniciar_conexion(ip_memoria, puerto_memoria,log_kernel);
    */

    recibir_mensaje(socket_conectado_dispatch,log_kernel);
    
    //close(socket_interrupt);
    close(socket_dispatch_listen);
    close(socket_conectado_dispatch);
    //close(socket_conectado_interrupt);
    //close(socket_conectado_io);
    //close(conexion_memoria);
    //close(socket_io);
    log_destroy(log_kernel);
    config_destroy(config_kernel);

    return 0;
}
