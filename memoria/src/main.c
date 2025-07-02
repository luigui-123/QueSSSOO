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
bool*bitmap;

// VER DICCIONARIO Y  CAMBIAR POR LISTA INSTRUCCIONES
sem_t *consultar_memoria;
char *MEMORIA_USUARIO;

// Estructuras
struct pcb // proceso
{
    int PID;
    // int PC;                   //Es el ID de la lista
    int tamanio;
    //int tamanioEnPag;
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
    DIR_PSEUDOCODIGO = config_get_string_value(nuevo_conf, "PATH_PSEUDOCODIGO");

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

int Asociar_Proceso_a_Marco()
{ 
    int taman=TAM_MEMORIA / TAM_PAGINA;
    for (int i = 0; i < taman; i++)
    {
        if(!bitmap[i]){
            bitmap[i]=1;
            /*for(int j =0; j<TAM_PAGINA;j++)
                memset(MEMORIA_USUARIO+(i*TAM_PAGINA)+j,0,sizeof(char));*/
            memset(MEMORIA_USUARIO+(i*TAM_PAGINA),0,TAM_PAGINA);
            return i;
        }
    }
    abort();
    return -1;
}

void Liberar_Proceso_de_Marco(int i)
{   
    if(bitmap[i]){
        //log_trace(log_memo,"%d",i);

        /*for (int j = 0; j < TAM_PAGINA; j++)
        {
            MEMORIA_USUARIO+(i*TAM_PAGINA)+j = 0;
        }*/

        bitmap[i]=0;
        return NULL;
    }
    return NULL;
}

int pagsMaxPorNivel(int nivel)
{
    int a = 1;
    for (int i = 0; i < nivel; i++)
        a *= ENTRADAS_POR_TABLA;
    return a;
}

// El tamaño de paginas esta casteado a TAM_PAGINA
t_list *generarTablaTamaño(int tam)  
{
    t_list *tabla = list_create();
    t_list *listaPaginas = list_create();

    for (int i = 0; i < tam / TAM_PAGINA; i++)
    {
        int puntero = Asociar_Proceso_a_Marco(); // obtenerDireccion();
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
            //intermedia=list_create();
            //list_destroy(intermedia);
        }

        list_destroy(listaPaginas);
        //log_trace(log_memo, "La cant de elementos del nivel %d es %d", i, list_size(tabla));

        if (i < CANTIDAD_NIVELES)
        {
            //list_destroy(listaPaginas);
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
t_list *reasignar_tabla(int tam,FILE* swap)  
{
    t_list *tabla = list_create();
    t_list *listaPaginas = list_create();

    for (int i = 0; i < tam / TAM_PAGINA; i++)
    {
        int puntero = Asociar_Proceso_a_Marco(); // obtenerDireccion();
        fread(MEMORIA_USUARIO+(puntero*TAM_PAGINA),sizeof(char)*TAM_PAGINA,1,swap);
        log_trace(log_memo,"se reescribio el marco %d",puntero);
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
            //intermedia=list_create();
            //list_destroy(intermedia);
        }

        list_destroy(listaPaginas);
        //log_trace(log_memo, "La cant de elementos del nivel %d es %d", i, list_size(tabla));

        if (i < CANTIDAD_NIVELES)
        {
            //list_destroy(listaPaginas);
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

/*void leer_pseudo()
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

    proceso->Tabla_Pag = generarTablaTamaño(proceso->tamanioEnPag);*/
    

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

    /*int tam_dir = 1024;
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
}*/

void peticion_creacion(int tamanio,char * archivo,int PDI){
    if (tamanio < 0 || tamanio > TAM_MEMORIA_ACTUAL)
    {
        //log_trace(log_memo,"No hay suficiente memoria paraproceso %d",PDI);
        // Enviar negacion
    }
    else if (tamanio % TAM_PAGINA != 0){
        tamanio = (tamanio - (tamanio % TAM_PAGINA) + TAM_PAGINA);
        TAM_MEMORIA_ACTUAL-=tamanio;
        crear_proceso(archivo,tamanio,PDI);
    }
    else{
        TAM_MEMORIA_ACTUAL-=tamanio;
        crear_proceso(archivo,tamanio,PDI);
    }
    return;
}

void crear_proceso(char * archivo, int tamanio_casteado, int num_proceso){  // Hay espacio en memoria y se crea el proceso
    struct pcb *proceso = malloc(sizeof(struct pcb));

    if (!proceso)
        abort();
    proceso->PID = num_proceso;
    proceso->lista_instrucciones = list_create();
    proceso->Tabla_Pag=generarTablaTamaño(tamanio_casteado);
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
    //strcat(dir, "pseudocodigo.txt");
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
        //log_trace(log_memo, "Guardar %s\n", (char *)list_get(lista, i));
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

void liberar(t_list *tabla, int nivel_actual, int nivel_max)
{
    if (!tabla)
        return;

    for (int i = 0; i < list_size(tabla); i++)
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
            //for (int j = 0; j < list_size(elemento); j++)

            while(0<list_size(elemento))
            {
                Liberar_Proceso_de_Marco(list_remove(elemento,0));
            }
            
            free(elemento);
        }
    }
    list_destroy(tabla); // libera solo la lista (no los elementos, ya fueron)
    return NULL;
}

void destruir_proceso(void *pro)
{
    struct pcb *proceso = (struct pcb *)pro;
    //log_trace(log_memo,"Destruir proceso restante %d",proceso->PID);
    liberar(proceso->Tabla_Pag,1,CANTIDAD_NIVELES);
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

void eliminar_proceso(int i){

    bool mismoPDI(struct pcb* pdi){
        
        return pdi->PID==i;
    }

    struct pcb *proceso= list_remove_by_condition(lista_procesos,mismoPDI);
    //struct pcb *proceso= find_by_PID(lista,i);
    liberar(proceso->Tabla_Pag,1,CANTIDAD_NIVELES);
    list_destroy_and_destroy_elements(proceso->lista_instrucciones, free);
    free(proceso);

    //list_remove_and_destroy_by_condition(lista,mismoPDI,free);
    //free(proceso);
    return;
}

// ESTRUCTURA DE SWAP = int(PID) int(tamanio) char*(contenido con volumen = tamanio)

void tabla_a_archivo (t_list *tabla, int nivel_actual, int nivel_max,FILE* swap){
    if (!tabla)
        return;

    for (int i = 0; i < list_size(tabla); i++)
    {
        t_list *elemento = list_get(tabla, i);

        if (nivel_actual < nivel_max)
        {
            // Si aún no llegamos al último nivel, asumimos que es otra t_list*
            tabla_a_archivo(elemento, nivel_actual + 1, nivel_max,swap);
        }
        else
        {
            int item=0;
            while(item<list_size(elemento))
            {
                int pag=list_get(elemento,item);
                log_trace(log_memo,"se guardo el marco %d",pag);  // REEMPLAZAR POR PAG EN LUGAR DE INT
                /*char* contenido=malloc(TAM_PAGINA);
                memset(contenido, 0, TAM_PAGINA);
                strcat(contenido, MEMORIA_USUARIO+(pag*TAM_PAGINA));  // Simplificar?
                fwrite(contenido,sizeof(char)*TAM_PAGINA,1,swap);
                free(contenido);*/
                fwrite(MEMORIA_USUARIO+(pag*TAM_PAGINA),sizeof(char)*TAM_PAGINA,1,swap);
                Liberar_Proceso_de_Marco(pag);
                item++;
            }
            
            //free(elemento);
        }
    }
    //list_destroy(tabla); // libera solo la lista (no los elementos, ya fueron)
    
    return NULL;
}

void suspender(int i){
    struct pcb *proceso=find_by_PID(lista_procesos,i);
    int tam = proceso->tamanio;
    int PID = proceso->PID;
    FILE* swap;
    if(swap=fopen(PATH_SWAPFILE,"ab")){
        log_trace(log_memo,"archivo abierto escritura");
        fwrite(&PID,sizeof(int),1,swap);
        fwrite(&tam,sizeof(int),1,swap);
        tabla_a_archivo(proceso->Tabla_Pag,1,CANTIDAD_NIVELES,swap);
        /*t_list *lista_a_escribir;
        lista_a_escribir=list_create();
        tabla_a_archivo(proceso->Tabla_Pag,1,CANTIDAD_NIVELES,lista_a_escribir);
        while (list_size(lista_a_escribir)!=0)
        {
            int escribir=list_remove(lista_a_escribir,0);
            fwrite(&escribir,sizeof(int),1,swap);
            Liberar_Proceso_de_Marco(escribir);
            log_trace(log_memo,"se guardo el frame %d",escribir);
            //free(escribir);
        }*/
        //fclose(swap);
        //list_destroy(lista_a_escribir);
        
        // Falta las subir las paginas

        fclose(swap);
    }
    return;
}

/*t_list* generar_tabla_con_pag(int tam, int* paginas){
     t_list *tabla = list_create();
    t_list *listaPaginas = list_create();

    for (int i = 0; i < tam / TAM_PAGINA; i++)
    {
        int puntero = Asociar_Proceso_a_Marco(); 
        MEMORIA_USUARIO[puntero]=paginas[i];
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
                list_add(intermedia, list_remove(listaPaginas, 0)); 
                actual++;
            }
            list_add(tabla, intermedia);
            //intermedia=list_create();
            //list_destroy(intermedia);
        }

        list_destroy(listaPaginas);
        //log_trace(log_memo, "La cant de elementos del nivel %d es %d", i, list_size(tabla));

        if (i < CANTIDAD_NIVELES)
        {
            //list_destroy(listaPaginas);
            listaPaginas = tabla;
            tabla = list_create();
        }
        
    }
    return tabla;
}*/

void desuspender(int i){
    struct pcb *proceso=find_by_PID(lista_procesos,i);
    if (proceso->tamanio > TAM_MEMORIA_ACTUAL)
    {
        //log_trace(log_memo,"No hay suficiente memoria paraproceso %d",PDI);
        // Enviar negacion
    }
    else{
        TAM_MEMORIA_ACTUAL-=proceso->tamanio;
        //proceso->Tabla_Pag=generarTablaTamaño(proceso->tamanio);
        int PID=-1,tam;
        FILE* swap;
        if(swap=fopen(PATH_SWAPFILE,"rb")){
            log_trace(log_memo,"archivo abierto lectura");

            // WHILE CON EOF
            while(!feof(swap)){
                fread(&PID,sizeof(int),1,swap);
                fread(&tam,sizeof(int),1,swap);
                if(PID==i){
                    liberar(proceso->Tabla_Pag,1,CANTIDAD_NIVELES);
                    proceso->Tabla_Pag=reasignar_tabla(tam,swap);
                    break;
                }
                else{
                    fseek(swap,sizeof(char)*TAM_PAGINA*(tam/TAM_PAGINA),SEEK_CUR);
                    log_trace(log_memo,"Se omitio el proceso %d",PID);
                }
            }
            fclose(swap);

        }
    }
    return;
}

int main(int argc, char *argv[])
{

    // Crea un hilo que carga las variables globales. El sistema debe esperar que termine
    // consultar_memoria=sem_open("SEM_MOD_MEMO", O_CREAT | O_EXCL, S_IRUSR | S_IRUSR, 0);

    // Cargamos las variables globales
    iniciar_config();
    MEMORIA_USUARIO=malloc(TAM_MEMORIA);
    TAM_MEMORIA_ACTUAL = TAM_MEMORIA;
    lista_procesos = list_create();

    // Creamos el bitmap para los frames
    bitmap = malloc(sizeof(bool) * TAM_MEMORIA / TAM_PAGINA);
    memset(bitmap, 0, sizeof(bool) * TAM_MEMORIA / TAM_PAGINA);

    // Creamos el log de memoria
    log_memo = log_create("memoria.log", "memoria", false, LOG_LEVEL);

    /*t_list* tabla1=generarTablaTamaño((TAM_PAGINA * 32),bitmap);
    t_list* tabla2=generarTablaTamaño((TAM_PAGINA * 20),bitmap);

    liberar(tabla1, 1, CANTIDAD_NIVELES,bitmap);
    //liberar(tabla2, 1, CANTIDAD_NIVELES,bitmap);

    t_list* tabla3=generarTablaTamaño((TAM_PAGINA * 40),bitmap);
    liberar(tabla3, 1, CANTIDAD_NIVELES,bitmap);

    liberar(tabla2, 1, CANTIDAD_NIVELES,bitmap);
    */
   peticion_creacion(116,"pseudocodigo.txt",1);
    peticion_creacion(1023,"pseudocodigo.txt",2);

    struct pcb *proceso2=find_by_PID(lista_procesos,2);
    enviar_toda_lista(proceso2->lista_instrucciones);

    struct pcb *proceso1=find_by_PID(lista_procesos,1);
    enviar_toda_lista(proceso1->lista_instrucciones);


    suspender(2);
    suspender(1);

    desuspender(1);
    desuspender(2);

    /*t_list* cadena=list_create();
    tabla_a_lista(proceso1->Tabla_Pag,1,CANTIDAD_NIVELES,cadena);
    log_trace(log_memo,"el tamaño en pags es de %d",list_size(cadena));*/

    eliminar_proceso(1);
    eliminar_proceso(2);    

    
    /*leer_pseudo(bitmap);

    struct pcb *proceso = find_by_PID(lista_procesos, 1);
    t_list *lista = proceso->lista_instrucciones;
    enviar_toda_lista(lista);

    // Creamos el hilo que crea el servidor
    pthread_t servidor;
    pthread_create(&servidor, NULL, gestion_conexiones, NULL);

    // Esperamos a que el hilo termine, aunque nunca lo haga
    pthread_join(servidor, NULL);*/
    // pthread_detach(servidor);

    // Limpieza general, que no realiza
    
    if (list_size(lista_procesos)!=0)
        list_destroy_and_destroy_elements(lista_procesos, destruir_proceso);
    else
        list_destroy(lista_procesos);
    config_destroy(nuevo_conf);
    if (socket_conectado)
        close(*socket_conectado);
    log_destroy(log_memo);
    free(bitmap);
    free(MEMORIA_USUARIO);

    return 0;
}
