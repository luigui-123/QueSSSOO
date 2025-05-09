#include <utils/hello.h>
#include <commons/log.h>
#include <commons/config.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

t_config* iniciar_config()
{
	t_config* nuevo_config = config_create("cpu.conf");
	return nuevo_config;
}

int main(char* id_cpu) 
{
    
    t_log *log_cpu = log_create("cpu.log", "cpu", false, LOG_LEVEL_INFO);
    log_info(log_cpu,id_cpu);
    t_config*cpu_conf = iniciar_config(); 

    // Inicia conexion con Kernel dispatch
    
    char* ip_kernel_dispatch = config_get_string_value(cpu_conf, "IP_KERNEL");
    char* puerto_kernel_dispatch = config_get_string_value(cpu_conf, "PUERTO_KERNEL_DISPATCH");
    int conexion_kernel_dispatch = iniciar_conexion(ip_kernel_dispatch, puerto_kernel_dispatch,log_cpu);
    
    // Inicia conexion con Kernel interrupcion
    /*
    char* ip_kernel_interrupt = config_get_string_value(cpu_conf, "IP_KERNEL");
    char* puerto_kernel_interrupt = config_get_string_value(cpu_conf, "PUERTO_KERNEL_INTERRUPT");
    int conexion_kernel_interrupt = iniciar_conexion(ip_kernel_interrupt, puerto_kernel_interrupt,log_cpu);
    */

    // Inicia conexion con Memoria
    /*
    char* ip_memoria = config_get_string_value(cpu_conf, "IP_MEMORIA");
    char* puerto_memoria = config_get_string_value(cpu_conf, "PUERTO_MEMORIA");
    int conexion_memoria = iniciar_conexion(ip_memoria, puerto_memoria,log_cpu);
    */
    
    char* leido = config_get_string_value(cpu_conf, "REEMPLAZO_CACHE");





    //enviar_mensaje(leido,conexion_memoria);
    enviar_mensaje(leido,conexion_kernel_dispatch, log_cpu);
    
    //enviar cpu_id al kernel
     enviar_mensaje(leido,conexion_kernel_dispatch, id_cpu);

    // Limpieza general
    close(conexion_kernel_dispatch);
    //close(conexion_memoria);
    log_destroy(log_cpu);
    config_destroy(cpu_conf);

    return 0;
}


int recibir_procesos(int conexion, char *id_cpu)
{
    //IMplementar escucha del CPU para recibir los PID y el PC --> una vez los reciba

    return 0;
}

int procesamiento(int pid, int pc, int conexion_memoria)
{
    //procesamiento en proceso
    return 0;
}
