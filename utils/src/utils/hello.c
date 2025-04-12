#include <utils/hello.h>
#include <commons/config.h>
#include <commons/log.h>
#include<sys/socket.h>
#include<netdb.h>
#include <commons/bitarray.h>

void saludar(char* quien) 
{
    printf("Hola desde %s!!\n", quien);
}

void* serializar_paquete(t_paquete* paquete, int bytes)
{
	void * magic = malloc(bytes);
	int desplazamiento = 0;

	memcpy(magic + desplazamiento, &(paquete->codigo_operacion), sizeof(int));
	desplazamiento+= sizeof(int);
	memcpy(magic + desplazamiento, &(paquete->buffer->size), sizeof(int));
	desplazamiento+= sizeof(int);
	memcpy(magic + desplazamiento, paquete->buffer->stream, paquete->buffer->size);
	desplazamiento+= paquete->buffer->size;

	return magic;
}

int iniciar_modulo(char* puerto)
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


int iniciar_conexion(char* ip, char* puerto)
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


int establecer_conexion(int socket_escucha)
{
	// Quitar esta línea cuando hayamos terminado de implementar la funcion

	// Aceptamos un nuevo cliente
	int socket_conectado = accept(socket_escucha, NULL, NULL);
	if (socket_conectado == -1) {
		return -1;
	}
	return socket_conectado;
}

void eliminar_paquete(t_paquete* paquete)
{
	free(paquete->buffer->stream);
	free(paquete->buffer);
	free(paquete);
}

void enviar_mensaje(char* mensaje, int socket_cliente)
{
	t_paquete* paquete = malloc(sizeof(t_paquete));

	paquete->codigo_operacion = MENSAJE;
	paquete->buffer = malloc(sizeof(t_buffer));
	paquete->buffer->size = strlen(mensaje) + 1;
	paquete->buffer->stream = malloc(paquete->buffer->size);
	memcpy(paquete->buffer->stream, mensaje, paquete->buffer->size);

	int bytes = paquete->buffer->size + 2*sizeof(int);

	void* a_enviar = serializar_paquete(paquete, bytes);

	send(socket_cliente, a_enviar, bytes, 0);

	free(a_enviar);
	eliminar_paquete(paquete);
}