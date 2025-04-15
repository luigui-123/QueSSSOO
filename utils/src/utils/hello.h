#ifndef UTILS_HELLO_H_
#define UTILS_HELLO_H_

#include <stdlib.h>
#include <stdio.h>
#include <commons/config.h>
#include <commons/log.h>
/**
* @brief Imprime un saludo por consola
* @param quien Módulo desde donde se llama a la función
* @return No devuelve nada
*/
typedef enum
{
	MENSAJE,
	PAQUETE
}op_code;

typedef struct
{
	int size;
	void* stream;
} t_buffer;

typedef struct
{
	op_code codigo_operacion;
	t_buffer* buffer;
} t_paquete;

void saludar(char* quien);

int iniciar_modulo(char* puerto, t_log* log_modulo);

int iniciar_conexion(char* ip, char* puerto,t_log* log_modulo);

int establecer_conexion(int socket_servidor,t_log* log_modulo);

//void enviar_mensaje(char* mensaje, int socket_cliente);

void enviar_mensaje(char* mensaje, int socket_cliente,t_log* log_modulo);

void recibir_mensaje(int socket_cliente,t_log* log_modulo);


#endif
