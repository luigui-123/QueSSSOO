#include <utils/hello.h>
#include <commons/log.h>
#include <commons/config.h>
#include <commons/string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>

t_config* iniciar_config()
{
	t_config* nuevo_config = config_create("/home/utnso/Desktop/tp-2025-1c-RompeComputadoras/io/io.conf");
	return nuevo_config;
}

t_config* io_conf;

int main(int argc, char* argv[]) {

    t_log * io_log = log_create("io.log", "io", true, LOG_LEVEL_TRACE);
    io_conf = iniciar_config();

    char* ip_kernel = config_get_string_value(io_conf, "IP_KERNEL");
    char* puerto_kernel = config_get_string_value(io_conf, "PUERTO_KERNEL");
    int conexion_kernel = iniciar_conexion(ip_kernel, puerto_kernel);

    char* nombre_io = argv[1];

    char* mensajeFin = string_from_format("La solicitud de %s ha finalizado", nombre_io);

    enviar_mensaje(nombre_io, conexion_kernel);
    free(recibir_mensaje(conexion_kernel));
    bool finalizar = false;
    while (!finalizar)
    {
    
        t_list *proceso;
        proceso = recibir_paquete(conexion_kernel);
        if(list_is_empty(proceso)){
            list_destroy(proceso);
            finalizar = true;
        } else {
            log_info(io_log, "PID: %d - Inicio de IO - Tiempo: %d", *(int*)(list_get(proceso, 0)), *(int*)(list_get(proceso, 1)));

            unsigned int tiempo = *((int *)list_get(proceso, 1)) * 1000;

            usleep(tiempo);

            log_info(io_log, "PID: %d - Fin de IO", *(int*)(list_get(proceso, 0)));
            enviar_mensaje(mensajeFin, conexion_kernel);

            list_destroy_and_destroy_elements(proceso,free);
        }

    }
    free(mensajeFin);

    // Limpieza general
    close(conexion_kernel);
    log_destroy(io_log);
    config_destroy(io_conf);

    return 0;
}
