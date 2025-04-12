#include <utils/hello.h>
#include <commons/log.h>
#include <commons/config.h>
#include<sys/socket.h>
#include<netdb.h>

struct PCB
{
    int PID;
    int PC;
    
};

t_config* iniciar_config()
{
	t_config* nuevo_config = config_create("kernel.conf");
	return nuevo_config;
}

t_log *log_kernel = NULL;

int main(int argc, char* argv[]) {
    //saludar("kernel");

    t_config* config_kernel = iniciar_config("kernel");

    log_kernel = log_create("kernel.log", "kernel", true, LOG_LEVEL_INFO);
    /*
    char *nombreArchivo = NULL;
    char *tamanioProceso = NULL;

    if (argc < 3)
    {
        log_info(log_kernel, "Error, Parametros INvalidos");
        return 1;
    }
    nombreArchivo = argv[1];
    tamanioProceso = argv[2];

    log_info(log_kernel, "Archivo: %s, tamanio: %s",nombreArchivo, tamanioProceso);
    */
    char* puerto_escucha_dispatch = config_get_string_value(config_kernel, "PUERTO_ESCUCHA_DISPATCH");
    int socket_dispatch = iniciar_modulo(puerto_escucha_dispatch);
    
    int cpu_conectada = establecer_conexion(socket_dispatch);

    

    if (cpu_conectada == -1)
    {
            log_info(log_kernel, "No Funco quizas?");
    }
    log_info(log_kernel, "Funco quizas?");


    return 0;
}

