#include <utils/hello.h>
#include <commons/log.h>
#include <commons/config.h>
#include <commons/string.h>
#include <commons/collections/list.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <stdbool.h>

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
    int tipo;   //1-Read / 2-Write
    int pid;
    int direccion;
} memoriainfo;

typedef struct {
    int nivel_1;
    int nivel_2;
    int nivel_3;
    int ptrb;   //seria el pid 
}DLinfo;

typedef struct {
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
    bool validez;    // Indica si la entrada es válida
    int menos_usado;   // Registro del último uso (para LRU)
} TLBEntrada;

// Estructura para simular la TLB
typedef struct {
    TLBEntrada *entrada[ENTRADAS_TLB];
    int contador_acceso; // Contador global para registrar accesos
} TLB;

// Inicializar la TLB
void inicializar_tlb(TLB *tlb) {
    for (int i = 0; i < ENTRADAS_TLB; i++) {
        tlb->entrada[i].validez = false; // Todas las entradas comienzan como inválidas
        tlb->entrada[i].menos_usado = -1; // Sin historial de uso
    }
    tlb->contador_acceso = 0; // Inicializa el contador global
}

// Buscar en la TLB
int buscar_tlb(TLB *tlb, int pagina) {
    for (int i = 0; i < ENTRADAS_TLB; i++) {
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

    for (int i = 0; i < ENTRADAS_TLB; i++) {
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


//-----------------------------------Cache-----------------------------------------

//cache de paginas
typedef struct {
    int numero_pagina;
    int marco;
    bool modificado;
    char contenido[TAMANIO_PAGINA];
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
        cache->paginas[i].marco = -1;
        cache->paginas[i].modificado = false;
        cache->paginas[i].referencia_bit = 0;
        memset(cache->paginas[i].contenido, 0, TAMANIO_PAGINA);
    }
    cache->puntero = 0;
}

int encontrar_pagina_cache(Cache *cache, int numero_pagina) {
    for (int i = 0; i < ENTRADAS_CACHE; i++) {
        if (cache->paginas[i].numero_pagina == numero_pagina) {
            return i;
        }
    }
    return -1;
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
char* acceder_contenido_cache (Cache*cache,int numero_pagina)
{
    usleep(RETARDO_CACHE*1000);
    int indice = encontrar_pagina_cache(cache,numero_pagina);
    char*contenido = cache->paginas[indice].contenido;
    return contenido;
}

void escribir_cache(Cache *cache, int numero_pagina, const char *contenido) {
    usleep(RETARDO_CACHE*1000); 
    int indice = buscar_cache(cache,numero_pagina);   
    printf("Pagina %d encontrada en cache. Actualizando contenido.\n", numero_pagina);
    strncpy(cache->paginas[indice].contenido, contenido, TAMANIO_PAGINA);
    cache->paginas[indice].referencia_bit = 1; 
    cache->paginas[indice].modificado = true;
    return;
}    

void actualizar_cache (Cache *cache, int numero_pagina,int marco, const char *contenido,int conexion_memoria,t_log log_cpu)
{
  while (1) {
        if (cache->paginas[cache->puntero].referencia_bit == 0) {

            //Mandar los cambios de paginas en cache a memoria si hubo modficaciones
            
            if(cache->paginas[cache->puntero].modificado=true)
            {
                PaginaCache *cambio_cache=malloc(sizeof(PaginaCache));
                cambio_cache->contenido=cache->paginas[cache->puntero].contenido;
                cambio_cache->direccion_fisica=cache->paginas[cache->puntero].marco * TAMANIO_PAGINA;
                t_paquete paquete=crear_paquete();
                agregar_a_paquete(paquete,cambio_cache,sizeof(PaginaCache));
                enviar_paquete(paquete,conexion_memoria,log_cpu);
                free(cambio_cache);
            }

            // Reemplazar la pagina
            printf("Reemplazando pagina %d con pagina %d.\n", cache->paginas[cache->puntero].numero_pagina, numero_pagina);
            cache->paginas[cache->puntero].numero_pagina = numero_pagina;
            cache->paginas[cache->puntero].modificado = false;
            cache->paginas[cache->puntero].marco = marco
            strncpy(cache->paginas[cache->puntero].contenido, contenido, TAMANIO_PAGINA);
            cache->paginas[cache->puntero].referencia_bit = 1; // Setea el bit referencia en 1
            cache->puntero = (cache->puntero + 1) % ENTRADAS_CACHE; // Mueve el puntero clock
            break;
        } else {
            // Setea el bit de referencia en 0  y mueva el puntero clock
            cache->paginas[cache->puntero].referencia_bit = 0;
            cache->puntero = (cache->puntero + 1) % ENTRADAS_CACHE;
        }
        }   
}    

bool se_modifico_cache (Cache*cache)
{
    for(int i=0;i<ENTRADAS_CACHE;i++)
    {
        if (cache->paginas[i].modificado == true)
        {
            return true;
        }
    }
    return false;
}
void enviar_cambios_memoria (Cache *cache,int conexion_memoria,t_log log_cpu)
{
    PaginaCache* cambios_cache=malloc(sizeof(PaginaCache));
    t_paquete* paquete = crear_paquete();
    for(int i=0;i<ENTRADAS_CACHE;i++)
    {
        if(cache->paginas[i].modificado=true)
        {
            cambios_cache->direccion_fisica = cache->paginas[i].marco*TAMANIO_PAGINA;
            cambios_cache->contenido = cache->paginas[i].contenido;
            cache->paginas[i].modificado =  false;
            agregar_a_paquete(paquete,cambios_cache,sizeof(PaginaCache));
        }
    }

    enviar_paquete(paquete,conexion_memoria,log_cpu);
    free(cambios_cache);

}    


int traducir_direccion (int direccion_logica, int conexion_memoria,cpuinfo *proceso, TLB*tlb)
{
    int numero_pagina = direccion_logica / TAMANIO_PAGINA;  
    int desplazamiento = direccion_logica % TAMANIO_PAGINA;
    int n1= (numero_pagina  / ENTRADAS_POR_TABLA ^ (2)) % ENTRADAS_POR_TABLA;
    int n2= (numero_pagina  / ENTRADAS_POR_TABLA ^ (1)) % ENTRADAS_POR_TABLA;
    int n3= (numero_pagina  / ENTRADAS_POR_TABLA ^ (0)) % ENTRADAS_POR_TABLA;
     
    int marco= buscar_tlb(tlb,numero_pagina);
    if(marco != -1 ) //TLB hit
    {
        return marco * TAMANIO_PAGINA + desplazamiento;
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

        int marco_memoria;
        recv (conexion_memoria,&marco_memoria, sizeof(int), MSG_WAITALL);

        actualizar_tlb(tlb,numero_pagina,marco_memoria);
        
        return marco_memoria * TAMANIO_PAGINA +desplazamiento;
      } 
    

}

int obtener_marco (int direccion_logica, int direccion_fisica)
{
    int desplazamiento = direccion_logica%TAMANIO_PAGINA;
    int marco = (direccion_fisica - desplazamiento)/TAMANIO_PAGINA;
    return marco;

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
        interrupcion = false;
        cpuinfo *procesocpu;
        procesocpu = malloc(sizeof(cpuinfo));
        procesocpu->tipo = 0; //Para que memoria sepa que le voy a pedir una instruccion
        procesocpu->pid = list_get(proceso, 0);
        procesocpu->pc = list_get(proceso, 1);
        do{
            instruccion = obtener_instruccion(procesocpu, conexion_memoria, log_cpu);
            decodear_y_ejecutar_instruccion(instruccion, procesocpu, conexion_memoria, conexion_kernel_dispatch, log_cpu, &interrupcion);
            //interrupcion = check_interrupt()
        }while(!interrupcion);
        t_paquete *paquete = crear_paquete();
        agregar_a_paquete(paquete, procesocpu, sizeof(cpuinfo));
        enviar_paquete(paquete, conectar_kernel_dispatch, log_cpu);
        free(procesocpu);

        //En caso de desalojo enviar cambios de la cache a memoria si hubo modificaciones
        if(se_modifico_cache(cache))
        {
           enviar_cambios_memoria(cache,conexion_memoria,log_cpu); 
        }
        //y vaciar cache y tlb
        inicializar_cache(cache);
        inicializar_tlb (tlb);

        
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
    char * instruccion;
    t_paquete *paquete = crear_paquete();
    agregar_a_paquete(paquete, procesocpu, sizeof(cpuinfo));
    enviar_paquete(paquete, conexion_memoria, log_cpu);
    instruccion = recibir_mensaje(conexion_memoria, log_cpu);

    return instruccion;
}


void decodear_y_ejecutar_instruccion(char *instruccion, cpuinfo *proceso, int conexion_memoria, int conexion_kernel, t_log *log_cpu, bool *interrupcion,Cache *cache,TLB*tlb)
{
    
    
    char **instruccion_separada = string_split(instruccion, " ");
    string_to_upper(instruccion_separada[0]);
    if(instruccion_separada[0] == "WRITE"){

        int direccion_logica = atoi(instruccion_separada[1]);
        int numero_pagina = direccion_logica/TAMANIO_PAGINA;

        //Primero vemos si esa pagina esta en cache

        if(buscar_cache(cache, numero_pagina)!=-1)
        {
           
            escribir_cache(cache,numero_pagina,instruccion_separada[2]);
            proceso->pc = proceso->pc + 1;
        }
        else{

            //sino esta en cache hacemos la traduccion y la buscamos en memoria

            int dir_fisica = traducir_direccion(direccion_logica,TAMANIO_PAGINA,conexion_memoria,proceso,tlb);
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
            proceso->pc = proceso->pc + 1;
            //actualizar cache
            int marco = obtener_marco(direccion_logica,dir_fisica); 
            actualizar_cache(cache, numero_pagina,marco,instruccion_separada[2],conexion_memoria,log_cpu);

        }
        

    } else if(instruccion_separada[0] == "READ"){
        
        int direccion_logica = atoi(instruccion_separada[1]);
        int numero_pagina = direccion_logica/TAMANIO_PAGINA;
        
        if(buscar_cache(cache,numero_pagina)!=-1)
        {
            
            char *leido = acceder_contenido_cache (cache, numero_pagina);
            printf(leido);
            proceso->pc = proceso->pc + 1;

            //falta lo del logueo

        }else{

            int dir_fisica = traducir_direccion(direccion_logica,TAMANIO_PAGINA,conexion_memoria,proceso,tlb);
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
            printf(leido);
            log_info(log_cpu, leido);
            proceso->pc = proceso->pc + 1;
            //actulizar cache
            int marco = obtener_marco(direccion_logica,dir_fisica);
            actualizar_cache(cache, numero_pagina,marco,leido,conexion_memoria,log_cpu);

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
