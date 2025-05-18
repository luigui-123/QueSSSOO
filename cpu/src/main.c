#include <utils/hello.h>
#include <commons/log.h>
#include <commons/config.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <stdbool.h>

t_config* iniciar_config()
{
	t_config* nuevo_config = config_create("cpu.conf");
	return nuevo_config;
}
int conectar_kernel_interrupt(void)
{

    char* ip_kernel_interrupt = config_get_string_value(cpu_conf, "IP_KERNEL");
    char* puerto_kernel_interrupt = config_get_string_value(cpu_conf, "PUERTO_KERNEL_INTERRUPT");
    int conexion_kernel_interrupt = iniciar_conexion(ip_kernel_interrupt, puerto_kernel_interrupt,log_cpu);
    return conexion_kernel_interrupt;
}

int conectar_kernel_dispatch(void)
{
    char* ip_kernel_dispatch = config_get_string_value(cpu_conf, "IP_KERNEL");
    char* puerto_kernel_dispatch = config_get_string_value(cpu_conf, "PUERTO_KERNEL_DISPATCH");
    int conexion_kernel_dispatch = iniciar_conexion(ip_kernel_dispatch, puerto_kernel_dispatch,log_cpu);
    return conectar_kernel_dispatch;
}

int conectar_memoria(void)
{
    char* ip_memoria = config_get_string_value(cpu_conf, "IP_MEMORIA");
    char* puerto_memoria = config_get_string_value(cpu_conf, "PUERTO_MEMORIA");
    int conexion_memoria = iniciar_conexion(ip_memoria, puerto_memoria,log_cpu);
    return conectar_memoria;
}

int  tamanio_tlb = config_get_int_value (cpu.conf,"ENTRADAS_TLB");
// Estructura para una entrada de la TLB
typedef struct {
    int pagina;
    int marco;
    bool validez;    // Indica si la entrada es válida
    int menos_usado;   // Registro del último uso (para LRU)
} TLBEntrada;

// Estructura para simular la TLB
typedef struct {
    TLBEntrada entrada[tamanio_tlb];
    int contador_acceso; // Contador global para registrar accesos
} TLB;

// Inicializar la TLB
void inicializar_tlb(TLB *tlb) {
    for (int i = 0; i < tamanio_tlb; i++) {
        tlb- entrada[i].validez = false; // Todas las entradas comienzan como inválidas
        tlb- entrada[i].menos_usado = -1; // Sin historial de uso
    }
    tlb->contador_acceso = 0; // Inicializa el contador global
}

// Buscar en la TLB
int buscar_tlb(TLB *tlb, int pagina) {
    for (int i = 0; i < tamanio_tlb; i++) {
        if (tlb- entrada[i].validez && tlb- entrada[i].pagina == pagina) {
            // Actualiza el registro de último uso
            tlb- entrada[i].menos_usado = tlb->contador_acceso++;
            printf("TLB hit: Página %d -> Marco %d\n", pagina, tlb- entrada[i].marco);
            return tlb- entrada[i].marco; // Devuelve el número de marco
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
        if (!tlb- entrada[i].validez) {
            // Si encontramos una entrada inválida, podemos usarla directamente
            return i;
        }
        if (tlb- entrada[i].menos_usado < min_menos_usado) {
            min_menos_usado = tlb- entrada[i].menos_usado;
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
void calcular_pagina_y_desplazamiento(unsigned int direccion_logica, int &numero_pagina, int &desplazamiento, int tamanio_pagina) {
    numero_pagina = direccion_logica / tamanio_pagina;   // Número de página
    desplazamiento = direccion_logica % tamanio_pagina;       // Desplazamiento (resto)
}

//Etapas del ciclo de instruccion
void recibir_proceso(int conexion,int &pid, int &pc)
{
    recv(conexion, &pid, sizeof(int), MSG_WAITALL);
    recv(conexion, &pc, sizeof(int), MSG_WAITALL);
}


char pedir_instruccion(int pid, int pc)
{
    send(conexion_memoria, &pid, sizeof(int), 0);
    send(conexion_memoria, &pc, sizeof(int), 0);
    char* instruccion= recibir_mensaje(conexion_memoria,log_cpu);
    return instruccion;
}




int main(char* id_cpu) 
{
    t_log *log_cpu = log_create("cpu.log", "cpu", false, LOG_LEVEL_INFO);

    log_info(log_cpu,id_cpu);

    t_config*cpu_conf = iniciar_config(); 



    // Inicia conexion_kernel_dispatch con Kernel dispatch
    int conexion_kernel_dispatch = conectar_kernel_dispatch();
    
    //enviar cpu_id al kernel
    enviar_mensaje(leido,conexion_kernel_dispatch, id_cpu);
    
    // Inicia conexion_kernel_dispatch con Kernel interrupcion
    int conexion_kernel_interrupt= conectar_kernel_interrupt();
    

    // Inicia conexion_kernel_dispatch con Memoria
    int conexion_memoria=conectar_memoria()
    
    
    char* leido = config_get_string_value(cpu_conf, "REEMPLAZO_CACHE");


    //enviar_mensaje(leido,conexion_memoria);
    enviar_mensaje(leido,conexion_kernel_dispatch, log_cpu);
    
    

    // Limpieza general
    close(conexion_kernel_dispatch);
    //close(conexion_memoria);
    log_destroy(log_cpu);
    config_destroy(cpu_conf);

    return 0;
}







