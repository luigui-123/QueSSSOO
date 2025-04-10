#include <utils/hello.h>
#include <commons/log.h>
#include <commons/config.h>

t_log *log = log_create("io.log", "io", true, LOG_LEVEL_INFO);
t_config* config = iniciar_config("io");



int main(int argc, char* argv[]) {

    if (argc<2)
    {
       log_info(log, "Argumentos invalidos");
       return -1; 
    }
    
    log_info(log, "Bienvenido a la interfaz IO, dispositivo: %s", argv[1]);
    
    iniciar_conexion(config_get_int_value(config, "IP_KERNEL"), config_get_int_value(config, "PUERTO_KERNEL") )

    return 0;
}

