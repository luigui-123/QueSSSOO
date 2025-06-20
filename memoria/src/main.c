
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

#define PROCESO_NUEVO 0
#define SUSPENDER 1
#define DES_SUSPENDER 2
#define FINALIZAR 3
#define ACCEDER_TABLA 4
#define ACCEDER_MEMO_USUARIO 5
#define LEER_PAG 6
#define ACTUALIZAR_PAG 7
#define MEMORY_DUMP 8

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

// VER DICCIONARIO Y  CAMBIAR POR LISTA INSTRUCCIONES
sem_t *consultar_memoria;
void *MEMORIA_USUARIO;

// Estructuras
struct pcb // proceso
{
    int PID;
    // int PC;                   //Es el ID de la lista
    int tamanio;
    int tamanioEnPag;
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
void iniciar_config()
{
    nuevo_conf = config_create("memoria.conf");

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
    DIR_PSEUDOCODIGO = config_get_string_value(nuevo_conf, "PATH_INSTRUCCIONES");

    return;
}

void *ingresar_conexion(void *socket_void)
{
    int *socket = (int *)socket_void;
    return NULL;
}

int str_to_int(char *txt, int ac)
{
    int num = 0, i = ac;

    while (txt[i] != ' ')
    {
        num = num * 10;
        num += txt[i] - '0';
        printf("%d", num);
        i++;
    }

    return num;
}

void Asociar_Proceso_a_Marco()
{ // En los parametros de la funcion iria el proceso, el puntero de frames totales y el vector de Frames obtenidos

    //  No termino de entender bien como sería para asociar

    return;
}

int pagsMaxPorNivel(int nivel)
{
    int a = 1;
    for (int i = 0; i < nivel; i++)
    {
        a *= ENTRADAS_POR_TABLA;
    }

    return a;
}

/*t_list* tablaLlenaDeTablas(int nivel){
   t_list tabla=list_create();
   if (nivel>1)
   {
       for (int j = 0; j < ENTRADAS_POR_TABLA; j++)
       {
           list_add(tabla,tablaLlenaDeTablas(nivel-1));
       }

   }
   else if(nivel == 1){
       for (int j = 0; j < ENTRADAS_POR_TABLA; j++)
       {
           //list_add(tabla,obtener direccion());              // ASIGNAR DIRECCION EN NIVEL 1
       }
   }
   return tabla;
}*/

/*t_list *tablaFinal(int nivel, int *pagNecesarias)
{
    t_list *tabla = list_create();
    if (nivel > 1)
    {
        if (pagNecesarias % pagsMaxPorNivel(nivel) != 0) // La tabla tiene un resto que se completa por separado.
        {
            for (int i = 0; i < (pagNecesarias / pagsMaxPorNivel(nivel)); i++) // "for" para la parte que si se completa toda
            {
                list_add(tabla, tablaFinal(nivel - 1, &pagNecesarias));
            }
            list_add(tabla, tablaFinal(nivel - 1, &pagNecesarias)); // Se completa el RESTO
        }

        else if (pagNecesarias >= pagsMaxPorNivel(nivel)) // El nivel se llena en su totalidad - "if" innecesario?
        {
            for (int j = 0; j < ENTRADAS_POR_TABLA; j++)
            {
                list_add(tabla, tablaFinal(nivel - 1, &pagNecesarias));
            }
        }
    }
    else if (nivel == 1) // El ultimo nivel tiene puntero a direcciones de memoria
    {
        for (int j = 0; j < ENTRADAS_POR_TABLA; j++)
        {
            // list_add(tabla,obtener direccion());              // ASIGNAR DIRECCION EN NIVEL 1
            pagNecesarias--;
        }
    }
    return tabla;
}*/

t_list *generarTablaTamaño(int tam)
{
    // int pagNecesarias=tam/TAM_PAGINA;
    t_list *tabla = list_create();
    t_list *listaPaginas = list_create();
    // tabla = tablaFinal(CANTIDAD_NIVELES,&pagNecesarias);

    for (int i = 0; i < tam / TAM_PAGINA; i++)
    {
        void *puntero = 5; // obtenerDireccion();
        list_add(listaPaginas, puntero);
    }

    for (int i = 1; i <= CANTIDAD_NIVELES; i++)
    {

        while (list_size(listaPaginas) != 0)
        {
            t_list *intermedia = list_create();
            // int recorrido=list_size(listaPaginas);
            // for (int j = 0; j < recorrido; j++)
            int actual = 0;
            while (actual < ENTRADAS_POR_TABLA && list_size(listaPaginas) > 0)
            {
                /*if (j >= ENTRADAS_POR_TABLA)
                {
                    break;
                }*/
                list_add(intermedia, list_remove(listaPaginas, 0));
                actual++;
            }
            list_add(tabla, intermedia);
            list_destroy(intermedia);
        }

        log_trace(log_memo, "La cant de elementos del nivel %d es %d", i, list_size(tabla));

        if (i < CANTIDAD_NIVELES)
        {
            listaPaginas = tabla;
            tabla=list_create();
        }
        else
        {
            list_destroy(listaPaginas);
        }
    }

    return tabla;
}

void leer_pseudo(bool *FramesDisp)
{
    struct pcb *proceso = malloc(sizeof(struct pcb));

    if (!proceso)
        abort();
    proceso->PID = 1;
    proceso->lista_instrucciones = list_create();
    proceso->Tabla_Pag = list_create();
    proceso->tamanio = 0;
    proceso->accesoTablaPag = 0;
    proceso->instruccionSolicitada = 0;
    proceso->bajadaSWAP = 0;
    proceso->subidasMemo = 1;
    proceso->cantLecturas = 0;
    proceso->cantEscrituras = 0;

    // int cantPag= proceso->tamanio / TAM_PAGINA;

    if (proceso->tamanio < 0)
    {
        list_destroy(proceso->Tabla_Pag);
        list_destroy(proceso->lista_instrucciones);
        free(proceso);
        return NULL;
    } // Mandar kernel que no se pudo
    else if (proceso->tamanio % TAM_PAGINA != 0)
        proceso->tamanioEnPag = (proceso->tamanio - (proceso->tamanio % TAM_PAGINA) + TAM_PAGINA);
    else
        proceso->tamanioEnPag = proceso->tamanio;

    proceso->Tabla_Pag = generarTablaTamaño(proceso->tamanioEnPag);

    /*
    while (cantPag / 4 > 0){ // CantPag queda como producto de 4 VER DESPUES
        cantPag = cantPag + 1;
    }

    int FramesObtendos[cantPag]; // Vector para las posiciones de los frames libres
    for (int i = 0; i < cantPag; i++){
        while (FramesDisp[j] != 0){
            j++;
            if (j > TAM_MEMORIA/TAM_PAGINA)
                return; // No se consiguieron los frames necesarios
        }
        FramesObtendos[i] = j; //Aca quedan los frames disponibles
    }*/
    // Podria venir aca una funcion tipo "Asociar_Proceso_a_Marco(proceso, FramesObtenidos) que asocie directo a todos"

    int tam_dir = 1024;
    char *dir = malloc(tam_dir);
    memset(dir, 0, tam_dir);
    strcat(dir, DIR_PSEUDOCODIGO);

    // Completa la ruta al archivo
    strcat(dir, "pseudocodigo.txt");

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
        // dictionary_put(lista_procesos,"1", (void*) &proceso); // Aniade a diccionario, "1" seria el PID
        list_add(lista_procesos, proceso);
    }
    free(dir);
    return;
}
void enviar_toda_lista(t_list *lista)
{ // Espera una lista a imprimir
    for (int i = 0; i < list_size(lista); i++)
    {
        log_trace(log_memo, "Guardar %s\n", (char *)list_get(lista, i));
    }
    return;
}

void *gestion_conexiones()
{
    // Crea socket y espera
    int socket_escucha = iniciar_modulo(PUERTO_ESCUCHA, log_memo);
    socket_conectado = malloc(sizeof(int));
    while (1)
    {

        // Recibe un cliente y crea un hilo personalizado para la conexión
        *socket_conectado = establecer_conexion(socket_escucha, log_memo);
        pthread_t manejo_servidor;
        pthread_create(&manejo_servidor, NULL, ingresar_conexion, (void *)socket_conectado);
        pthread_detach(manejo_servidor);
    }
    close(socket_escucha);
    return NULL;
}

/*void* gestion_conexiones(){
    // Crea socket y espera
    int socket_escucha = iniciar_modulo(PUERTO_ESCUCHA, log_memo);
    while(1){

        // Recibe un cliente y crea un hilo personalizado para la conexión
        socket_conectado = establecer_conexion(socket_escucha, log_memo);
        pthread_t manejo_servidor;
        pthread_create(&manejo_servidor,NULL,ingresar_conexion,(void*) socket_conectado);
        pthread_detach(manejo_servidor);

    }
    close(socket_escucha);
    return;
}*/

/*void liberar(t_list *lista, int nivel)
{
    while (list_size(lista))
    {
        if (nivel>1)
        {
            liberar(list_get(lista,0),nivel-1);
            
        }
        else{
            list_clean_and_destroy_elements(lista,free);
        }
    }
    list_destroy(lista);
    return NULL;
}*/

void liberar(t_list *tabla, int nivel_actual, int nivel_max) {
    if (!tabla) return;

    for (int i = 0; i < list_size(tabla); i++) {
        void *elemento = list_get(tabla, i);

        if (nivel_actual < nivel_max) {
            // Si aún no llegamos al último nivel, asumimos que es otra t_list*
            liberar((t_list *)elemento, nivel_actual + 1, nivel_max);
        } else {
            // Último nivel: liberar punteros individuales
            free(elemento);
        }
    }

    list_destroy(tabla);  // libera solo la lista (no los elementos, ya fueron)
}

void destruir_proceso(void *pro)
{
    struct pcb *proceso = (struct pcb *)pro;
    list_destroy_and_destroy_elements(proceso->lista_instrucciones, free);
    free(proceso);
    return;
}

struct pcb *find_by_PID(t_list *lista, int i)
{
    bool PID_contains(void *ptr)
    {
        struct pcb *proceso = (struct pcb *)ptr;
        return proceso->PID == i;
    }
    return list_find(lista, PID_contains);
}

int main(int argc, char *argv[])
{

    // Crea un hilo que carga las variables globales. El sistema debe esperar que termine
    // consultar_memoria=sem_open("SEM_MOD_MEMO", O_CREAT | O_EXCL, S_IRUSR | S_IRUSR, 0);

    // Cargamos las variables globales
    iniciar_config();
    TAM_MEMORIA_ACTUAL = TAM_MEMORIA;
    lista_procesos = list_create();

    // Creamos el bitmap para los frames
    bool *bitmap = malloc(sizeof(bool) * TAM_MEMORIA / TAM_PAGINA);
    memset(bitmap, 0, sizeof(bool) * TAM_MEMORIA / TAM_PAGINA);

    // Creamos el log de memoria
    log_memo = log_create("memoria.log", "memoria", false, LOG_LEVEL);

    liberar(generarTablaTamaño((TAM_PAGINA * 40)),1,CANTIDAD_NIVELES);

    /*leer_pseudo(bitmap);

    struct pcb *proceso = find_by_PID(lista_procesos, 1);
    t_list *lista = proceso->lista_instrucciones;
    enviar_toda_lista(lista);

    // Creamos el hilo que crea el servidor
    pthread_t servidor;
    pthread_create(&servidor, NULL, gestion_conexiones, NULL);

    // Esperamos a que el hilo termine, aunque nunca lo haga
    pthread_join(servidor, NULL);
    // pthread_detach(servidor);

    */

    // Limpieza general, que no realiza
    list_destroy_and_destroy_elements(lista_procesos, destruir_proceso);
    config_destroy(nuevo_conf);
    if (socket_conectado)
        close(*socket_conectado);
    log_destroy(log_memo);

    return 0;
}