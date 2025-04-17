#include <utils/hello.h>
#include <commons/log.h>
#include <commons/config.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

t_config* iniciar_config()
{
	t_config* nuevo_conf = config_create("memoria.conf");
	return nuevo_conf;
}

t_config* memo_conf;
t_log *log_memo;

int iniciar_socket_servidor(char* puerto, t_log* log)
{
    int socket = iniciar_modulo(puerto, log);
    return socket
}


int main(int argc, char* argv[]) {
    
    log_memo = log_create("memoria.log", "memoria", false, LOG_LEVEL_INFO);
    memo_conf = iniciar_config(); 
    
    // Crea socket y espera
    char* puerto_escucha= config_get_string_value(memo_conf, "PUERTO_ESCUCHA");
    int socket_escucha = iniciar_modulo(puerto_escucha, log_memo);
    
    // Acepta conexion
    int socket_conectado = establecer_conexion(socket_escucha, log_memo);

    // Recibe mensaje    
    recibir_mensaje(socket_conectado,log_memo);
    
    // Limpieza general
    close(socket_escucha);
    close(socket_conectado);

    log_destroy(log_memo);
    config_destroy(memo_conf);

    return 0;
}
