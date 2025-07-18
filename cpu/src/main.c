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

//variables globales
int tam_pagina; //pide a memoria
int niveles_tabla; //pide a memoria
int entradas_por_tabla; // pide a memoria
int conexion_memoria;
int conexion_kernel_dispatch;
int conexion_kernel_interrupt;
int entradas_tlb;
char *reemplazo_tlb;
int entradas_cache;
char *reemplazo_cache;
int retardo_cache;

t_log *log_cpu;
t_config *config;

typedef struct
{
    int tipo;
	int pid;
	int pc;
} cpuinfo;

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
    int tipo; //4 (Para enviar pagina a Memoria)
    int pid;
    int direccion_fisica;
    char *contenido;
}PaginaCache;


//-------------------------conexiones--------------------------------
t_list *recibir_procesos();
char *obtener_instruccion(cpuinfo*);

void iniciar_config(char* path_config)
{
	config = config_create(path_config);
    conectar_kernel_dispatch();
    conectar_kernel_interrupt();
    conectar_memoria();
    entradas_cache = config_get_int_value(config, "ENTRADAS_CACHE");
    retardo_cache = config_get_int_value(config, "RETARDO_CACHE");
    entradas_tlb = config_get_int_value (config,"ENTRADAS_TLB");
    reemplazo_cache = config_get_string_value(config, "REEMPLAZO_CACHE");
    reemplazo_tlb = config_get_string_value(config, "REEMPLAZO_TLB");
}
void conectar_kernel_interrupt()
{
    char* ip_kernel_interrupt = config_get_string_value(config, "IP_KERNEL");
    char *puerto_kernel_interrupt = config_get_string_value(config, "PUERTO_KERNEL_INTERRUPT");
    conexion_kernel_interrupt = iniciar_conexion(ip_kernel_interrupt, puerto_kernel_interrupt);
    return;
}

void conectar_kernel_dispatch()
{
    char* ip_kernel_dispatch = config_get_string_value(config, "IP_KERNEL");
    char* puerto_kernel_dispatch = config_get_string_value(config, "PUERTO_KERNEL_DISPATCH");
    conexion_kernel_dispatch = iniciar_conexion(ip_kernel_dispatch, puerto_kernel_dispatch);
    return;
}

void conectar_memoria()
{
    char* ip_memoria = config_get_string_value(config, "IP_MEMORIA");
    char* puerto_memoria = config_get_string_value(config, "PUERTO_MEMORIA");
    int conexion_memoria = iniciar_conexion(ip_memoria, puerto_memoria);
    return;
}

//---------------------------------------TLB----------------------------------------

// Estructura para una entrada de la TLB
typedef struct {
    int pagina;
    int marco;
    int menos_usado;   // Registro del último uso (para LRU)
    int numero_ingreso;
} TLBEntrada;

int contador_acceso = 0; // Contador global para registrar accesos
int contador_ingresos = 0;

// Inicializar la TLB
void inicializar_tlb(TLBEntrada *tlb) {
    for (int i = 0; i < entradas_tlb; i++) {
        tlb[i].pagina = -1;
        tlb[i].marco= -1;
        tlb[i].menos_usado = -1; // Sin historial de uso
        tlb[i].numero_ingreso=-1;
    }
    contador_acceso = 0; // Inicializa el contador global
    contador_ingresos = 0;
}

// Buscar en la TLB el marco 
int buscar_tlb(TLBEntrada *tlb, int pagina, int proceso) {
    for (int i = 0; i < entradas_tlb; i++) {
        if ( tlb[i].pagina == pagina) {
            log_info(log_cpu,"PID: %d - TLB HIT - Pagina: %d",proceso,pagina);
            
            return (tlb[i].marco); // Devuelve el número de marco
        }
    }
    log_info(log_cpu,"PID: %d - TLB MISS - Pagina: %d",proceso,pagina);
    return -1; // Si no se encuentra, devuelve -1
}

// Encuentra el índice LRU para reemplazo
int buscar_entrada_lru(TLBEntrada *tlb) {
    int indice_lru = 0;
    int min_menos_usado = tlb[0].menos_usado; // El valor de referencia de la primera entrada

    for (int i = 1; i < entradas_tlb; i++) {
        
        if (tlb[i].menos_usado < min_menos_usado) {
            min_menos_usado = tlb[i].menos_usado;
            indice_lru = i;
        }
    }
    return indice_lru;
}

int buscar_entrada_fifo(TLBEntrada *tlb){
    int indice_fifo = 0;
    int primero_ingresado = tlb[0].numero_ingreso;

    for(int i = 1; i < entradas_tlb; i++){
        if(tlb[i].numero_ingreso < primero_ingresado){
            primero_ingresado = tlb[i].numero_ingreso;
            indice_fifo = i;
        }
    }
    return indice_fifo;
}

void actualizar_tlb(TLBEntrada *tlb, int pagina, int marco) {
    int indice = -1;
    if(reemplazo_tlb == "LRU"){    
        // Encuentra el índice a reemplazar según LRU
        indice = buscar_entrada_lru(tlb);
    }
    else if(reemplazo_tlb == "FIFO"){
        indice = buscar_entrada_fifo(tlb);
    }
    // Reemplazar la entrada
    tlb[indice].pagina = pagina;
    tlb[indice].marco = marco;
    contador_acceso++;
    tlb[indice].menos_usado = contador_acceso;
    contador_ingresos++;
    tlb[indice].numero_ingreso=contador_ingresos;
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

int puntero = 0; // apunta la entrada actual de cache
int frames_cargados = 0;

// Inicializar cache
void inicializar_cache(Pagina *cache) {
    for (int i = 0; i < entradas_cache; i++) {
        cache[i].numero_pagina = -1; 
        cache[i].modificado = 0;
        cache[i].referencia_bit = 0;
        for(int j=0; j<tam_pagina; j++){
            cache[i].contenido[j]="0";
        }
    }
    puntero = 0;
    frames_cargados = 0;
}
bool esta_en_cache(Pagina *cache, int numero_pagina,cpuinfo*proceso) {
    usleep(retardo_cache*1000);
    for (int i = 0; i < entradas_cache; i++) {
        if (cache[i].numero_pagina == numero_pagina) {
            log_info(log_cpu,"PID: %d - Cache Hit - Pagina: %d",proceso->pid,numero_pagina );
            return true;
        }
    }
    log_info(log_cpu,"PID: %d - Cache Miss - Pagina: %d",proceso->pid,numero_pagina );
    return  false;
}
// Busca una pagina en cache y retorna el indice 
int buscar_cache(Pagina *cache, int numero_pagina) {
    for (int i = 0; i < entradas_cache; i++) {
        if (cache[i].numero_pagina == numero_pagina) {
            return i;
        }
    }
    return -1;
}

char* leer_cache (Pagina *cache,int numero_pagina, int desplazamiento, int longitud)
{
    char *contenido = malloc(sizeof(char)*(longitud+1));
    int indice;
    indice = buscar_cache(cache,numero_pagina);
    for(int j=desplazamiento; j<desplazamiento+longitud; j++){
        contenido[j-desplazamiento] = cache[indice].contenido[j];
    }
    cache[indice].referencia_bit = 1;      
    
    contenido[longitud] = '\0';
    return contenido;
}

void escribir_cache(Pagina *cache, int numero_pagina, const char *contenido, int desplazamiento, int longitud) {
    int indice;   
    indice = buscar_cache(cache,numero_pagina); 
    for(int j=desplazamiento; j<desplazamiento+longitud; j++){
        cache[indice].contenido[j] = contenido[j-desplazamiento];
    }
    cache[indice].referencia_bit = 1; 
    cache[indice].modificado = 1;
    
    return;
}    

void actualizar_cache (Pagina *cache, Pagina*pagina,cpuinfo*proceso, TLBEntrada *tlb)
{
    PaginaCache *cambio_cache=malloc(sizeof(PaginaCache));
    cambio_cache->tipo = 4;
    cambio_cache->pid=proceso->pid;
    t_paquete *paquete=crear_paquete();
    int direccion_logica;
    bool reemplazado = false;
    int indice;
    if(reemplazo_cache == "CLOCK"){
        if((indice = buscar_cache(cache, pagina->numero_pagina))+1){
            cache[indice].referencia_bit = 1;
            reemplazado = true;
        }
        while (reemplazado == false) {
            if (cache[puntero].referencia_bit == 0) {
            
                if(cache[puntero].modificado)
                //si hubo modficaciones
                {
                cambio_cache->contenido=cache[puntero].contenido;
                direccion_logica=cache[puntero].numero_pagina*tam_pagina;
                cambio_cache->direccion_fisica=traducir_direccion(direccion_logica, proceso, tlb);
                agregar_a_paquete(paquete,cambio_cache,sizeof(PaginaCache));
                enviar_paquete(paquete,conexion_memoria);
                recibir_mensaje(conexion_memoria); //Espero la confirmacion de memoria
                log_info(log_cpu,"PID: %d - Memory Update - Página: %d - Frame: %d",proceso->pid,pagina->numero_pagina,cambio_cache->direccion_fisica);
                }

                // Reemplazar la pagina
                log_info(log_cpu,"PID: %d - Cache Add - Pagina: %d",proceso->pid,pagina->numero_pagina);
                cache[puntero].numero_pagina = pagina->numero_pagina;
                cache[puntero].modificado = 0;
                strncpy(cache[puntero].contenido, pagina->contenido, tam_pagina);
                cache[puntero].referencia_bit = 1; // Setea el bit referencia en 1
                puntero = (puntero + 1) % entradas_cache; // Mueve el puntero clock
                reemplazado = true;
                if(frames_cargados < entradas_tlb)
                    frames_cargados++;
            } else {
                // Setea el bit de referencia en 0  y mueva el puntero clock
                cache[puntero].referencia_bit = 0;
                puntero = (puntero + 1) % entradas_cache;
            }
        } 
    } else if(reemplazo_cache == "CLOCK-M"){
        if((indice = buscar_cache(cache, pagina->numero_pagina))+1){
            cache[indice].referencia_bit = 1;
            reemplazado = true;
        }
        while(reemplazado == false){
            for(int i=0; i<entradas_cache; i++){
                if(cache[puntero].referencia_bit==0 && cache[puntero].modificado==0 && reemplazado==false){
                    // Reemplazar la pagina
                    log_info(log_cpu,"PID: %d - Cache Add - Pagina: %d",proceso->pid,pagina->numero_pagina);
                    cache[puntero].numero_pagina = pagina->numero_pagina;
                    cache[puntero].modificado = 0;
                    strncpy(cache[puntero].contenido, pagina->contenido, tam_pagina);
                    cache[puntero].referencia_bit = 1; // Setea el bit referencia en 1
                    cache = (puntero + 1) % entradas_cache; // Mueve el puntero clock
                    reemplazado = true;
                    if(frames_cargados < entradas_tlb)
                        frames_cargados++;
                } else if(reemplazado==false){
                    puntero = (puntero + 1) % entradas_cache;
                }
            }

            for(int i=0; i<entradas_cache; i++){
                if(cache[puntero].referencia_bit==0 && cache[puntero].modificado==1 && reemplazado==false){
                    cambio_cache->contenido=cache[puntero].contenido;
                    direccion_logica=cache[puntero].numero_pagina*tam_pagina;
                    cambio_cache->direccion_fisica=traducir_direccion(direccion_logica, proceso, tlb);
                    agregar_a_paquete(paquete,cambio_cache,sizeof(PaginaCache));
                    enviar_paquete(paquete,conexion_memoria);
                    recibir_mensaje(conexion_memoria); //Espero la confirmacion de memoria
                    log_info(log_cpu,"PID: %d - Memory Update - Página: %d - Frame: %d",proceso->pid,pagina->numero_pagina,cambio_cache->direccion_fisica);

                    log_info(log_cpu,"PID: %d - Cache Add - Pagina: %d",proceso->pid,pagina->numero_pagina);
                    cache[puntero].numero_pagina = pagina->numero_pagina;
                    cache[puntero].modificado = 0;
                    strncpy(cache[puntero].contenido, pagina->contenido, tam_pagina);
                    cache[puntero].referencia_bit = 1; // Setea el bit referencia en 1
                    puntero = (puntero + 1) % entradas_cache; // Mueve el puntero clock
                    reemplazado = true;
                    if(frames_cargados < entradas_tlb)
                        frames_cargados++;
                } else if(reemplazado == false){
                    cache[puntero].referencia_bit = 0;
                    puntero = (puntero + 1) % entradas_cache;
                }
            }
        }
    }
  

    
    free(cambio_cache);
    eliminar_paquete(paquete);
}    

bool se_modifico_cache (Pagina *cache)
{
    for(int i=0;i<entradas_cache;i++)
    {
        if (cache[i].modificado)
        {
            return true;
        }
    }
    return false;
}

//seria para cuando se dealoja al proceso
void enviar_cambios_memoria (Pagina *cache,cpuinfo *proceso,TLBEntrada *tlb)
{
    PaginaCache* cambios_cache=malloc(sizeof(PaginaCache));
    cambios_cache->tipo = 4;
    cambios_cache->pid = proceso->pid;
    int direccion_logica;
    for(int i=0;i<entradas_cache;i++)
    {
        if(cache[i].modificado)
        {
            t_paquete* paquete = crear_paquete();
            direccion_logica= cache[i].numero_pagina * tam_pagina;
            cambios_cache->direccion_fisica = traducir_direccion(direccion_logica, proceso, tlb);
            cambios_cache->contenido = cache[i].contenido;
            cache[i].modificado = 0;
            agregar_a_paquete(paquete,cambios_cache,sizeof(PaginaCache));
            enviar_paquete(paquete,conexion_memoria);
            recibir_mensaje(conexion_memoria); //Recibo el OK de memoria
            eliminar_paquete(paquete);
        }
    }


    free(cambios_cache);

}    


int traducir_direccion (int direccion_logica,cpuinfo *proceso, TLBEntrada* tlb)
{
    int numero_pagina = direccion_logica / tam_pagina;  
    int desplazamiento = direccion_logica % tam_pagina;
     
    int marco;
    if((marco = buscar_tlb(tlb,numero_pagina, proceso->pid))+1 ) //TLB hit (sumo 1 porque si no lo encuentra, marco=-1)
    {
        return (marco * tam_pagina + desplazamiento);
    }
    else // TLB miss
    {
        t_paquete *paquete = crear_paquete();
        agregar_a_paquete(paquete, 5, sizeof(int)); //5 -> Envio marco
        agregar_a_paquete(paquete, proceso->pid, sizeof(int));
        int n;
        for(int i=1; i<=niveles_tabla; i++){
            n = (numero_pagina / pow(entradas_por_tabla, niveles_tabla - i)) % entradas_por_tabla;
            agregar_a_paquete(paquete, n, sizeof(int));
        }
        enviar_paquete(paquete, conexion_memoria);
        eliminar_paquete(paquete);
        int marco;
        t_list *lista = recibir_paquete(conexion_memoria);
        marco = *(int *)list_get(lista, 0);
        list_destroy_and_destroy_elements(lista, free);
        
        return (marco * tam_pagina + desplazamiento);
    } 
    

}

sem_t mutex_interrupcion;
bool interrupcion_conexion = false;

void escuchar_conexion_interrupt(){
    while(1){
        char* mensaje = recibir_mensaje(conexion_kernel_interrupt);
        log_info(log_cpu, "Llega interrupcion al puerto Interrupt");
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


int main(int argc, char* argv[]) 
{
    if(argc < 3){
        return 1;
    }

    char *nombre_log_cpu = malloc(sizeof(char)*9);
    strcpy(nombre_log_cpu, "cpu");
    strcat(nombre_log_cpu, argv[2]);
    strcat(nombre_log_cpu, ".log");

    log_cpu = log_create(nombre_log_cpu, "cpu", false, LOG_LEVEL_INFO);

    char *path_config = argv[1];

    iniciar_config(path_config); 
    
    //enviar cpu_id al kernel
    enviar_mensaje(argv[2],conexion_kernel_dispatch);

    recibir_mensaje(conexion_kernel_dispatch);
    
    
    //enviar cpu_id a memoria;
    //enviar_mensaje(argv[2], conexion_memoria);

    t_list *paquete_memoria = recibir_paquete(conexion_memoria);
    tam_pagina = *(int *)list_get(paquete_memoria, 0);
    niveles_tabla = *(int *)list_get(paquete_memoria, 1);
    entradas_por_tabla = *(int *)list_get(paquete_memoria, 2);
    list_destroy_and_destroy_elements(paquete_memoria, free);

    TLBEntrada *tlb;

    if(entradas_tlb > 0){
        tlb = malloc(sizeof(TLBEntrada)*entradas_tlb);
    }

    Pagina *cache;

    if(entradas_cache > 0){
        cache = malloc(sizeof(Pagina)*entradas_cache);
        for(int i = 0; i<entradas_cache; i++){
            cache[i].contenido = malloc(sizeof(char)*tam_pagina);
        }
    }
    
    t_list *proceso;
    char *instruccion;
    bool interrupcion;
    sem_init(&mutex_interrupcion, 0, 1);
    pthread_t hiloInterrupcion;
    pthread_create(&hiloInterrupcion, NULL, escuchar_conexion_interrupt, NULL);
    pthread_detach(hiloInterrupcion);

    while (1){
        //vaciar cache y tlb
        if(entradas_cache > 0)
            inicializar_cache(cache);
        if(entradas_tlb > 0)
            inicializar_tlb(tlb);
        proceso = recibir_procesos();
        interrupcion = false;
        cpuinfo *procesocpu;
        procesocpu = malloc(sizeof(cpuinfo));
        procesocpu->tipo = 0; //Para que memoria sepa que le voy a pedir una instruccion
        procesocpu->pid = *(int *)list_get(proceso, 0);
        procesocpu->pc = *(int *)list_get(proceso, 1);
        list_clean_and_destroy_elements(proceso, free);
        do{
            instruccion = obtener_instruccion(procesocpu);
            if(instruccion != "NO"){
                decodear_y_ejecutar_instruccion(instruccion, procesocpu, &interrupcion, cache, tlb);
                sem_wait(&mutex_interrupcion);
                interrupcion = check_interrupt(interrupcion);
                sem_post(&mutex_interrupcion);
            } else{
                interrupcion = true;
            }

        }while(!interrupcion);
        t_paquete *paquete = crear_paquete();
        int tipo = 4;
        agregar_a_paquete(paquete, tipo, sizeof(int));
        agregar_a_paquete(paquete, procesocpu, sizeof(cpuinfo));
        enviar_paquete(paquete, conexion_kernel_dispatch);
        eliminar_paquete(paquete);

        //En caso de desalojo enviar cambios de la cache a memoria si hubo modificaciones
        if(se_modifico_cache(cache))
        {
           enviar_cambios_memoria(cache, procesocpu, tlb); 
        }
        free(procesocpu);
    }

    list_destroy(proceso);

    // Limpieza general
    close(conexion_kernel_dispatch);
    close(conexion_kernel_interrupt);
    close(conexion_memoria);
    log_destroy(log_cpu);
    config_destroy(config);
    if(entradas_cache > 0)
        for(int i = 0; i<entradas_cache; i++){
            free(cache[i].contenido);
        }
        free(cache);
    if(entradas_tlb > 0)
        free(tlb);

    return 0;
}

t_list *recibir_procesos()
{
    t_list *proceso;
    proceso = recibir_paquete(conexion_kernel_dispatch);

    return proceso;
}

char *obtener_instruccion(cpuinfo *procesocpu)
{
    log_info(log_cpu, "PID: %d - FETCH - Program Counter: %d", procesocpu->pid, procesocpu->pc);
    char * instruccion;
    t_paquete *paquete = crear_paquete();
    agregar_a_paquete(paquete, procesocpu, sizeof(cpuinfo));
    enviar_paquete(paquete, conexion_memoria);
    eliminar_paquete(paquete);
    instruccion = recibir_mensaje(conexion_memoria);

    return instruccion;
}


void decodear_y_ejecutar_instruccion(char *instruccion, cpuinfo *proceso, bool *interrupcion,Pagina *cache,TLBEntrada*tlb)
{
    
    char **instruccion_separada = string_n_split(instruccion, 3, " ");
    string_to_upper(instruccion_separada[0]);
    char *parametros = malloc(sizeof(char)*64);
    strcpy(parametros, instruccion_separada[1]);
    strcat(parametros, " ");
    strcat(parametros, instruccion_separada[2]);
    log_info(log_cpu, "PID: %d - Ejecutando: %s - %s ", proceso->pid, instruccion_separada[0], parametros);
    free(parametros);
    if(instruccion_separada[0] == "WRITE"){

        int direccion_logica = atoi(instruccion_separada[1]);
        int numero_pagina = direccion_logica/tam_pagina;
        int desplazamiento = direccion_logica%tam_pagina;
        int longitud = string_length(instruccion_separada[2]);

        //Primero vemos si esa pagina esta en cache

        if(frames_cargados > 0 && esta_en_cache(cache,numero_pagina,proceso))
        {
           
            escribir_cache(cache,numero_pagina,instruccion_separada[2], desplazamiento, longitud);
            proceso->pc = proceso->pc + 1;
        } else{

            //traducir y escribir
            int dir_fisica = traducir_direccion(direccion_logica, proceso, tlb);
            memoriainfo *write;
            write = malloc(sizeof(memoriainfo));
            write->tipo = 2;
            write->pid = proceso->pid;
            write->direccion = dir_fisica;
            char *dato = instruccion_separada[2];            
            int longitud = string_length(dato);
            t_paquete *paquete = crear_paquete();
            agregar_a_paquete(paquete, write, sizeof(memoriainfo));
            agregar_a_paquete(paquete, longitud+1, sizeof(int));
            agregar_a_paquete(paquete, dato, (longitud+1)*sizeof(char));
            enviar_paquete(paquete, conexion_memoria);
            free(write);
            eliminar_paquete(paquete);
            recibir_mensaje(conexion_memoria); //Recibo el OK de memoria
            log_info(log_cpu, "PID: %d - Acción: ESCRIBIR - Dirección Física: %d - Valor: %s",proceso->pid,dir_fisica,dato);
            proceso->pc = proceso->pc + 1;

            //actualizar cache
            if(entradas_cache > 0){
                Pagina *pagina_cache = malloc(sizeof(Pagina));
                pagina_cache->modificado=0;
                pagina_cache->referencia_bit=1;
                memoriainfo *pagina = malloc(sizeof(memoriainfo));
                pagina->tipo = 3;
                pagina->pid = proceso->pid;
                pagina->direccion = dir_fisica-desplazamiento; //envia el marco que busca
                t_paquete *paquete_pagina = crear_paquete();
                agregar_a_paquete(paquete_pagina, pagina, sizeof(memoriainfo));
                enviar_paquete(paquete_pagina, conexion_memoria);
                char *contenido = recibir_mensaje(conexion_memoria);
                pagina_cache->numero_pagina=numero_pagina;
                pagina_cache->contenido = contenido;
                actualizar_cache(cache, pagina_cache,proceso, tlb);
                eliminar_paquete(paquete_pagina);
                free(pagina_cache);
                free(pagina);
            }


            //actualizar TLB
            if(entradas_tlb > 0){
                int marco= (dir_fisica-desplazamiento)/tam_pagina;
                actualizar_tlb (tlb,numero_pagina,marco);            
            }

        }
        

    } else if(instruccion_separada[0] == "READ"){
        
        int direccion_logica = atoi(instruccion_separada[1]);
        int numero_pagina = direccion_logica/tam_pagina;
        int desplazamiento =direccion_logica%tam_pagina;
        int longitud = atoi(instruccion_separada[2]);
        
        if(frames_cargados > 0 && esta_en_cache(cache,numero_pagina,proceso))
        {

            char *leido = leer_cache (cache, numero_pagina, desplazamiento, longitud);
            log_info(log_cpu, leido);
            free(leido);
            proceso->pc = proceso->pc + 1;

        }else{
            
            //traducir y leer
            int dir_fisica = traducir_direccion(direccion_logica ,proceso, tlb);
            memoriainfo *read;
            read = malloc(sizeof(memoriainfo));
            read->tipo = 1;
            read->pid = proceso->pid;
            read->direccion = dir_fisica;
            int tamanio = atoi(instruccion_separada[2]);
            t_paquete *paquete = crear_paquete();
            agregar_a_paquete(paquete, read, sizeof(memoriainfo));
            agregar_a_paquete(paquete, tamanio, sizeof(int));
            enviar_paquete(paquete, conexion_memoria);
            free(read);
            eliminar_paquete(paquete);
            char *leido = recibir_mensaje(conexion_memoria);
            log_info(log_cpu, "PID: %d - Acción: LEER - Dirección Física: %d - Valor: %s",proceso->pid,dir_fisica,leido);
            proceso->pc = proceso->pc + 1;
            
            //actualizar cache
            if(entradas_cache > 0){
                Pagina *pagina_cache = malloc(sizeof(Pagina));
                pagina_cache->modificado=0;
                pagina_cache->referencia_bit=1;
                memoriainfo *pagina = malloc(sizeof(memoriainfo));
                pagina->tipo = 3;
                pagina->pid = proceso->pid;
                pagina->direccion = dir_fisica-desplazamiento;
                t_paquete *paquete_pagina = crear_paquete();
                agregar_a_paquete(paquete_pagina, pagina, sizeof(memoriainfo));
                enviar_paquete(paquete_pagina, conexion_memoria);
                char *contenido = recibir_mensaje(conexion_memoria);
                pagina_cache->numero_pagina=numero_pagina;
                pagina_cache->contenido = contenido;
                actualizar_cache(cache, pagina_cache,proceso, tlb);
                eliminar_paquete(paquete_pagina);
                free (pagina_cache);
                free(pagina);
            }


            //actualizar TLB
            if(entradas_tlb > 0){
                int marco= (dir_fisica-desplazamiento)/tam_pagina;
                actualizar_tlb (tlb,numero_pagina,marco);
            }

        }
        

    } else if(instruccion_separada[0] == "GOTO"){
        proceso->pc = atoi(instruccion_separada[1]);

    } else if(instruccion_separada[0] == "IO"){
        cpuinfo *io;
        io = malloc(sizeof(cpuinfo));
        io->tipo = 3;
        io->pid = proceso->pid;
        io->pc = proceso->pc + 1;
        char *dispositivo = instruccion_separada[1];
        int longitud = string_length(dispositivo);
        int time = atoi(instruccion_separada[2]);
        t_paquete *paquete = crear_paquete();
        agregar_a_paquete(paquete, io, sizeof(cpuinfo));
        agregar_a_paquete(paquete, dispositivo, (longitud+1)*sizeof(char));
        agregar_a_paquete(paquete, time, sizeof(int));
        enviar_paquete(paquete, conexion_kernel_dispatch);
        eliminar_paquete(paquete);
        free(io);
        proceso->pc = proceso->pc + 1;
        *interrupcion = true;

    } else if(instruccion_separada[0] == "INIT_PROC"){
        cpuinfo *init;
        init = malloc(sizeof(cpuinfo));
        init->tipo = 1;
        init->pid = proceso->pid;
        init->pc = proceso->pc + 1;
        char *archivo = instruccion_separada[1];
        int longitud = string_length(archivo);
        int tamanio = atoi(instruccion_separada[2]);
        t_paquete *paquete = crear_paquete();
        agregar_a_paquete(paquete, init, sizeof(cpuinfo));
        agregar_a_paquete(paquete, archivo, (longitud+1)*sizeof(char));
        agregar_a_paquete(paquete, tamanio, sizeof(int));
        enviar_paquete(paquete, conexion_kernel_dispatch);
        eliminar_paquete(paquete);
        recibir_mensaje(conexion_kernel_dispatch);
        free(init);
        proceso->pc = proceso->pc + 1;

    } else if(instruccion_separada[0] == "DUMP_MEMORY"){
        cpuinfo *dump;
        dump = malloc(sizeof(cpuinfo));
        dump->tipo = 2;
        dump->pid = proceso->pid;
        dump->pc = proceso->pc + 1;
        t_paquete *paquete = crear_paquete();
        agregar_a_paquete(paquete, dump, sizeof(cpuinfo));
        enviar_paquete(paquete, conexion_kernel_dispatch);
        eliminar_paquete(paquete);
        free(dump);
        proceso->pc = proceso->pc + 1;
        *interrupcion = true;

    } else if(instruccion_separada[0] == "EXIT"){
        cpuinfo *exit;
        exit = malloc(sizeof(cpuinfo));
        exit->tipo = 0;
        exit->pid = proceso->pid;
        exit->pc = proceso->pc;
        t_paquete *paquete = crear_paquete();
        agregar_a_paquete(paquete, exit, sizeof(cpuinfo));
        enviar_paquete(paquete, conexion_kernel_dispatch);
        eliminar_paquete(paquete);
        free(exit);
    }

    

    return;
}
