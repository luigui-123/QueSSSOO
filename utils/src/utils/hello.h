#ifndef UTILS_HELLO_H_
#define UTILS_HELLO_H_

#include <stdlib.h>
#include <stdio.h>
#include <commons/config.h>

/**
* @brief Imprime un saludo por consola
* @param quien Módulo desde donde se llama a la función
* @return No devuelve nada
*/
void saludar(char* quien);

int iniciar_modulo(char* puerto);

int iniciar_conexion(char* ip, char* puerto);

int establecer_conexion(int socket_servidor);

#endif
