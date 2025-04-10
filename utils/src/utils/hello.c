#include <utils/hello.h>
#include <commons/config.h>
#include <commons/log.h>
#include<sys/socket.h>
#include<netdb.h>

void saludar(char* quien) {
    printf("Hola desde %s!!\n", quien);
}

t_config* iniciar_config(char* test)
{
	t_config* nuevo_config = config_create(string_from_vformat("%s.conf", test));
	return nuevo_config;
}

int recibir_conexion(char* puerto)
{
	// Quitar esta línea cuando hayamos terminado de implementar la funcion

	int socket_cpu;

	struct addrinfo hints, *servinfo, *p;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	int err = getaddrinfo(NULL, puerto, &hints, &servinfo);
	if (err == -1)
	{
		printf("Error en la cpu, Getadderifno");
	}
	socket_cpu = socket(servinfo->ai_family,
                        servinfo->ai_socktype,
                        servinfo->ai_protocol);
	// Creamos el socket de escucha del servidor

  	err = setsockopt(socket_cpu, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof(int));
	if (err == -1)
		printf("Error en la cpu, setsockopt");

	// Asociamos el socket a un puerto
	
  	err = bind(socket_cpu, servinfo->ai_addr, servinfo->ai_addrlen);
	if (err == -1)
		printf("Error en la cpu, bind");

	// Escuchamos las conexiones entrantes
	err = listen(socket_cpu, SOMAXCONN);

	freeaddrinfo(servinfo);
	return socket_cpu;
}


int iniciar_conexion(int ip, int puerto)
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