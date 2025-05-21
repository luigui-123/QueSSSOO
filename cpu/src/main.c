#include <utils/hello.h>
#include <commons/log.h>
#include <commons/config.h>
#include <commons/string.h>
#include <commons/collections/list.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <stdbool.h>

typedef struct
{
    int tipo;
	int pid;
	int pc;
} cpuinfo;

typedef struct
{
    int tipo;
    int pid;
    int pc;
} syscallinfo;

typedef struct
{
    int tipo;   //1-Read / 2-Write
    int pid;
    int direccion;
} memoriainfo;

t_config* iniciar_config()
{
	t_config* nuevo_config = config_create("cpu.conf");
	return nuevo_config;
}
int conectar_kernel_interrupt(t_log *log_cpu)
{

    char* ip_kernel_interrupt = config_get_string_value("cpu_conf", "IP_KERNEL");
    char* puerto_kernel_interrupt = config_get_string_value("cpu_conf", "PUERTO_KERNEL_INTERRUPT");
    int conexion_kernel_interrupt = iniciar_conexion(ip_kernel_interrupt, puerto_kernel_interrupt,log_cpu);
    return conexion_kernel_interrupt;
}

int conectar_kernel_dispatch(t_log *log_cpu)
{
    char* ip_kernel_dispatch = config_get_string_value("cpu_conf", "IP_KERNEL");
    char* puerto_kernel_dispatch = config_get_string_value("cpu_conf", "PUERTO_KERNEL_DISPATCH");
    int conexion_kernel_dispatch = iniciar_conexion(ip_kernel_dispatch, puerto_kernel_dispatch,log_cpu);
    return conexion_kernel_dispatch;
}

int conectar_memoria(t_log *log_cpu)
{
    char* ip_memoria = config_get_string_value("cpu_conf", "IP_MEMORIA");
    char* puerto_memoria = config_get_string_value("cpu_conf", "PUERTO_MEMORIA");
    int conexion_memoria = iniciar_conexion(ip_memoria, puerto_memoria,log_cpu);
    return conexion_memoria;
}

int  tamanio_tlb = config_get_int_value ("cpu.conf","ENTRADAS_TLB");
// Estructura para una entrada de la TLB
typedef struct {
    int pagina;
    int marco;
    bool validez;    // Indica si la entrada es válida
    int menos_usado;   // Registro del último uso (para LRU)
} TLBEntrada;

// Estructura para simular la TLB
typedef struct {
    TLBEntrada *entrada[tamanio_tlb];
    int contador_acceso; // Contador global para registrar accesos
} TLB;

// Inicializar la TLB
void inicializar_tlb(TLB *tlb) {
    for (int i = 0; i < tamanio_tlb; i++) {
        tlb->entrada[i].validez = false; // Todas las entradas comienzan como inválidas
        tlb->entrada[i].menos_usado = -1; // Sin historial de uso
    }
    tlb->contador_acceso = 0; // Inicializa el contador global
}

// Buscar en la TLB
int buscar_tlb(TLB *tlb, int pagina) {
    for (int i = 0; i < tamanio_tlb; i++) {
        if (tlb->entrada[i].validez && tlb->entrada[i].pagina == pagina) {
            // Actualiza el registro de último uso
            tlb->entrada[i].menos_usado = tlb->contador_acceso++;
            printf("TLB hit: Página %d -> Marco %d\n", pagina, tlb->entrada[i].marco);
            return tlb->entrada[i].marco; // Devuelve el número de marco
        }
    }
    printf("TLB miss para la página %d\n", pagina);
    return -1; // Si no se encuentra, devuelve -1
}

// Encuentra el índice LRU para reemplazo
int buscar_entrada_lru(TLB *tlb) {
    int indice_lru = -1;
    int min_menos_usado = __INT_MAX__; // Un valor inicial alto

    for (int i = 0; i < tamanio_tlb; i++) {
        if (!tlb->entrada[i].validez) {
            // Si encontramos una entrada inválida, podemos usarla directamente
            return i;
        }
        if (tlb->entrada[i].menos_usado < min_menos_usado) {
            min_menos_usado = tlb->entrada[i].menos_usado;
            indice_lru = i;
        }
    }
    return indice_lru;
}

// Actualizar la TLB usando LRU
void actualizar_tlb(TLB *tlb, int pagina, int marco) {
    // Encuentra el índice a reemplazar según LRU
    int indice = buscar_entrada_lru(tlb);

    // Reemplazar la entrada
    tlb-> entrada[indice].pagina = pagina;
    tlb-> entrada[indice].marco = marco;
    tlb-> entrada[indice].validez = true;
    tlb-> entrada[indice].menos_usado = tlb->contador_acceso++;

    printf("TLB actualizado: Página %d -> Marco %d (Reemplazando entrada %d)\n", 
           pagina, marco, indice);
}


//Calcular numero de pagina y desplzamiento
void calcular_pagina_y_desplazamiento(unsigned int direccion_logica, int &numero_pagina, int &desplazamiento, int tamanio_pagina)
{
    numero_pagina = direccion_logica / tamanio_pagina;   // Número de página
    desplazamiento = direccion_logica % tamanio_pagina;       // Desplazamiento (resto)
}

int main(char* id_cpu) 
{
    char *nombre_log_cpu = strcat("cpu.log", id_cpu);

    t_log *log_cpu = log_create(nombre_log_cpu, "cpu", false, LOG_LEVEL_INFO);

    log_info(log_cpu,id_cpu);

    t_config*cpu_conf = iniciar_config(); 



    // Inicia conexion_kernel_dispatch con Kernel dispatch
    int conexion_kernel_dispatch = conectar_kernel_dispatch(log_cpu);
    
    //enviar cpu_id al kernel
    enviar_mensaje(id_cpu,conexion_kernel_dispatch, log_cpu);
    
    // Inicia conexion_kernel_dispatch con Kernel interrupcion
    int conexion_kernel_interrupt= conectar_kernel_interrupt(log_cpu);
    

    // Inicia conexion_kernel_dispatch con Memoria
    int conexion_memoria=conectar_memoria(log_cpu);
    
    
    char* leido = config_get_string_value(cpu_conf, "REEMPLAZO_CACHE");


    //enviar_mensaje(leido,conexion_memoria);
    enviar_mensaje(leido,conexion_memoria, nombre_log_cpu);
    
    //enviar cpu_id al kernel
    enviar_mensaje(leido,conexion_kernel_dispatch, id_cpu);

    t_list *proceso;
    char *instruccion;
    bool interrupcion;

    //while (cpu conectada){
        proceso = recibir_procesos(conexion_kernel_dispatch, id_cpu);
        cpuinfo *procesocpu;
        procesocpu = malloc(sizeof(cpuinfo));
        procesocpu->tipo = 0;
        procesocpu->pid = list_get(proceso, 0);
        procesocpu->pc = list_get(proceso, 1);
        do{
            instruccion = obtener_instruccion(procesocpu, conexion_memoria, log_cpu);
            decodear_y_ejecutar_instruccion(instruccion, procesocpu, conexion_memoria, conexion_kernel_dispatch, log_cpu);
            //interrupcion = check_interrupt()
        }while(!interrupcion);
        t_paquete *paquete = crear_paquete();
        agregar_a_paquete(paquete, procesocpu, sizeof(cpuinfo));
        enviar_paquete(paquete, conectar_kernel_dispatch, log_cpu);
        free(procesocpu);
    //}

    // Limpieza general
    close(conexion_kernel_dispatch);
    close(conexion_memoria);
    log_destroy(log_cpu);
    config_destroy(cpu_conf);

    return 0;
}

t_list *recibir_procesos(int conexion, t_log *log_cpu)
{
    t_list *proceso;
    proceso = recibir_paquete(conexion, log_cpu);

    return proceso;
}

char *obtener_instruccion(cpuinfo *procesocpu, int conexion_memoria, t_log *log_cpu)
{
    char * instruccion;
    t_paquete *paquete = crear_paquete();
    agregar_a_paquete(paquete, procesocpu, sizeof(cpuinfo));
    enviar_paquete(paquete, conexion_memoria, log_cpu);
    instruccion = recibir_mensaje(conexion_memoria, log_cpu);

    return instruccion;
}

void decodear_y_ejecutar_instruccion(char *instruccion, cpuinfo *proceso, int conexion_memoria, int conexion_kernel, t_log *log_cpu)
{
    char *instruccion_separada[] = string_split(instruccion, " ");
    string_to_upper(instruccion_separada[0]);
    if(instruccion_separada[0] == "WRITE"){
        //int dir_fisica = traducir_direccion(instruccion_separada[1]);

        
        proceso->pc = proceso->pc + 1;

    } else if(instruccion_separada[0] == "READ"){
        //int dir_fisica = traducir_direccion(instruccion_separada[1]);
        //send();
        //recv();
        //log_info(log_cpu, valor_leido);
        proceso->pc = proceso->pc + 1;

    } else if(instruccion_separada[0] == "GOTO"){
        proceso->pc = atoi(instruccion_separada[1]);

    } else if(instruccion_separada[0] == "IO"){
        syscallinfo *io;
        io = malloc(sizeof(syscallinfo));
        io->tipo = 3;
        io->pid = proceso->pid;
        io->pc = proceso->pc + 1;
        char *dispositivo = instruccion_separada[1];
        int time = atoi(instruccion_separada[2]);
        t_paquete *paquete;
        agregar_a_paquete(paquete, io, sizeof(syscallinfo));
        agregar_a_paquete(paquete, dispositivo, sizeof(dispositivo));
        agregar_a_paquete(paquete, time, sizeof(int));
        enviar_paquete(paquete, conexion_kernel, log_cpu);
        free(io);
        proceso->pc = proceso->pc + 1;

    } else if(instruccion_separada[0] == "INIT_PROC"){
        syscallinfo *init;
        init = malloc(sizeof(syscallinfo));
        init->tipo = 1;
        init->pid = proceso->pid;
        init->pc = proceso->pc + 1;
        char *archivo = instruccion_separada[1];
        int tamanio = atoi(instruccion_separada[2]);
        t_paquete *paquete;
        agregar_a_paquete(paquete, init, sizeof(syscallinfo));
        agregar_a_paquete(paquete, archivo, sizeof(archivo));
        agregar_a_paquete(paquete, tamanio, sizeof(int));
        enviar_paquete(paquete, conexion_kernel, log_cpu);
        free(init);
        proceso->pc = proceso->pc + 1;

    } else if(instruccion_separada[0] == "DUMP_MEMORY"){
        syscallinfo *dump;
        dump = malloc(sizeof(syscallinfo));
        dump->tipo = 2;
        dump->pid = proceso->pid;
        dump->pc = proceso->pc + 1;
        t_paquete *paquete;
        agregar_a_paquete(paquete, dump, sizeof(syscallinfo));
        enviar_paquete(paquete, conexion_kernel, log_cpu);
        free(dump);
        proceso->pc = proceso->pc + 1;

    } else if(instruccion_separada[0] == "EXIT"){
        syscallinfo *exit;
        exit = malloc(sizeof(syscallinfo));
        exit->tipo = 0;
        exit->pid = proceso->pid;
        exit->pc = proceso->pc;
        t_paquete *paquete;
        agregar_a_paquete(paquete, exit, sizeof(syscallinfo));
        enviar_paquete(paquete, conexion_kernel, log_cpu);
        free(exit);
    }

    return 0;
}
