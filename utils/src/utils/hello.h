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

int recibir_conexion(char* puerto);

t_config* iniciar_config(char* cofig);

int iniciar_conexion(int ip, int puerto);

#endif
