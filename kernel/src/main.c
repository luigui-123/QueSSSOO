#include <utils/hello.h>
#include <commons/log.h>
#include <commons/config.h>
#include<sys/socket.h>
#include<netdb.h>

struct PCB
{
    int PID;
    int PC;
    enum ME;
//Declarar
    enum MT;
//Declarar
};

int conexion_cpu_ejecutar;
int conexion_cpu_interrupciones;

t_config* config == iniciar_config();
t_log *log = log_create("kernel.log", "kernel", true, LOG_LEVEL_INFO);


int main(int argc, char* argv[]) {
    //saludar("kernel");
    char *nombreArchivo = NULL;
    char *tamanioProceso = NULL;

    if (argc < 3)
    {
        log_info(log, "Error, Parametros INvalidos");
        return 1;
    }
    nombreArchivo = argv[1];
    tamanioProceso = argv[2];

    log_info(log, "Archivo: %s, tamanio: %s",nombreArchivo, tamanioProceso);

    return 0;
}

t_config* iniciar_config(void)
{
	t_config* nuevo_config = config_create("kernel.config");


	return nuevo_config;
}

int iniciar_conexion(char *ip, char* puerto)
{
	struct addrinfo hints;
	struct addrinfo *modulo_2;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	getaddrinfo(ip, puerto, &hints, &modulo_2);

	// Ahora vamos a crear el socket.
	int socket_a_crear = socket(modulo_2->ai_family,
                        modulo_2->ai_socktype,
                        modulo_2->ai_protocol);
	// Ahora que tenemos el socket, vamos a conectarlo
	
	connect(socket_a_crear, modulo_2->ai_addr, modulo_2->ai_addrlen);


	freeaddrinfo(modulo_2);

	return socket_a_crear;
}
