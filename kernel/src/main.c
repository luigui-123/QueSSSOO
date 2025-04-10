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


t_log *log_kernel;

int main(int argc, char* argv[]) {
    //saludar("kernel");

    int conexion_cpu_ejecutar;
    int conexion_cpu_interrupciones;

    t_config* config = iniciar_config("kernel");

    log_kernel = log_create("kernel.log", "kernel", true, LOG_LEVEL_INFO);

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

    return 0;
}
