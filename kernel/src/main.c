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
};

t_config* iniciar_config()
{
	t_config* nuevo_config = config_create("kernel.conf");
	return nuevo_config;
}

t_log *log_kernel = NULL;

int main(int argc, char* argv[]) {

    t_config* config_kernel = iniciar_config("kernel");

    log_kernel = log_create("kernel.log", "kernel", false, LOG_LEVEL_INFO);

    // Crea socket de dispatch
    char* puerto_escucha_dispatch = config_get_string_value(config_kernel, "PUERTO_ESCUCHA_DISPATCH");
    int socket_dispatch = iniciar_modulo(puerto_escucha_dispatch, log_kernel);

    // Crea socket de interrupcion
    //char* puerto_escucha_interrupt = config_get_string_value(config_kernel, "PUERTO_ESCUCHA_INTERRUPT");
    //int socket_interrupt = iniciar_modulo(puerto_escucha_interrupt, log_kernel);
    
    int err = establecer_conexion(socket_dispatch, log_kernel);
    //int cpu_interrupcion = establecer_conexion(socket_interrupt, log_kernel);

    recibir_mensaje(socket_dispatch,log_kernel);
    enviar_mensaje('regreso',socket_dispatch);

    close(socket_dispatch);
    log_destroy(log_kernel);
    config_destroy(config_kernel);

    return 0;
}
