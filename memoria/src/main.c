
#include <commons/log.h>
#include <commons/config.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <commons/collections/list.h>
#include <commons/collections/dictionary.h>
#include <commons/string.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <commons/txt.h>
#include <string.h>

#include <utils/hello.h>

#include <commons/collections/queue.h>

#include <commons/temporal.h>

#define TAREA 0
#define LEER_PAG 1
#define ACTUALIZAR_PAG 2
#define LEER_PAG_COMPLETA 3
#define ACTUALIZAR_PAG_COM 4
#define ACCEDER_TABLA 5

#define PROCESO_NUEVO 6
#define SUSPENDER 7
#define DES_SUSPENDER 8
#define MEMORY_DUMP 9
#define FINALIZAR 10

#define CORTAR 11

// Globales Obligatorias
t_config *nuevo_conf;
t_log *log_memo;
int *socket_conectado;
char *PUERTO_ESCUCHA;
int TAM_MEMORIA;
int TAM_PAGINA;
int ENTRADAS_POR_TABLA;
int CANTIDAD_NIVELES;
int RETARDO_MEMORIA;
char *PATH_SWAPFILE;
int RETARDO_SWAP;
t_log_level LOG_LEVEL;
char *DUMP_PATH;
char *DIR_PSEUDOCODIGO;
FILE *FILE_PSEUDOCODIGO;
int TAM_MEMORIA_ACTUAL;
t_list *lista_procesos;
bool *bitmap;

// Semaforos
sem_t creacion;
sem_t memo_usuario;
sem_t asignar_pag;
sem_t memo_swap;
sem_t cambiar_tam_memoria;

sem_t operacion;

// VER DICCIONARIO Y  CAMBIAR POR LISTA INSTRUCCIONES

void *MEMORIA_USUARIO;

// Estructuras
struct pcb // proceso
{
    int PID;
    // int PC;                   //Es el ID de la lista
    int tamanio;
    // int tamanioEnPag;
    int accesoTablaPag;
    int instruccionSolicitada;
    int bajadaSWAP;
    int subidasMemo;
    int cantLecturas;
    int cantEscrituras;
    t_list *lista_instrucciones;
    t_list *Tabla_Pag;
};

// Funciones que funcionan
void iniciar_config(char *vector)
{
    nuevo_conf = config_create(vector);

    // Carga todas las globales del archivo
    PUERTO_ESCUCHA = config_get_string_value(nuevo_conf, "PUERTO_ESCUCHA");
    TAM_MEMORIA = config_get_int_value(nuevo_conf, "TAM_MEMORIA");
    TAM_PAGINA = config_get_int_value(nuevo_conf, "TAM_PAGINA");
    ENTRADAS_POR_TABLA = config_get_int_value(nuevo_conf, "ENTRADAS_POR_TABLA");
    CANTIDAD_NIVELES = config_get_int_value(nuevo_conf, "CANTIDAD_NIVELES");
    RETARDO_MEMORIA = config_get_int_value(nuevo_conf, "RETARDO_MEMORIA");
    PATH_SWAPFILE = config_get_string_value(nuevo_conf, "PATH_SWAPFILE");
    RETARDO_SWAP = config_get_int_value(nuevo_conf, "RETARDO_SWAP");
    char *nivel_log = config_get_string_value(nuevo_conf, "LOG_LEVEL");
    LOG_LEVEL = log_level_from_string(nivel_log);
    DUMP_PATH = config_get_string_value(nuevo_conf, "DUMP_PATH");
    if (!DUMP_PATH)
        abort();
    DIR_PSEUDOCODIGO = config_get_string_value(nuevo_conf, "PATH_PSEUDOCODIGO");
    if (!DIR_PSEUDOCODIGO)
        abort();

    return;
}

int Asociar_Proceso_a_Marco()
{
    sem_wait(&asignar_pag);
    int taman = TAM_MEMORIA / TAM_PAGINA;
    for (int i = 0; i < taman; i++)
    {
        if (!bitmap[i])
        {
            bitmap[i] = 1;
            memset(MEMORIA_USUARIO + (i * TAM_PAGINA), '_', TAM_PAGINA);
            sem_post(&asignar_pag);
            return i;
        }
    }
    abort();
    sem_post(&asignar_pag);
    return -1;
}

void Liberar_Proceso_de_Marco(int i)
{
    sem_wait(&asignar_pag);
    if (bitmap[i])
    {
        bitmap[i] = 0;
    }
    sem_post(&asignar_pag);
    return;
}

// El tamaño de paginas esta casteado a TAM_PAGINA
t_list *generarTablaTamaño(int tam)
{
    t_list *tabla = list_create();

    if (tam == 0)
    {
        return tabla;
    }
    t_list *listaPaginas = list_create();

    for (int i = 0; i < tam / TAM_PAGINA; i++)
    {
        int *puntero = malloc(sizeof(int));
        // sem_wait(&asignar_pag);
        *puntero = Asociar_Proceso_a_Marco(); // obtenerDireccion();
        // sem_post(&asignar_pag);
        list_add(listaPaginas, puntero);
    }

    for (int i = 1; i <= CANTIDAD_NIVELES; i++)
    {
        while (list_size(listaPaginas) != 0)
        {
            t_list *intermedia = list_create();
            int actual = 0;
            while (actual < ENTRADAS_POR_TABLA && list_size(listaPaginas) > 0)
            {
                list_add(intermedia, list_remove(listaPaginas, 0)); // PROBLEMA CON VALGRIM, NO LIBERA MEMORIA
                actual++;
            }
            list_add(tabla, intermedia);
            // intermedia=list_create();
            // list_destroy(intermedia);
        }

        list_destroy(listaPaginas);

        if (i < CANTIDAD_NIVELES)
        {
            // list_destroy(listaPaginas);
            listaPaginas = tabla;
            tabla = list_create();
        }
        /*else
        {
            list_destroy(listaPaginas);
        }*/
    }
    return tabla;
}

// El tamaño de paginas esta casteado a TAM_PAGINA
t_list *reasignar_tabla(int tam, FILE *swap)
{
    t_list *tabla = list_create();

    if (tam == 0)
    {
        return tabla;
    }

    t_list *listaPaginas = list_create();

    sem_wait(&memo_usuario);
    for (int i = 0; i < tam / TAM_PAGINA; i++)
    {
        int *puntero = malloc(sizeof(int)); // TODO Falta liberar este coso horroro de acá
        // sem_wait(&asignar_pag);
        *puntero = Asociar_Proceso_a_Marco(); // obtenerDireccion();
        // sem_post(&asignar_pag);
        int leido = fread(MEMORIA_USUARIO + ((*puntero) * TAM_PAGINA), sizeof(char) * TAM_PAGINA, 1, swap);
        if (leido == 0 || leido == -1)
        {
            // log_debug(log_memo, "es el proceso con %d en pags %d", tam, tam / TAM_PAGINA);
            // log_debug(log_memo, "No capo");
            abort();
        }
        list_add(listaPaginas, puntero);
    }
    sem_post(&memo_usuario);

    for (int i = 1; i <= CANTIDAD_NIVELES; i++)
    {
        while (list_size(listaPaginas) != 0)
        {
            t_list *intermedia = list_create();
            int actual = 0;
            while (actual < ENTRADAS_POR_TABLA && list_size(listaPaginas) > 0)
            {
                list_add(intermedia, list_remove(listaPaginas, 0)); // PROBLEMA CON VALGRIM, NO LIBERA MEMORIA
                actual++;
            }
            list_add(tabla, intermedia);
        }

        list_destroy(listaPaginas);

        if (i < CANTIDAD_NIVELES)
        {
            listaPaginas = tabla;
            tabla = list_create();
        }
    }
/*
for (int i = 1; i <= CANTIDAD_NIVELES; i++)
    {
        while (list_size(listaPaginas) != 0)
        {
            t_list *intermedia = list_create();
            int actual = 0;
            while (actual < ENTRADAS_POR_TABLA && list_size(listaPaginas) > 0)
            {
                list_add(intermedia, list_remove(listaPaginas, 0)); // PROBLEMA CON VALGRIM, NO LIBERA MEMORIA
                actual++;
            }
            list_add(tabla, intermedia);
            // intermedia=list_create();
            // list_destroy(intermedia);
        }

        list_destroy(listaPaginas);

        if (i < CANTIDAD_NIVELES)
        {
            // list_destroy(listaPaginas);
            listaPaginas = tabla;
            tabla = list_create();
        }
        
    }*/
    return tabla;
}

void crear_proceso(char *archivo, int tamanio_casteado, int num_proceso)
{ // Hay espacio en memoria y se crea el proceso
    struct pcb *proceso = malloc(sizeof(struct pcb));

    if (!proceso)
        abort();
    proceso->PID = num_proceso;
    proceso->lista_instrucciones = list_create();
    proceso->Tabla_Pag = generarTablaTamaño(tamanio_casteado);
    proceso->tamanio = tamanio_casteado;
    proceso->accesoTablaPag = 0;
    proceso->instruccionSolicitada = 0;
    proceso->bajadaSWAP = 0;
    proceso->subidasMemo = 1;
    proceso->cantLecturas = 0;
    proceso->cantEscrituras = 0;

    int tam_dir = 1024;
    char *dir = malloc(tam_dir);
    memset(dir, 0, tam_dir);
    strcat(dir, DIR_PSEUDOCODIGO);

    // Completa la ruta al archivo
    // strcat(dir, "pseudocodigo.txt");
    strcat(dir, archivo);

    if ((FILE_PSEUDOCODIGO = fopen(dir, "r")) != NULL)
    {
        char cont[256];

        // Obtiene lineas hasta el final de archivo
        while (fgets(cont, 256, FILE_PSEUDOCODIGO))
        {
            char *guarda = string_duplicate(cont);          // Pone la linea en un puntero
            list_add(proceso->lista_instrucciones, guarda); // Aniade a la lista la linea de turno
        }
        fclose(FILE_PSEUDOCODIGO);
        list_add(lista_procesos, proceso);
    }
    free(dir);
    log_info(log_memo, "## PID: %d - Proceso Creado - Tamaño: %d", proceso->PID, proceso->tamanio);
    return;
}

void peticion_creacion(int tamanio, char *archivo, int PDI, int *socket)
{
    sem_wait(&creacion);
    sem_wait(&cambiar_tam_memoria);
    char *mensaje;

    if (tamanio < 0 || tamanio > TAM_MEMORIA_ACTUAL)
    {
        mensaje = "NO";
    }
    else if (tamanio % TAM_PAGINA != 0)
    {
        tamanio = (tamanio - (tamanio % TAM_PAGINA) + TAM_PAGINA);
        TAM_MEMORIA_ACTUAL -= tamanio;
        crear_proceso(archivo, tamanio, PDI);
        mensaje = "OK";
    }
    else
    {
        TAM_MEMORIA_ACTUAL -= tamanio;
        crear_proceso(archivo, tamanio, PDI);
        mensaje = "OK";
    }
    enviar_mensaje(mensaje, *socket);
    // free(mensaje);
    sem_post(&cambiar_tam_memoria);
    sem_post(&creacion);
    return;
}

void liberar(t_list *tabla, int nivel_actual, int nivel_max)
{
    if (list_is_empty(tabla))
    {
        list_destroy(tabla);
        tabla=NULL;
        return;
    }
    else if (tabla == NULL)
    {

        return;
    }

    int tam_tabla = list_size(tabla);
    for (int i = 0; i < tam_tabla; i++)
    {
        t_list *elemento = list_get(tabla, i);
        if (nivel_actual < nivel_max)
        {
            // Si aún no llegamos al último nivel, asumimos que es otra t_list*
            liberar(elemento, nivel_actual + 1, nivel_max);
        }
        else
        {
            // Último nivel: liberar punteros individuales
            while (0 < list_size(elemento))
            {
                int *puntero = list_remove(elemento, 0);
                // sem_wait(&asignar_pag);
                Liberar_Proceso_de_Marco(*puntero);
                // sem_post(&asignar_pag);
                free(puntero);
            }
            list_destroy(elemento);
            // free(elemento);
        }
    }
    list_destroy(tabla); // libera solo la lista (no los elementos, ya fueron)
    tabla=NULL;
    return;
}

void destruir_proceso(void *pro)
{
    sem_wait(&creacion);
    struct pcb *proceso = (struct pcb *)pro;
    // log_trace(log_memo,"Destruir proceso restante %d",proceso->PID);
    liberar(proceso->Tabla_Pag, 1, CANTIDAD_NIVELES);
    sem_wait(&cambiar_tam_memoria);
    TAM_MEMORIA_ACTUAL += proceso->tamanio;
    sem_post(&cambiar_tam_memoria);
    log_info(log_memo, "## PID %d - Proceso Destruido - Métricas - Acc.T.Pag: %d; Inst.Sol.: %d; SWAP: %d; Mem.Prin.: %d; Lec.Mem.: %d; Esc.Mem.: %d", proceso->PID, proceso->accesoTablaPag, proceso->instruccionSolicitada, proceso->bajadaSWAP, proceso->subidasMemo, proceso->cantLecturas, proceso->cantEscrituras);
    list_destroy_and_destroy_elements(proceso->lista_instrucciones, free);
    free(proceso);

    sem_post(&creacion);
    return;
}

struct pcb *find_by_PID(t_list *lista, int i)
{
    // Ver si poner semaforo para un proceso que se destruye pero deberia dar falso
    bool PID_contains(void *ptr)
    {
        struct pcb *proceso = (struct pcb *)ptr;
        return proceso->PID == i;
    }
    return list_find(lista, PID_contains);
}

void enviar_instruccion(int pro, int instruccion, int *socket)
{
    sem_wait(&creacion);
    struct pcb *proceso = find_by_PID(lista_procesos, pro);
    // log_trace(log_memo, "Guardar %s\n", (char *)list_get(proceso->lista_instrucciones, instruccion));
    //  Envia list_get(proceso->lista_instrucciones, instruccion)
    char *cadena;
    if (instruccion < list_size(proceso->lista_instrucciones))
    {
        cadena = string_duplicate((char *)list_get(proceso->lista_instrucciones, instruccion));
        proceso->instruccionSolicitada += 1;
        log_info(log_memo, "## PID: %d - Obtener instrucción: %d - Instrucción: %s", proceso->PID, instruccion + 1, cadena);
    }
    else
    {

        cadena = "NO";
        enviar_mensaje(cadena, *socket);
        return;
    }
    enviar_mensaje(cadena, *socket);
    free(cadena);
    sem_post(&creacion);
    return;
}
/*
void eliminar_susp(int i)
{

    struct pcb *proceso = find_by_PID(lista_procesos, i);
    sem_wait(&cambiar_tam_memoria);

    int PID = -1, tam;
    FILE *swap;
    FILE *reemplazo;
    sem_wait(&memo_swap);
    if ((swap = fopen(PATH_SWAPFILE, "rb")) && (reemplazo = fopen("reemplazo", "wb")))
    {
        usleep(RETARDO_SWAP * 1000);
        while (fread(&PID, sizeof(int), 1, swap) == 1 && fread(&tam, sizeof(int), 1, swap) == 1)
        {
            if (PID == i)
            {
                t_list *tabla = list_create();
                t_list *listaPaginas = list_create();

                for (int i = 0; i < tam / TAM_PAGINA; i++)
                {
                    int *puntero = malloc(sizeof(int)); // TODO Falta liberar este coso horroro de acá

                    fseek(swap, sizeof(char) * TAM_PAGINA, SEEK_CUR);
                    list_add(listaPaginas, puntero);
                }

                for (int i = 1; i <= CANTIDAD_NIVELES; i++)
                {
                    while (list_size(listaPaginas) != 0)
                    {
                        t_list *intermedia = list_create();
                        int actual = 0;
                        while (actual < ENTRADAS_POR_TABLA && list_size(listaPaginas) > 0)
                        {
                            list_add(intermedia, list_remove(listaPaginas, 0)); // PROBLEMA CON VALGRIM, NO LIBERA MEMORIA
                            actual++;
                        }
                        list_add(tabla, intermedia);
                    }

                    list_destroy(listaPaginas);

                    if (i < CANTIDAD_NIVELES)
                    {
                        listaPaginas = tabla;
                        tabla = list_create();
                    }
                }
                proceso->bajadaSWAP += 1;

                proceso->Tabla_Pag = tabla;
            }
            else
            {
                fwrite(&PID, sizeof(int), 1, reemplazo);
                fwrite(&tam, sizeof(int), 1, reemplazo);
                char *cad_remp = malloc(sizeof(char) * tam);
                fread(cad_remp, sizeof(char) * tam, 1, swap);
                fwrite(cad_remp, sizeof(char) * tam, 1, reemplazo);
                free(cad_remp);
            }
        }
        fclose(reemplazo);
        fclose(swap);
        remove(PATH_SWAPFILE);
        rename("reemplazo", PATH_SWAPFILE);
        sem_post(&memo_swap);
    }

    sem_post(&cambiar_tam_memoria);
    return;
}
*/

void bajar_de_swap(int i)
{
    int PID = -1, tam;
    FILE *swap;
    FILE *reemplazo;
    sem_wait(&memo_swap);
    if ((swap = fopen(PATH_SWAPFILE, "rb")) && (reemplazo = fopen("reemplazo", "wb")))
    {
        usleep(RETARDO_SWAP * 1000);
        while (fread(&PID, sizeof(int), 1, swap) == 1 && fread(&tam, sizeof(int), 1, swap) == 1)
        {
            if (PID == i)
            {
                // log_trace(log_memo, "%d estoy en %ld de swap", PID, ftell(swap));
                fseek(swap, sizeof(char) * tam, SEEK_CUR);
                // log_trace(log_memo, "%d estoy en %ld de swap", PID, ftell(swap));
            }
            else
            {
                // log_trace(log_memo, "%d estoy en %ld de swap", PID, ftell(swap));
                fwrite(&PID, sizeof(int), 1, reemplazo);
                fwrite(&tam, sizeof(int), 1, reemplazo);
                char *cad_remp = malloc(sizeof(char) * tam);
                fread(cad_remp, sizeof(char) * tam, 1, swap);
                fwrite(cad_remp, sizeof(char) * tam, 1, reemplazo);
                free(cad_remp);
                // log_trace(log_memo, "%d estoy en %ld de swap", PID, ftell(swap));
            }
        }
        fclose(reemplazo);
        fclose(swap);
        remove(PATH_SWAPFILE);
        rename("reemplazo", PATH_SWAPFILE);
        sem_post(&memo_swap);
    }

    return;
}

void eliminar_proceso(int i, int *socket)
{

    bool mismoPDI(void *pdi)
    {
        struct pcb *pro = (struct pcb *)pdi;
        return pro->PID == i;
    }

    sem_wait(&creacion);
    struct pcb *proceso = list_remove_by_condition(lista_procesos, mismoPDI);
    sem_post(&creacion);
    // struct pcb *proceso= find_by_PID(lista,i);

    /*if (proceso->Tabla_Pag == NULL)
    {
        bajar_de_swap(proceso->PID);
    }
    else
    {
        liberar(proceso->Tabla_Pag, 1, CANTIDAD_NIVELES);
    }*/

    if (proceso->Tabla_Pag == NULL)
    {
        bajar_de_swap(i);
    }

    if (proceso->Tabla_Pag)
    {
        sem_wait(&cambiar_tam_memoria);
        liberar(proceso->Tabla_Pag, 1, CANTIDAD_NIVELES);

        TAM_MEMORIA_ACTUAL += proceso->tamanio;
        // log_trace(log_memo, "memoria tiene %d",TAM_MEMORIA_ACTUAL);
        sem_post(&cambiar_tam_memoria);
    }

    list_destroy_and_destroy_elements(proceso->lista_instrucciones, free);
    log_info(log_memo, "## PID %d - Proceso Destruido - Métricas - Acc.T.Pag: %d; Inst.Sol.: %d; SWAP: %d; Mem.Prin.: %d; Lec.Mem.: %d; Esc.Mem.: %d", proceso->PID, proceso->accesoTablaPag, proceso->instruccionSolicitada, proceso->bajadaSWAP, proceso->subidasMemo, proceso->cantLecturas, proceso->cantEscrituras);

    free(proceso);
    char *mensaje = "OK";
    enviar_mensaje(mensaje, *socket);
    return;
}

// ESTRUCTURA DE SWAP = int(PID) int(tamanio) char*(contenido con volumen = tamanio)

void tabla_a_archivo(t_list *tabla, int nivel_actual, int nivel_max, FILE *swap)
{
    if (!tabla)
        return;

    for (int i = 0; i < list_size(tabla); i++)
    {
        t_list *elemento = list_get(tabla, i);
        // log_trace(log_memo,"la tabla tiene %d",list_size(elemento));
        if (nivel_actual < nivel_max)
        {
            // Si aún no llegamos al último nivel, asumimos que es otra t_list*
            tabla_a_archivo(elemento, nivel_actual + 1, nivel_max, swap);
        }
        else
        {
            sem_wait(&memo_usuario);
            while (0 < list_size(elemento))
            {
                int *pag = list_remove(elemento, 0);
                // log_trace(log_memo,"se guardo el marco %d",pag);  // REEMPLAZAR POR PAG EN LUGAR DE INT

                fwrite(MEMORIA_USUARIO + ((*pag) * TAM_PAGINA), sizeof(char) * TAM_PAGINA, 1, swap);

                // sem_wait(&asignar_pag);
                Liberar_Proceso_de_Marco(*pag);
                // sem_post(&asignar_pag);
                free(pag);
            }
            sem_post(&memo_usuario);

            list_destroy(elemento);
        }
    }
    // if(nivel_actual==1)

    list_destroy(tabla); // libera solo la lista (no los elementos, ya fueron)
    tabla = NULL;
    return;
}

void suspender(int i, int *socket)
{
    struct pcb *proceso = find_by_PID(lista_procesos, i);
    int tam = proceso->tamanio;
    int PID = proceso->PID;
    FILE *swap;
    sem_wait(&memo_swap);

    if (proceso->Tabla_Pag == NULL)
    {
        char *mensaje = "NO";
        enviar_mensaje(mensaje, *socket);
        sem_post(&memo_swap);
        return;
    }

    if ((swap = fopen(PATH_SWAPFILE, "ab")))
    {
        usleep(RETARDO_SWAP * 1000);
        // log_trace(log_memo,"archivo abierto escritura");
        fwrite(&PID, sizeof(int), 1, swap);
        fwrite(&tam, sizeof(int), 1, swap);
        // log_trace(log_memo,"proceos %d",PID);

        if (tam == 0)
        {
            liberar(proceso->Tabla_Pag, 1, CANTIDAD_NIVELES);
            proceso->Tabla_Pag = NULL;
            char *mensaje = "OK";
            enviar_mensaje(mensaje, *socket);
            sem_post(&memo_swap);
            return;
        }

        tabla_a_archivo(proceso->Tabla_Pag, 1, CANTIDAD_NIVELES, swap);
        fclose(swap);

        proceso->bajadaSWAP += 1;
        sem_post(&memo_swap);
        // log_trace(log_memo, "El proceso %d pesa %d", proceso->PID, proceso->tamanio);

        proceso->Tabla_Pag = NULL;

        sem_wait(&cambiar_tam_memoria);
        TAM_MEMORIA_ACTUAL += tam;
        sem_post(&cambiar_tam_memoria);
        char *mensaje = "OK";
        enviar_mensaje(mensaje, *socket);
    }
    else
    {
        sem_post(&memo_swap);
    }

    return;
}

void desuspender(int i, int *socket)
{

    char *mensaje;
    struct pcb *proceso = find_by_PID(lista_procesos, i);

    sem_wait(&cambiar_tam_memoria);
    

    if (proceso->tamanio > TAM_MEMORIA_ACTUAL)
    {
        mensaje = "NO";
    }
    else
    {
        TAM_MEMORIA_ACTUAL -= proceso->tamanio;
        int PID = -1, tam;
        FILE *swap;
        FILE *reemplazo;
        sem_wait(&memo_swap);
        if ((swap = fopen(PATH_SWAPFILE, "rb")) && (reemplazo = fopen("reemplazo", "wb")))
        {
            usleep(RETARDO_SWAP * 1000);
            while (fread(&PID, sizeof(int), 1, swap) == 1 && fread(&tam, sizeof(int), 1, swap) == 1)
            {

                if (PID == i)
                {
                    // log_debug(log_memo, "num proceso %d", PID);
                    proceso->Tabla_Pag = reasignar_tabla(tam, swap); // READ que adelanta
                    proceso->subidasMemo += 1;
                }
                else
                {
                    fwrite(&PID, sizeof(int), 1, reemplazo);
                    fwrite(&tam, sizeof(int), 1, reemplazo);
                    char *cad_remp = malloc(sizeof(char) * tam);
                    int leido = fread(cad_remp, sizeof(char) * tam, 1, swap);
                    if (leido == 0 || leido == -1)
                    {
                        // log_trace(log_memo,"piden %d que es %d tam %d tiene %s primera instruccion %s y segunda %s",i,PID,tam,cad_remp,list_get(proceso->lista_instrucciones,0),list_get(proceso->lista_instrucciones,1));
                        // log_debug(log_memo, "ayuda");
                        abort();
                    }

                    /*size_t leidos = fread(cad_remp, sizeof(char), tam, swap);
                    if (leidos < tam) {
                        memset(cad_remp + leidos, 0, tam - leidos);  // Inicializar lo que no se leyó
                    }*/

                    fwrite(cad_remp, sizeof(char) * tam, 1, reemplazo);
                    // log_trace(log_memo,"%d tam %d tiene %s",PID,tam,cad_remp);
                    free(cad_remp);
                }
            }
            fclose(reemplazo);
            fclose(swap);
            remove(PATH_SWAPFILE);
            rename("reemplazo", PATH_SWAPFILE);
        }
        sem_post(&memo_swap);
        mensaje = "OK";
    }

    // log_trace(log_memo, "El proceso %d pesa %d", proceso->PID, proceso->tamanio);
    sem_post(&cambiar_tam_memoria);
    enviar_mensaje(mensaje, *socket);
    return;
}

void acceso_tabla_paginas(t_list *tabla, int pag[], int nivel_actual, int *accesoTablaPag, int *socket)
{
    // Esperar tiempo espera
    // log_trace(log_memo, "la pag tiene %d paginas y se pide acceder a la %d", list_size(tabla), pag[nivel_actual - 1] + 1);

    // t_paquete * paquete=crear_paquete();

    if (pag[nivel_actual - 1] < list_size(tabla))
    {
        usleep(RETARDO_MEMORIA * 1000);
        if (nivel_actual < CANTIDAD_NIVELES)
        {
            t_list *aux = list_get(tabla, pag[nivel_actual - 1]);
            *accesoTablaPag += 1;
            acceso_tabla_paginas(aux, pag, nivel_actual + 1, accesoTablaPag, socket);
        }
        else
        {
            int *marco = list_get(tabla, pag[nivel_actual - 1]);
            if (*marco == -1)
                log_debug(log_memo, "-1");
            // log_trace(log_memo, "el marco accedido es %d", *marco);
            // agregar_a_paquete(paquete,(void*)marco,sizeof(int));
            char *aux = string_itoa(*marco);
            enviar_mensaje(aux, *socket);
            free(aux);
        }
    }
    else
    {

        // int error=-1;
        // agregar_a_paquete(paquete,(void*)&error,sizeof(int));
        enviar_mensaje("NO", *socket);
    }
    // enviar_paquete(paquete,*socket);
    // eliminar_paquete(paquete);
    return;
}

void acceder_a_marco(int pro, int niveles[], int *socket)
{
    sem_wait(&creacion);
    struct pcb *proceso = find_by_PID(lista_procesos, pro);
    proceso->accesoTablaPag += 1;
    acceso_tabla_paginas(list_get(proceso->Tabla_Pag, 0), niveles, 1, &proceso->accesoTablaPag, socket);
    sem_post(&creacion);
    return;
}

void dumpeo(t_list *tabla, int nivel_actual, int nivel_max, FILE *dump)
{
    if (!tabla)
        return;

    for (int i = 0; i < list_size(tabla); i++)
    {
        t_list *elemento = list_get(tabla, i);

        if (nivel_actual < nivel_max)
        {
            // Si aún no llegamos al último nivel, asumimos que es otra t_list*
            dumpeo(elemento, nivel_actual + 1, nivel_max, dump);
        }
        else
        {
            int inc = 0;
            while (inc < list_size(elemento))
            {
                int *pag = list_get(elemento, inc);
                sem_wait(&memo_usuario);
                fwrite((char *)(MEMORIA_USUARIO + ((*pag) * TAM_PAGINA)), sizeof(char) * TAM_PAGINA, 1, dump);
                sem_post(&memo_usuario);
                inc++;
            }
        }
    }
    return;
}

void dump_memory(int pro, int *socket)
{
    struct pcb *proceso = find_by_PID(lista_procesos, pro);
    FILE *dump;
    int tam_archivo = 1024;
    char *archivo = malloc(tam_archivo);
    memset(archivo, 0, tam_archivo);
    strcat(archivo, DUMP_PATH);
    char *num_pro = string_itoa(proceso->PID);
    strcat(archivo, num_pro);
    char *fecha = temporal_get_string_time("-%H:%M:%S.dmp");
    strcat(archivo, fecha);
    char *tam = string_itoa(proceso->tamanio);
    char *mensaje;
    if ((dump = fopen(archivo, "w")) != NULL)
    {
        txt_write_in_file(dump, tam);
        // fwrite(&tam,sizeof(tam),1,dump);
        // sem_wait(&memo_usuario);
        dumpeo(proceso->Tabla_Pag, 1, CANTIDAD_NIVELES, dump);
        // sem_post(&memo_usuario);
        fclose(dump);
        mensaje = "OK";
    }
    else
    {
        mensaje = "NO";
    }
    free(archivo);
    free(fecha);
    free(num_pro);
    free(tam);
    log_info(log_memo, "## PID: %d - Memory Dump solicitado", proceso->PID);
    enviar_mensaje(mensaje, *socket);
    return;
}

void leer_pag_entera(int pro, int marco)
{
    struct pcb *proceso = find_by_PID(lista_procesos, pro);
    char *cadena = malloc((sizeof(char) * TAM_PAGINA) + 1);
    // memset(cadena,0,TAM_PAGINA);
    // strcpy(cadena,MEMORIA_USUARIO);
    sem_wait(&memo_usuario);
    memcpy(cadena, MEMORIA_USUARIO + (marco), TAM_PAGINA);
    sem_post(&memo_usuario);
    strcat(cadena, "\0");
    // log_trace(log_memo, "Se pide enviar la cadena %s", cadena);
    free(cadena);
    proceso->cantLecturas++;
    log_info(log_memo, "## PID: %d - Lectura - Dir. Física: %d - Tamaño: %d", proceso->PID, ((marco * TAM_PAGINA)), TAM_PAGINA);
    return;
}

void leer_pag_por_tam(int pro, int dir_fisica, int tam, int *socket)
{
    sem_wait(&memo_usuario);
    if (tam > TAM_PAGINA - (dir_fisica % TAM_PAGINA))
    {
        char *cadena = "NO";
        enviar_mensaje(cadena, *socket);
    }
    else
    {
        struct pcb *proceso = find_by_PID(lista_procesos, pro);
        char *cadena = malloc(tam + 1);
        // memset(cadena,0,TAM_PAGINA);
        // strcpy(cadena,MEMORIA_USUARIO);

        memcpy(cadena, (char *)(MEMORIA_USUARIO + (dir_fisica)), tam);
        cadena[tam] = '\0';
        // log_trace(log_memo, "Se pide enviar la cadena %s", cadena);
        proceso->cantLecturas++;
        log_info(log_memo, "## PID: %d - Lectura - Dir. Física: %d - Tamaño: %d", proceso->PID, ((dir_fisica)), tam);

        enviar_mensaje(cadena, *socket);
        free(cadena);
    }
    sem_post(&memo_usuario);
    return;
}

void actualizar_pag_completa(int pro, int dir, int tam, char *cont, int *socket)
{
    sem_wait(&memo_usuario);
    char *cadena;
    if (tam > TAM_PAGINA - (dir % TAM_PAGINA))
    {
        cadena = "NO";
    }
    else
    {
        struct pcb *proceso = find_by_PID(lista_procesos, pro);

        // memset(MEMORIA_USUARIO + (dir ), 0, TAM_PAGINA);
        memcpy(MEMORIA_USUARIO + (dir), cont, tam * sizeof(char));

        proceso->cantEscrituras++;
        log_info(log_memo, "## PID: %d - Escritura - Dir. Física: %d - Tamaño: %d", proceso->PID, ((dir)), tam);
        cadena = "OK";
    }
    sem_post(&memo_usuario);
    enviar_mensaje(cadena, *socket);
    // free(cadena);
    return;
}

void *ingresar_conexion(void *socket_void)
{
    int socket = *((int *)socket_void);
    free(socket_void);

    t_paquete *paquete = crear_paquete();
    agregar_a_paquete(paquete, (void *)&TAM_PAGINA, sizeof(int));
    agregar_a_paquete(paquete, (void *)&CANTIDAD_NIVELES, sizeof(int));
    agregar_a_paquete(paquete, (void *)&ENTRADAS_POR_TABLA, sizeof(int));
    enviar_paquete(paquete, socket);
    eliminar_paquete(paquete);

    while (1)
    {
        t_list *partes = recibir_paquete(socket);
        if (list_is_empty(partes))
        {
            list_destroy_and_destroy_elements(partes, free);
            return NULL;
        }

        
        sem_wait(&operacion);

        int *tarea = (int *)list_get(partes, 0);
        int *PID;
        if ((*tarea) != CORTAR)
        {
            PID = (int *)list_get(partes, 1);
            //log_debug(log_memo, "PID - %d", *PID);
        }

        // log_trace(log_memo,"se pide cod %d con pid %d",*tarea,*PID);

        int *tam_proceso;
        char *archivo;
        int *entradas;
        int *direccion_fisica;
        int *tam_leer_escribir;
        char *mensaje_a_escribir;
        int *num_tarea;
        usleep(RETARDO_MEMORIA * 1000);
        

        //log_debug(log_memo, "tarea %d", *tarea);

        switch (*tarea)
        {
            // Kernel
        case PROCESO_NUEVO:
            log_info(log_memo, "## Kernel Conectado - FD del socket: %d", socket);
            tam_proceso = (int *)list_get(partes, 2);
            archivo = string_duplicate((char *)list_get(partes, 3));
            // log_trace(log_memo, "el archivos es %s", archivo);
            peticion_creacion(*tam_proceso, archivo, *PID, &socket);

            free(archivo);
            list_destroy_and_destroy_elements(partes, free);
            sem_post(&operacion);
            return NULL;
        case SUSPENDER:
            log_info(log_memo, "## Kernel Conectado - FD del socket: %d", socket);
            suspender(*PID, &socket);
            // log_debug(log_memo, "Pide suspender el proceso %d", *PID);
            list_destroy_and_destroy_elements(partes, free);
            sem_post(&operacion);
            return NULL;
        case DES_SUSPENDER:
            log_info(log_memo, "## Kernel Conectado - FD del socket: %d", socket);
            // log_debug(log_memo, "Pide desuspender el proceso %d", *PID);
            desuspender(*PID, &socket);
            list_destroy_and_destroy_elements(partes, free);
            sem_post(&operacion);
            return NULL;
        case FINALIZAR:
            log_info(log_memo, "## Kernel Conectado - FD del socket: %d", socket);
            eliminar_proceso(*PID, &socket);
            list_destroy_and_destroy_elements(partes, free);
            sem_post(&operacion);
            return NULL;
        case MEMORY_DUMP:
            log_info(log_memo, "## Kernel Conectado - FD del socket: %d", socket);
            dump_memory(*PID, &socket);
            list_destroy_and_destroy_elements(partes, free);
            sem_post(&operacion);
            return NULL;

            // CPU
        case ACCEDER_TABLA:
            entradas = malloc(sizeof(int) * CANTIDAD_NIVELES);
            for (int i = 0; i < CANTIDAD_NIVELES; i++)
            {
                entradas[i] = *((int *)list_get(partes, i + 2));
                // log_trace(log_memo,"se guardo %d en la posicion %d", entradas[i],i);
            }
            acceder_a_marco(*PID, entradas, &socket);
            free(entradas);
            sem_post(&operacion);
            break;
        case LEER_PAG:
            direccion_fisica = (int *)list_get(partes, 2);
            tam_leer_escribir = (int *)list_get(partes, 3);
            leer_pag_por_tam(*PID, *direccion_fisica, *tam_leer_escribir, &socket);
            sem_post(&operacion);
            break;
        case LEER_PAG_COMPLETA:
            direccion_fisica = (int *)list_get(partes, 2);
            leer_pag_por_tam(*PID, *direccion_fisica, TAM_PAGINA, &socket);
            sem_post(&operacion);
            break;
        case ACTUALIZAR_PAG:
            direccion_fisica = (int *)list_get(partes, 2);
            tam_leer_escribir = (int *)list_get(partes, 3);
            mensaje_a_escribir = string_duplicate((char *)list_get(partes, 4));
            actualizar_pag_completa(*PID, *direccion_fisica, *tam_leer_escribir, mensaje_a_escribir, &socket);
            free(mensaje_a_escribir);
            sem_post(&operacion);
            break;
        case ACTUALIZAR_PAG_COM:
            direccion_fisica = list_get(partes, 2);
            mensaje_a_escribir = string_duplicate((char *)list_get(partes, 3));
            actualizar_pag_completa(*PID, *direccion_fisica, TAM_PAGINA, mensaje_a_escribir, &socket);
            free(mensaje_a_escribir);
            sem_post(&operacion);
            break;
        case TAREA:
            num_tarea = list_get(partes, 2);
            enviar_instruccion(*PID, *num_tarea, &socket);
            sem_post(&operacion);
            break;
        case CORTAR:
            close(socket);
            list_destroy_and_destroy_elements(partes, free);
            sem_post(&operacion);
            return NULL;

        default:
            char *mensaje = "NO";
            enviar_mensaje(mensaje, socket);
            sem_post(&operacion);
            break;
        }
        list_destroy_and_destroy_elements(partes, free);
    }
    return NULL;
}

void *gestion_conexiones()
{
    // Crea socket y espera
    int socket_escucha = iniciar_modulo(PUERTO_ESCUCHA);
    // socket_conectado = malloc(sizeof(int));
    while (1)
    {
        // Recibe un cliente y crea un hilo personalizado para la conexión
        int conexion = establecer_conexion(socket_escucha);

        // Independiza la conexion
        int *particular = malloc(sizeof(int));
        *particular = conexion;

        // Manejamos la conexion de manera independiente
        pthread_t manejo_servidor;
        pthread_create(&manejo_servidor, NULL, ingresar_conexion, (void *)particular);
        // pthread_join(manejo_servidor,NULL);
        pthread_detach(manejo_servidor);
    }
    close(socket_escucha);
    return NULL;
}

int main(int argc, char *argv[])
{

    // if (argc < 2){
    //     abort();
    // }
    iniciar_config("/home/utnso/tp-2025-1c-RompeComputadoras/memoria/memoria.conf" /*argv[1]*/);

    log_memo = log_create("memoria.log", "memoria", true, LOG_LEVEL);
    // iniciar_config("/home/utnso/Desktop/tp-2025-1c-RompeComputadoras/memoria/memoria.conf");

    sem_init(&creacion, 1, 1);
    sem_init(&memo_usuario, 1, 1);
    sem_init(&asignar_pag, 1, 1);
    sem_init(&memo_swap, 1, 1);
    sem_init(&cambiar_tam_memoria, 1, 1);
    sem_init(&operacion, 1, 1);

    // Crea un hilo que carga las variables globales. El sistema debe esperar que termine

    // Cargamos las variables globales
    MEMORIA_USUARIO = malloc(TAM_MEMORIA);
    TAM_MEMORIA_ACTUAL = TAM_MEMORIA;
    lista_procesos = list_create();

    // Creamos el bitmap para los frames
    bitmap = malloc(sizeof(bool) * TAM_MEMORIA / TAM_PAGINA);
    memset(bitmap, 0, sizeof(bool) * TAM_MEMORIA / TAM_PAGINA);

    // Creamos el log de memoria
    // TODO Poner en false
    log_info(log_memo, "Memoria en ejecucion");

    //      peticion_creacion(tamaño_del_proceso , "archivo_de_pseudocodigo" , numero_de_proceso);
    //      acceder_a_marco(num_proceso , [posiciones_de_tabla]);
    //      actualizar_pag_completa(numero_proceso , marco_a_escribir , tamaño_a_escribir , "mensaje");
    //      leer_pag_entera(numero_proceso , marco_a_leer);
    //      leer_pag_por_tam(numero_proceso , marco_a_leer , tamaño_a_leer);
    //      dump_memory(numero_proceso_a_dumpear);
    //      suspender(numero_proceso_a_suspender);
    //      enviar_instruccion(numero_de_proceso , numero_instruccion);
    //      eliminar_proceso(numero_proceso);

    // pruebas
    /*
        int *chivo=malloc(sizeof(int));
        int *posicion = malloc((CANTIDAD_NIVELES) * sizeof(int));

        posicion[0]=0;
        posicion[1]=0;
        posicion[2]=0;

        peticion_creacion(64, "pseudocodigo.txt", 1,chivo);
        peticion_creacion(65, "pseu.txt", 2,chivo);
        peticion_creacion(64, "pseudocodigo.txt", 3,chivo);
        actualizar_pag_completa(2 , 1 , 62 , "hola__________________________________________________________",chivo);
        actualizar_pag_completa(1 , 0 , 64 , "_____________________________juan_______________________________",chivo);
        actualizar_pag_completa(2 , 2 , 64 , "hola_2__________________________________________________________",chivo);
        actualizar_pag_completa(3 , 3 , 64 , "hola_roquefeleeererererererererererererererererererererererererr",chivo);

        acceder_a_marco(3, posicion,chivo);
        enviar_instruccion(1,2,chivo);

        suspender(1);
        suspender(2);
        suspender(3);

        desuspender(3,chivo);
        desuspender(1,chivo);
        desuspender(2,chivo);

        dump_memory(1);
        dump_memory(2);
        dump_memory(3);

        suspender(1);
        suspender(2);

        desuspender(2,chivo);
        desuspender(1,chivo);

        acceder_a_marco(3 , posicion,chivo);
        acceder_a_marco(1 , posicion,chivo);
        acceder_a_marco(2 , posicion,chivo);

        leer_pag_por_tam(1,3,20,chivo);
        leer_pag_entera(1,3);
        enviar_instruccion(1,2,chivo);

        eliminar_proceso(1);
        eliminar_proceso(2);
        eliminar_proceso(3);

        free(posicion);
        free(chivo);
    */

    // Creamos el hilo que crea el servidor
    pthread_t servidor;

    pthread_create(&servidor, NULL, gestion_conexiones, NULL);

    // Esperamos a que el hilo termine, aunque nunca lo haga
    pthread_join(servidor, NULL);

    // pthread_detach(servidor);

    // Limpieza general, que no realiza
    if (list_size(lista_procesos) != 0)
    {
        list_destroy_and_destroy_elements(lista_procesos, destruir_proceso);
    }
    else
    {
        list_destroy(lista_procesos);
    }
    config_destroy(nuevo_conf);
    if (socket_conectado)
    {
        close(*socket_conectado);
    }
    log_destroy(log_memo);
    free(bitmap);
    free(MEMORIA_USUARIO);

    return 0;
}
