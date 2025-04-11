#include <utils/hello.h>
#include <commons/log.h>
#include <commons/config.h>
#include<sys/socket.h>
#include<netdb.h>


t_log *log_cpu = NULL;
t_config* cpu_conf = NULL;

t_config* iniciar_config()
{
	t_config* nuevo_config = config_create("cpu.conf");
	return nuevo_config;
}


int main(int argc, char* argv[]) 
{
    log_cpu = log_create("cpu.log", "cpu", true, LOG_LEVEL_INFO);
    cpu_conf = iniciar_config();  
    //inicia conexion con Kernel dispatch
    char* ip_kernel_dispatch = config_get_string_value(cpu_conf, "IP_KERNEL");
    char* puerto_kernel_disptach = config_get_string_value(cpu_conf, "PUERTO_KERNEL_DISPATCH");
    int conexion_kernel_dispatch = iniciar_conexion(ip_kernel_dispatch, puerto_kernel_disptach);

    log_info(log_cpu, "conexion creada?");
    //Inicia conexion con la memoria
    //int conexion_memoria = iniciar_conexion("8002");
    //inicia conexion con Kernel interrput
    //int conexion_kernel_interrput = iniciar_conexion("8003");

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
