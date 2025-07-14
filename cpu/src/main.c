#include <commons/log.h>
#include <commons/config.h>
#include <commons/string.h>
#include <commons/collections/list.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <stdbool.h>
#include <utils/hello.h>
#include <pthread.h>
#include <semaphore.h>

//algunas variables globales
#define TAMANIO_PAGINA 64
#define ENTRADAS_POR_TABLA 4
#define ENTRADAS_CACHE 2
#define RETARDO_CACHE 250
#define ENTRADAS_TLB 4

//int ENTRADAS_CACHE =  config_get_int_value("cpu_conf", "ENTRADAS_CACHE");
//int RETARDO_CACHE = config_get_int_value("cpu_conf", "RETARDO_CACHE");
//int  ENTRADAS_TLB = config_get_int_value ("cpu.conf","ENTRADAS_TLB");

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
    int tipo;   //1-Read / 2-Write / 3-Solicitar Pagina (Cache)
    int pid;
    int direccion;
} memoriainfo;

typedef struct 
{
    int conexion;
    t_log* log;
} infohilointerrupcion;


typedef struct {
    int nivel_1;
    int nivel_2;
    int nivel_3;
    int ptrb;   //seria el pid 
}DLinfo;

typedef struct {
    int tipo; //4 (Para enviar pagina a Memoria)
    int direccion_fisica;
    char *contenido;
}PaginaCache;


//-------------------------conexiones--------------------------------
t_list *recibir_procesos(int, t_log*);
char *obtener_instruccion(cpuinfo*, int, t_log *);

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

//---------------------------------------TLB----------------------------------------

// Estructura para una entrada de la TLB
typedef struct {
    int pagina;
    int marco;
    int proceso;
    int menos_usado;   // Registro del último uso (para LRU)
    int numero_ingreso;
} TLBEntrada;

// Estructura para simular la TLB
typedef struct {
    TLBEntrada entrada[ENTRADAS_TLB];
    int contador_acceso; // Contador global para registrar accesos
    int contador_ingresos;
} TLB;

// Inicializar la TLB
void inicializar_tlb(TLB *tlb) {
    for (int i = 0; i < ENTRADAS_TLB; i++) {
        tlb->entrada[i].pagina = -1;
        tlb->entrada[i].marco= -1;
        tlb->entrada[i].menos_usado = -1; // Sin historial de uso
        tlb->entrada[i].proceso=-1;
        tlb->entrada[i].numero_ingreso=-1;
    }
    tlb->contador_acceso = 0; // Inicializa el contador global
}

// Buscar en la TLB el marco 
int buscar_tlb(TLB *tlb, int pagina, int proceso, t_log*log_cpu) {
    for (int i = 0; i < ENTRADAS_TLB; i++) {
        if ( tlb->entrada[i].pagina == pagina && tlb->entrada[i].proceso==proceso) {
            log_info(log_cpu,"PID: %d - TLB HIT - Pagina: %d",proceso,pagina);
            
            return (tlb->entrada[i].marco); // Devuelve el número de marco
        }
    }
    log_info(log_cpu,"PID: %d - TLB MISS - Pagina: %d",proceso,pagina);
    return -1; // Si no se encuentra, devuelve -1
}

// Encuentra el índice LRU para reemplazo
int buscar_entrada_lru(TLB *tlb) {
    int indice_lru = 0;
    int min_menos_usado = tlb->entrada[0].menos_usado; // El valor de referencia de la primera entrada

    for (int i = 1; i < ENTRADAS_TLB; i++) {
        
        if (tlb->entrada[i].menos_usado < min_menos_usado) {
            min_menos_usado = tlb->entrada[i].menos_usado;
            indice_lru = i;
        }
    }
    return indice_lru;
}

int buscar_entrada_fifo(TLB *tlb){
    int indice_fifo = 0;
    int primero_ingresado = tlb->entrada[0].numero_ingreso;

    for(int i = 1; i < ENTRADAS_TLB; i++){
        if(tlb->entrada[i].numero_ingreso < primero_ingresado){
            primero_ingresado = tlb->entrada[i].numero_ingreso;
            indice_fifo = i;
        }
    }
    return indice_fifo;
}

void actualizar_tlb(TLB *tlb, int pagina, int marco,int proceso, char* reemplazo_tlb) {
    int indice = -1;
    if(reemplazo_tlb == "LRU"){    
        // Encuentra el índice a reemplazar según LRU
        indice = buscar_entrada_lru(tlb);
    }
    else if(reemplazo_tlb == "FIFO"){
        indice = buscar_entrada_fifo(tlb);
    }
    // Reemplazar la entrada
    tlb-> entrada[indice].pagina = pagina;
    tlb-> entrada[indice].marco = marco;
    tlb->contador_acceso++;
    tlb-> entrada[indice].menos_usado = tlb->contador_acceso;
    tlb-> entrada[indice].proceso=proceso;
    tlb->contador_ingresos++;
    tlb->entrada[indice].numero_ingreso=tlb->contador_ingresos;
    printf("TLB actualizado: Página %d -> Marco %d (Reemplazando entrada %d)\n", 
        pagina, marco, indice);
    return;
}


//-----------------------------------Cache-----------------------------------------

//cache de paginas
typedef struct {
    int numero_pagina;
    int modificado;
    char *contenido;
    int referencia_bit; //utilzado por el algoritmo Clock
} Pagina;

typedef struct {
    Pagina paginas[ENTRADAS_CACHE];
    int puntero; // apunta la entrada actual de cache
} Cache;

// Inicializar cache
void inicializar_cache(Cache *cache) {
    for (int i = 0; i < ENTRADAS_CACHE; i++) {
        cache->paginas[i].numero_pagina = -1; 
        cache->paginas[i].modificado = 0;
        cache->paginas[i].referencia_bit = 0;
        memset(cache->paginas[i].contenido, 0, TAMANIO_PAGINA);
    }
    cache->puntero = 0;
}
bool esta_en_cache(Cache *cache, int numero_pagina,cpuinfo*proceso,t_log*log_cpu, int desplazamiento, int longitud) {
    int cant_paginas = ((desplazamiento+longitud)/TAMANIO_PAGINA)+1;
    bool *presencia = malloc(cant_paginas*sizeof(bool));
    for(int i=0; i<cant_paginas; i++){ //Inicializo el array que va a indicar si todas las paginas necesitadas estan cargadas
        presencia[i] = false;
    }

    for(int j=numero_pagina; j<numero_pagina+cant_paginas; j++){ //Verifico que paginas estan cargadas
        for (int i = 0; i < ENTRADAS_CACHE; i++) {
            if (cache->paginas[i].numero_pagina == j) {
                presencia[j-numero_pagina] = true;
            }
        }
    }

    for(int i=0; i<cant_paginas; i++){ //Si una pagina no esta cargada -> Cache miss
        if(presencia[i] == false){
            log_info(log_cpu,"PID: %d - Cache Miss - Pagina: %d",proceso->pid,numero_pagina );
            free(presencia);
            return  false;
        }
    }

    for(int i=0; i<cant_paginas; i++){
        log_info(log_cpu,"PID: %d - Cache Hit - Pagina: %d",proceso->pid,numero_pagina+i );
    }
    free(presencia);
    return true;
}
// Busca una pagina en cache y retorna el indice 
int buscar_cache(Cache *cache, int numero_pagina) {
    for (int i = 0; i < ENTRADAS_CACHE; i++) {
        if (cache->paginas[i].numero_pagina == numero_pagina) {
            return i;
        }
    }
    return -1;
}

int minimo(int a, int b) {
  if (a < b) {
    return a;
  } else {
    return b;
  }
}

char* leer_cache (Cache *cache,int numero_pagina, int desplazamiento, int longitud)
{
    usleep(RETARDO_CACHE*1000);
    int cant_paginas = ((desplazamiento+longitud)/TAMANIO_PAGINA)+1;
    char *contenido = "";
    int indice, tope_leer;
    for(int i=0; i<cant_paginas; i++){
        indice = buscar_cache(cache,numero_pagina+i);
        if(i==0){
            tope_leer = minimo((desplazamiento+longitud), TAMANIO_PAGINA);
            for(int j=desplazamiento; j<tope_leer; j++){
                contenido[j-desplazamiento] = cache->paginas[indice].contenido[desplazamiento+j];
            }
            longitud = longitud-(tope_leer-desplazamiento);
        } else {
            tope_leer = minimo(longitud, TAMANIO_PAGINA);
            for(int j=0; j<tope_leer;j++){
                contenido[TAMANIO_PAGINA*i-desplazamiento+j] = cache->paginas[indice].contenido[j];
            }
            longitud = longitud-tope_leer;
        }
        cache->paginas[indice].referencia_bit = 1;      
    }
    contenido = strcat(contenido, '\0');
    return contenido;
}

void escribir_cache(Cache *cache, int numero_pagina, const char *contenido, int desplazamiento, int longitud) {
    usleep(RETARDO_CACHE*1000); 
    int cant_paginas = ((desplazamiento+longitud)/TAMANIO_PAGINA)+1;
    int indice, tope_escribir, tope_escribir_inic;   
    printf("Pagina %d encontrada en cache. Actualizando contenido.\n", numero_pagina);
    strncpy(cache->paginas[indice].contenido, contenido, TAMANIO_PAGINA);

    char *nuevo_contenido;
    for(int i=0; i<cant_paginas; i++){
        indice = buscar_cache(cache,numero_pagina+i); 
        if(i==0){
            tope_escribir_inic = minimo((desplazamiento+longitud), TAMANIO_PAGINA);
            for(int j=desplazamiento; j<tope_escribir_inic; j++){
                cache->paginas[indice].contenido[desplazamiento+j] = contenido[j-desplazamiento];
            }
            longitud = longitud-tope_escribir_inic;
        } else{
            tope_escribir = minimo(longitud, TAMANIO_PAGINA);
            for(int j=0; j<tope_escribir; j++){
                cache->paginas[indice].contenido[j] = contenido[tope_escribir_inic+TAMANIO_PAGINA*(i-1)+j];
            }
            longitud = longitud-tope_escribir;
        }

        cache->paginas[indice].referencia_bit = 1; 
        cache->paginas[indice].modificado = 1;
    }

    return;
}    

void actualizar_cache (Cache *cache, Pagina*pagina,cpuinfo*proceso,int conexion_memoria,TLB *tlb,t_log *log_cpu, char *tipo)
{
    PaginaCache *cambio_cache=malloc(sizeof(PaginaCache));
    cambio_cache->tipo = 4;
    t_paquete *paquete=crear_paquete();
    int direccion_logica;
    bool reemplazado = false;
    int indice;
    if(tipo == "CLOCK"){
        if((indice = buscar_cache(cache, pagina->numero_pagina))+1){
            cache->paginas[indice].referencia_bit = 1;
            reemplazado = true;
        }
        while (reemplazado == false) {
            if (cache->paginas[cache->puntero].referencia_bit == 0) {
            
                if(cache->paginas[cache->puntero].modificado)
                //si hubo modficaciones
                {
                cambio_cache->contenido=cache->paginas[cache->puntero].contenido;
                direccion_logica=cache->paginas[cache->puntero].numero_pagina*TAMANIO_PAGINA;
                cambio_cache->direccion_fisica=traducir_direccion(direccion_logica,conexion_memoria,proceso,tlb,log_cpu);
                agregar_a_paquete(paquete,cambio_cache,sizeof(PaginaCache));
                enviar_paquete(paquete,conexion_memoria,log_cpu);
                log_info(log_cpu,"PID: %d - Memory Update - Página: %d - Frame: %d",proceso->pid,pagina->numero_pagina,cambio_cache->direccion_fisica);
                }

                // Reemplazar la pagina
                log_info(log_cpu,"PID: %d - Cache Add - Pagina: %d",proceso->pid,pagina->numero_pagina);
                cache->paginas[cache->puntero].numero_pagina = pagina->numero_pagina;
                cache->paginas[cache->puntero].modificado = 0;
                strncpy(cache->paginas[cache->puntero].contenido, pagina->contenido, TAMANIO_PAGINA);
                cache->paginas[cache->puntero].referencia_bit = 1; // Setea el bit referencia en 1
                cache->puntero = (cache->puntero + 1) % ENTRADAS_CACHE; // Mueve el puntero clock
                reemplazado = true;
            } else {
                // Setea el bit de referencia en 0  y mueva el puntero clock
                cache->paginas[cache->puntero].referencia_bit = 0;
                cache->puntero = (cache->puntero + 1) % ENTRADAS_CACHE;
            }
        } 
    } else if(tipo == "CLOCK-M"){
        if((indice = buscar_cache(cache, pagina->numero_pagina))+1){
            cache->paginas[indice].referencia_bit = 1;
            reemplazado = true;
        }
        while(reemplazado == false){
            for(int i=0; i<ENTRADAS_CACHE; i++){
                if(cache->paginas[cache->puntero].referencia_bit==0 && cache->paginas[cache->puntero].modificado==0 && reemplazado==false){
                    // Reemplazar la pagina
                    log_info(log_cpu,"PID: %d - Cache Add - Pagina: %d",proceso->pid,pagina->numero_pagina);
                    cache->paginas[cache->puntero].numero_pagina = pagina->numero_pagina;
                    cache->paginas[cache->puntero].modificado = 0;
                    strncpy(cache->paginas[cache->puntero].contenido, pagina->contenido, TAMANIO_PAGINA);
                    cache->paginas[cache->puntero].referencia_bit = 1; // Setea el bit referencia en 1
                    cache->puntero = (cache->puntero + 1) % ENTRADAS_CACHE; // Mueve el puntero clock
                    reemplazado = true;
                } else if(reemplazado==false){
                    cache->puntero = (cache->puntero + 1) % ENTRADAS_CACHE;
                }
            }

            for(int i=0; i<ENTRADAS_CACHE; i++){
                if(cache->paginas[cache->puntero].referencia_bit==0 && cache->paginas[cache->puntero].modificado==1 && reemplazado==false){
                    cambio_cache->contenido=cache->paginas[cache->puntero].contenido;
                    direccion_logica=cache->paginas[cache->puntero].numero_pagina*TAMANIO_PAGINA;
                    cambio_cache->direccion_fisica=traducir_direccion(direccion_logica,conexion_memoria,proceso,tlb,log_cpu);
                    agregar_a_paquete(paquete,cambio_cache,sizeof(PaginaCache));
                    enviar_paquete(paquete,conexion_memoria,log_cpu);
                    log_info(log_cpu,"PID: %d - Memory Update - Página: %d - Frame: %d",proceso->pid,pagina->numero_pagina,cambio_cache->direccion_fisica);

                    log_info(log_cpu,"PID: %d - Cache Add - Pagina: %d",proceso->pid,pagina->numero_pagina);
                    cache->paginas[cache->puntero].numero_pagina = pagina->numero_pagina;
                    cache->paginas[cache->puntero].modificado = 0;
                    strncpy(cache->paginas[cache->puntero].contenido, pagina->contenido, TAMANIO_PAGINA);
                    cache->paginas[cache->puntero].referencia_bit = 1; // Setea el bit referencia en 1
                    cache->puntero = (cache->puntero + 1) % ENTRADAS_CACHE; // Mueve el puntero clock
                    reemplazado = true;
                } else if(reemplazado == false){
                    cache->paginas[cache->puntero].referencia_bit = 0;
                    cache->puntero = (cache->puntero + 1) % ENTRADAS_CACHE;
                }
            }
        }
    }
  

    
    free(cambio_cache);
    free(paquete);
}    

bool se_modifico_cache (Cache*cache)
{
    for(int i=0;i<ENTRADAS_CACHE;i++)
    {
        if (cache->paginas[i].modificado)
        {
            return true;
        }
    }
    return false;
}

//seria para cuando se dealoja al proceso
void enviar_cambios_memoria (Cache *cache,int conexion_memoria,cpuinfo *proceso,TLB*tlb,t_log *log_cpu)
{
    PaginaCache* cambios_cache=malloc(sizeof(PaginaCache));
    t_paquete* paquete = crear_paquete();
    int direccion_logica;
    for(int i=0;i<ENTRADAS_CACHE;i++)
    {
        if(cache->paginas[i].modificado)
        {
            direccion_logica= cache->paginas[i].numero_pagina * TAMANIO_PAGINA;
            cambios_cache->direccion_fisica = traducir_direccion(direccion_logica,conexion_memoria,proceso,tlb,log_cpu);
            cambios_cache->contenido = cache->paginas[i].contenido;
            cache->paginas[i].modificado = 0;
            agregar_a_paquete(paquete,cambios_cache,sizeof(PaginaCache));
        }
    }

    enviar_paquete(paquete,conexion_memoria,log_cpu);
    free(cambios_cache);
    free(paquete);

}    


int traducir_direccion (int direccion_logica, int conexion_memoria,cpuinfo *proceso, TLB*tlb,t_log* log_cpu)
{
    int numero_pagina = direccion_logica / TAMANIO_PAGINA;  
    int desplazamiento = direccion_logica % TAMANIO_PAGINA;
    int n1= (numero_pagina  / ENTRADAS_POR_TABLA ^ (2)) % ENTRADAS_POR_TABLA;
    int n2= (numero_pagina  / ENTRADAS_POR_TABLA ^ (1)) % ENTRADAS_POR_TABLA;
    int n3= (numero_pagina  / ENTRADAS_POR_TABLA ^ (0)) % ENTRADAS_POR_TABLA;
     
    int marco;
    if((marco = buscar_tlb(tlb,numero_pagina,proceso->pid,log_cpu))+1 ) //TLB hit (sumo 1 porque si no lo encuentra, marco=-1)
    {
        return (marco * TAMANIO_PAGINA + desplazamiento);
    }
    else // TLB miss
    {
        DLinfo * DL = malloc (sizeof(DLinfo));
        DL ->nivel_1=n1;
        DL ->nivel_2=n2;
        DL ->nivel_3=n3;
        DL ->ptrb =proceso->pid;

        t_paquete *paquete = crear_paquete();
        agregar_a_paquete(paquete, DL, sizeof(DLinfo));
        enviar_paquete(paquete, conexion_memoria, log_cpu);
        free (paquete);
        free(DL);
        int marco;
        recv (conexion_memoria,&marco, sizeof(int), MSG_WAITALL);
        
        return (marco * TAMANIO_PAGINA +desplazamiento);
    } 
    

}

sem_t mutex_interrupcion;
bool interrupcion_conexion = false;

void escuchar_conexion_interrupt(void *arg){
    infohilointerrupcion *datos = (infohilointerrupcion *) arg;
    while(1){
        char* mensaje = recibir_mensaje(datos->conexion, datos->log);
        log_info(datos->log, "Llega interrupcion al puerto Interrupt");
        if(mensaje == "DESALOJAR")
            sem_wait(&mutex_interrupcion);
            interrupcion_conexion = true;
            sem_post(&mutex_interrupcion);
        }
    return;
}

bool check_interrupt(bool interrupcion){
    if(interrupcion_conexion){
        interrupcion_conexion = false;
        return true;
    }
    return interrupcion;
}


int main(char* id_cpu) 
{
    char *nombre_log_cpu = strcat("cpu.log", id_cpu);

    t_log *log_cpu = log_create(nombre_log_cpu, "cpu", false, LOG_LEVEL_INFO);

    log_info(log_cpu,id_cpu);

    t_config*cpu_conf = iniciar_config(); 
    
    TLB*tlb;

    Cache *cache;

    // Inicia conexion_kernel_dispatch con Kernel dispatch
    int conexion_kernel_dispatch = conectar_kernel_dispatch(log_cpu);
    
    //enviar cpu_id al kernel
    enviar_mensaje(id_cpu,conexion_kernel_dispatch, log_cpu);
    
    // Inicia conexion_kernel_dispatch con Kernel interrupcion
    int conexion_kernel_interrupt= conectar_kernel_interrupt(log_cpu);
    

    // Inicia conexion_kernel_dispatch con Memoria
    int conexion_memoria=conectar_memoria(log_cpu);
    
    
    char* reemplazo_cache = config_get_string_value(cpu_conf, "REEMPLAZO_CACHE");
    char* reemplazo_tlb = config_get_string_value(cpu_conf, "REEMPLAZO_TLB");

    //enviar_mensaje(leido,conexion_memoria);
    enviar_mensaje("Hola Memoria!",conexion_memoria, nombre_log_cpu);
    
    //enviar cpu_id al kernel
    enviar_mensaje("Hola Kernel",conexion_kernel_dispatch, id_cpu);

    t_list *proceso;
    char *instruccion;
    bool interrupcion;
    infohilointerrupcion *conexion_interrumpir = malloc(sizeof(infohilointerrupcion));
    conexion_interrumpir->conexion = conexion_kernel_interrupt;
    conexion_interrumpir->log = log_cpu;
    sem_init(&mutex_interrupcion, 0, 1);
    pthread_t hiloInterrupcion;
    pthread_create(&hiloInterrupcion, NULL, escuchar_conexion_interrupt, (void *) conexion_interrumpir);
    pthread_detach(hiloInterrupcion);

    //while (cpu conectada){
        //vaciar cache y tlb
        inicializar_cache(cache);
        inicializar_tlb (tlb);
        proceso = recibir_procesos(conexion_kernel_dispatch, id_cpu);
        interrupcion = false;
        cpuinfo *procesocpu;
        procesocpu = malloc(sizeof(cpuinfo));
        procesocpu->tipo = 0; //Para que memoria sepa que le voy a pedir una instruccion
        procesocpu->pid = list_get(proceso, 0);
        procesocpu->pc = list_get(proceso, 1);
        do{
            instruccion = obtener_instruccion(procesocpu, conexion_memoria, log_cpu);
            decodear_y_ejecutar_instruccion(instruccion, procesocpu, conexion_memoria, conexion_kernel_dispatch, log_cpu, &interrupcion, cache, tlb, reemplazo_cache, reemplazo_tlb);
            sem_wait(&mutex_interrupcion);
            interrupcion = check_interrupt(interrupcion);
            sem_post(&mutex_interrupcion);
        }while(!interrupcion);
        t_paquete *paquete = crear_paquete();
        agregar_a_paquete(paquete, procesocpu, sizeof(cpuinfo));
        enviar_paquete(paquete, conectar_kernel_dispatch, log_cpu);

        //En caso de desalojo enviar cambios de la cache a memoria si hubo modificaciones
        if(se_modifico_cache(cache))
        {
           enviar_cambios_memoria(cache,conexion_memoria,procesocpu,tlb,log_cpu); 
        }
        free(procesocpu);
    //}

    // Limpieza general
    close(conexion_kernel_dispatch);
    close(conexion_memoria);
    log_destroy(log_cpu);
    config_destroy(cpu_conf);
    free(cache);
    free(tlb);

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
    log_info(log_cpu, "PID: ", procesocpu->pid, " - FETCH - Program Counter: ", procesocpu->pc);
    char * instruccion;
    t_paquete *paquete = crear_paquete();
    agregar_a_paquete(paquete, procesocpu, sizeof(cpuinfo));
    enviar_paquete(paquete, conexion_memoria, log_cpu);
    instruccion = recibir_mensaje(conexion_memoria, log_cpu);

    return instruccion;
}


void decodear_y_ejecutar_instruccion(char *instruccion, cpuinfo *proceso, int conexion_memoria, int conexion_kernel,t_log *log_cpu, bool *interrupcion,Cache *cache,TLB*tlb, char* reemplazo_cache, char* reemplazo_tlb)
{
    
    
    char **instruccion_separada = string_split(instruccion, " ");
    string_to_upper(instruccion_separada[0]);
    char *parametros = strcat(instruccion_separada[1], " ", instruccion_separada[2]);
    log_info(log_cpu, "PID: ", proceso->pid, " - Ejecutando: ", instruccion_separada[0], " - ", parametros);
    if(instruccion_separada[0] == "WRITE"){

        int direccion_logica = atoi(instruccion_separada[1]);
        int numero_pagina = direccion_logica/TAMANIO_PAGINA;
        int desplazamiento = direccion_logica%TAMANIO_PAGINA;
        int longitud = string_length(instruccion_separada[2]);

        //Primero vemos si esa pagina esta en cache

        if(esta_en_cache(cache,numero_pagina,proceso,log_cpu, desplazamiento, longitud))
        {
           
            escribir_cache(cache,numero_pagina,instruccion_separada[2], desplazamiento, longitud);
            proceso->pc = proceso->pc + 1;
        }
        else{

            //traducir y escribir
            int dir_fisica = traducir_direccion(direccion_logica,conexion_memoria,proceso,tlb,log_cpu);
            memoriainfo *write;
            write = malloc(sizeof(memoriainfo));
            write->tipo = 2;
            write->pid = proceso->pid;
            write->direccion = dir_fisica;
            char *dato = instruccion_separada[2];            
            int longitud = string_length(dato);
            t_paquete *paquete = crear_paquete();
            agregar_a_paquete(paquete, write, sizeof(memoriainfo));
            agregar_a_paquete(paquete, dato, (longitud+1)*sizeof(char));
            enviar_paquete(paquete, conexion_memoria, log_cpu);
            free(write);
            recibir_mensaje(conexion_memoria, log_cpu); //Recibo el OK de memoria
            log_info(log_cpu, "PID: %d - Acción: ESCRIBIR - Dirección Física: %d - Valor: %s",proceso->pid,dir_fisica,dato);
            proceso->pc = proceso->pc + 1;

            //actualizar cache
            int cant_paginas = ((desplazamiento+longitud)/TAMANIO_PAGINA)+1;
            Pagina *pagina_cache = malloc(sizeof(Pagina));
            pagina_cache->modificado=0;
            pagina_cache->referencia_bit=1;
            memoriainfo *pagina = malloc(sizeof(memoriainfo));
            pagina->tipo = 3;
            pagina->pid = proceso->pid;
            for(int i=numero_pagina; i<numero_pagina+cant_paginas; i++){
                pagina->direccion = i;
                t_paquete *paquete_pagina = crear_paquete();
                agregar_a_paquete(paquete_pagina, pagina, sizeof(memoriainfo));
                enviar(paquete_pagina, conexion_memoria, log_cpu);
                char *contenido = recibir_mensaje(conexion_memoria, log_cpu);
                pagina_cache->numero_pagina=i;
                pagina_cache->contenido = contenido;
                actualizar_cache(cache, pagina_cache,proceso,conexion_memoria,tlb,log_cpu, reemplazo_cache);
                free(paquete_pagina);
            }
            free (pagina_cache);
            free(paquete);
            free(pagina);

            //actualizar TLB
            int marco= (dir_fisica-desplazamiento)/TAMANIO_PAGINA;
            actualizar_tlb (tlb,numero_pagina,marco,proceso, reemplazo_tlb);

        }
        

    } else if(instruccion_separada[0] == "READ"){
        
        int direccion_logica = atoi(instruccion_separada[1]);
        int numero_pagina = direccion_logica/TAMANIO_PAGINA;
        int desplazamiento =direccion_logica%TAMANIO_PAGINA;
        int longitud = atoi(instruccion_separada[2]);
        
        if(esta_en_cache(cache,numero_pagina,proceso,log_cpu, desplazamiento, longitud))
        {

            char *leido = leer_cache (cache, numero_pagina, desplazamiento, longitud);
            log_info(log_cpu, leido);
            proceso->pc = proceso->pc + 1;

        }else{
            
            //traducir y leer
            int dir_fisica = traducir_direccion(direccion_logica,conexion_memoria,proceso,tlb,log_cpu);
            memoriainfo *read;
            read = malloc(sizeof(memoriainfo));
            read->tipo = 1;
            read->pid = proceso->pid;
            read->direccion = dir_fisica;
            int tamanio = atoi(instruccion_separada[2]);
            t_paquete *paquete = crear_paquete();
            agregar_a_paquete(paquete, read, sizeof(memoriainfo));
            agregar_a_paquete(paquete, tamanio, sizeof(int));
            enviar_paquete(paquete, conexion_memoria, log_cpu);
            free(read);
            char *leido = recibir_mensaje(conexion_memoria, log_cpu);
            log_info(log_cpu, "PID: %d - Acción: LEER - Dirección Física: %d - Valor: %s",proceso->pid,dir_fisica,leido);
            proceso->pc = proceso->pc + 1;
            
            //actualizar cache
            int cant_paginas = ((desplazamiento+longitud)/TAMANIO_PAGINA)+1;
            Pagina *pagina_cache = malloc(sizeof(Pagina));
            pagina_cache->modificado=0;
            pagina_cache->referencia_bit=1;
            memoriainfo *pagina = malloc(sizeof(memoriainfo));
            pagina->tipo = 3;
            pagina->pid = proceso->pid;
            for(int i=numero_pagina; i<numero_pagina+cant_paginas; i++){
                pagina->direccion = i;
                t_paquete *paquete_pagina = crear_paquete();
                agregar_a_paquete(paquete_pagina, pagina, sizeof(memoriainfo));
                enviar(paquete_pagina, conexion_memoria, log_cpu);
                char *contenido = recibir_mensaje(conexion_memoria, log_cpu);
                pagina_cache->numero_pagina=i;
                pagina_cache->contenido = contenido;
                actualizar_cache(cache, pagina_cache,proceso,conexion_memoria,tlb,log_cpu, reemplazo_cache);
                free(paquete_pagina);
            }
            free (pagina_cache);
            free(paquete);
            free(pagina);

            //actualizar TLB
            int marco= (dir_fisica-desplazamiento)/TAMANIO_PAGINA;
            actualizar_tlb (tlb,numero_pagina,marco,proceso, reemplazo_tlb);

        }
        

    } else if(instruccion_separada[0] == "GOTO"){
        proceso->pc = atoi(instruccion_separada[1]);

    } else if(instruccion_separada[0] == "IO"){
        syscallinfo *io;
        io = malloc(sizeof(syscallinfo));
        io->tipo = 3;
        io->pid = proceso->pid;
        io->pc = proceso->pc + 1;
        char *dispositivo = instruccion_separada[1];
        int longitud = string_length(dispositivo);
        int time = atoi(instruccion_separada[2]);
        t_paquete *paquete = crear_paquete();
        agregar_a_paquete(paquete, io, sizeof(syscallinfo));
        agregar_a_paquete(paquete, dispositivo, (longitud+1)*sizeof(char));
        agregar_a_paquete(paquete, time, sizeof(int));
        enviar_paquete(paquete, conexion_kernel, log_cpu);
        free(io);
        proceso->pc = proceso->pc + 1;
        *interrupcion = true;

    } else if(instruccion_separada[0] == "INIT_PROC"){
        syscallinfo *init;
        init = malloc(sizeof(syscallinfo));
        init->tipo = 1;
        init->pid = proceso->pid;
        init->pc = proceso->pc + 1;
        char *archivo = instruccion_separada[1];
        int longitud = string_length(archivo);
        int tamanio = atoi(instruccion_separada[2]);
        t_paquete *paquete = crear_paquete();
        agregar_a_paquete(paquete, init, sizeof(syscallinfo));
        agregar_a_paquete(paquete, archivo, (longitud+1)*sizeof(char));
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
        t_paquete *paquete = crear_paquete();
        agregar_a_paquete(paquete, dump, sizeof(syscallinfo));
        enviar_paquete(paquete, conexion_kernel, log_cpu);
        free(dump);
        proceso->pc = proceso->pc + 1;
        *interrupcion = true;

    } else if(instruccion_separada[0] == "EXIT"){
        syscallinfo *exit;
        exit = malloc(sizeof(syscallinfo));
        exit->tipo = 0;
        exit->pid = proceso->pid;
        exit->pc = proceso->pc;
        t_paquete *paquete = crear_paquete();
        agregar_a_paquete(paquete, exit, sizeof(syscallinfo));
        enviar_paquete(paquete, conexion_kernel, log_cpu);
        free(exit);
    }

    

    return;
}
