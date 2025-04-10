#include <utils/hello.h>
#include <commons/log.h>
#include <commons/config.h>
#include<sys/socket.h>
#include<netdb.h>


t_log *log = log_create("cpu.log", "cpu", true, LOG_LEVEL_INFO);
t_config* config = iniciar_config("cpu");

int main(int argc, char* argv[]) 
{
    //inicia conexion con Kernel dispatch
    int conexion_kernel_dispatch = recibir_conexion("8001");
    //Inicia conexion con la memoria
    int conexion_memoria = recibir_conexion("8002");
    //inicia conexion con Kernel interrput
    int conexion_kernel_interrput recibir_conexion("8003");

}

int recibir_procesos(int conexion)
{
    //IMplementar escucha del CPU para recibir los PID y el PC --> una vez los reciba

    return 0;
}

int procesamiento(int pid, int pc, int conexion_memoria)
{
    //procesamiento en proceso
}
