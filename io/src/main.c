#include <utils/hello.h>
#include <commons/log.h>
#include <commons/config.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

t_config* iniciar_config()
{
	t_config* nuevo_config = config_create("io.conf");
	return nuevo_config;
}

t_config* io_conf;

int main(int argc, char* argv[]) {

    t_log * io_log = log_create("io.log", "io", false, LOG_LEVEL_INFO);
    io_conf = iniciar_config();

    char* ip_kernel = config_get_string_value(io_conf, "IP_KERNEL");
    char* puerto_kernel = config_get_string_value(io_conf, "PUERTO_KERNEL");
    int conexion_kernel = iniciar_conexion(ip_kernel, puerto_kernel,io_log);

    char* leido = "Ida";

    /*
    NO ENVIA BIEN
    NO ENVIA BIEN
    NO ENVIA BIEN
    */
    enviar_mensaje(leido,conexion_kernel);

    // Limpieza general
    close(conexion_kernel);
    log_destroy(io_log);
    config_destroy(io_conf);

    return 0;
}

