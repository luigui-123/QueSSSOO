#include <utils/hello.h>
#include <commons/log.h>
#include <commons/config.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <commons/collections/list.h>
#include <commons/string.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <commons/txt.h>
#include <string.h>
#define PROCESO_NUEVO 0
#define SUSPENDER 1
#define DES_SUSPENDER 2
#define FINALIZAR 3
#define ACCEDER_TABLA 4
#define ACCEDER_MEMO_USUARIO 5
#define LEER_PAG 6
#define ACTUALIZAR_PAG 7
#define MEMORY_DUMP 8

// Globales
t_config* nuevo_conf;
t_log *log_memo;
int socket_conectado;
char*  PUERTO_ESCUCHA;
int TAM_MEMORIA;
int TAM_PAGINA;
int ENTRADAS_POR_TABLA;
int CANTIDAD_NIVELES;
int RETARDO_MEMORIA;
char* PATH_SWAPFILE;
int RETARDO_SWAP;
t_log_level LOG_LEVEL;
char* DUMP_PATH;
char* DIR_PSEUDOCODIGO;
FILE * FILE_PSEUDOCODIGO;

int TAM_MEMORIA_ACTUAL;
t_list *lista_enviar; 
t_list *lista_procesos; // VER DICCIONARIO
sem_t *consultar_memoria;
void* MEMORIA_USUARIO;

// Estructuras
struct pcb
{
    int PID;
    int PC;
    int tamanio;
    int accesoTablaPag;
    int instruccionSolicitada;
    int bajadaSWAP;
    int subidasMemo;
    int cantLecturas;
    int cantEscrituras;
};

struct tarea
{
    int id, num1, tam_texto, num2;
    char* texto;
};


// Funciones que funcionan

void iniciar_config()
{
	nuevo_conf = config_create("memoria.conf");
	
    // Carga todas las globales del archivo
    PUERTO_ESCUCHA= config_get_string_value(nuevo_conf, "PUERTO_ESCUCHA");
    TAM_MEMORIA= config_get_int_value(nuevo_conf, "TAM_MEMORIA");
    TAM_PAGINA= config_get_int_value(nuevo_conf, "TAM_PAGINA");
    ENTRADAS_POR_TABLA= config_get_int_value(nuevo_conf, "ENTRADAS_POR_TABLA");
    CANTIDAD_NIVELES= config_get_int_value(nuevo_conf, "CANTIDAD_NIVELES");
    RETARDO_MEMORIA= config_get_int_value(nuevo_conf, "RETARDO_MEMORIA");
    PATH_SWAPFILE= config_get_string_value(nuevo_conf, "PATH_SWAPFILE");
    RETARDO_SWAP= config_get_int_value(nuevo_conf, "RETARDO_SWAP");
    char* nivel_log=config_get_string_value(nuevo_conf, "LOG_LEVEL");
    LOG_LEVEL=log_level_from_string(nivel_log);
    DUMP_PATH= config_get_string_value(nuevo_conf, "DUMP_PATH");
    DIR_PSEUDOCODIGO=config_get_string_value(nuevo_conf, "PATH_PSEUDOCODIGO");

    //lista_procesos =list_create();

    return;
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



// FUNCIONES PARA VER

void* ingresar_conexion(void* conexion)
{
    /*
    // Transformamos la conexion a INT
    int aux=(int*) conexion;

    // Bloqueamos el proceso para ver si el proceso actual puede almacenarse en la memoria
    sem_wait(consultar_memoria);

    // Recibimos el buffer
    int pro[4] = recibir_mensaje(aux,log_memo); //CAMBIAR RECIBIR MENSAJE (instruccion, PID, PC y tamanio)

    //int cod_operacion = obtener_operacion(mensaje);
    //struct proceso proceso = recibir_proceso(mensaje); // El tamanio, PID y PC

    struct pcb proceso;

    switch (pro[0])
    {
    case (PROCESO_NUEVO): // TOCA SWAP Y LISTA DE PROCESOS
    case (DES_SUSPENDER): // TOCA SWAP Y LISTA DE PROCESOS
        // Modificar memoria si el proceso puede entrar a memoria
        if(pro[3] <= TAM_MEMORIA_ACTUAL){
            
            if(pro[0]==DES_SUSPENDER){
                pthread_t cargar_SWAP;
                pthread_create(&cargar_SWAP,leer_SWAP,(void*)pro[1]); // LEE Y RETORNA EL PROCESO COMPLETO
                pthread_join(cargar_SWAP,&proceso);
                proceso.bajadaSWAP++;
                proceso.subidasMemo++;
                enviar_mensaje("OK",conexion,log_memo); //CAMBIAR ENVIAR MENSAJE
                list_add(lista_procesos,(void*) proceso);
            }
            else if(pro[0]==PROCESO_NUEVO){
                proceso.tamanio=pro[3];
                proceso.subidasMemo=1;
                proceso.accesoTablaPag=0;
                proceso.instruccionSolicitada=0;
                proceso.cantEscrituras=0;
                proceso.cantLecturas=0;
                enviar_mensaje("OK",conexion,log_memo); //CAMBIAR ENVIAR MENSAJE
                list_add(lista_procesos,(void*) proceso);
                // QUE ES MARCO ASIGNADO ?
            }
        }
        else {
            char* mensaje="no hay memoria";
            enviar_mensaje(mensaje,conexion,log_memo);
        }
        break;
    
    case (SUSPENDER): // TOCA SWAP Y LISTA
        // ENCONTRAR Y SACAR PROCESO DE LISTA
        proceso=buscar_proceso(pro[1]);  // LO BUSCA Y SACA
        TAM_MEMORIA_ACTUAL=TAM_MEMORIA_ACTUAL+proceso.tamanio;
        pthread_t escribir_SWAP;
        pthread_create(&escribir_SWAP,guardar_SWAP,(void *) proceso); // HACER CODIGO DE GUARDAR BINARIO
        pthread_detach(escribir_SWAP);

        // QUE ES INFO NECESARIA?
        break;

    case (FINALIZAR): // TOCA LISTA Y SWAP
        // ENCONTRAR UN PROCESO Y SACARLO DE LA LISTA
        // CODIGO PARA BUSCARLO EN SWAP Y LIBERAR SUS ENTRADAS
        // Escribirlo en log
        log_trace(log_memo, "Ha finalizado el proceso %d con %d accesos a la tabla de paginas, %d instrucciones solicitadas, %d bajadas a SWAP, %d subidas a memoria, %d lecturas en memoria y %d escrituras en pagina",proceso.accesoTablaPag,proceso.instruccionSolicitada,proceso.bajadaSWAP,proceso.subidasMemo,proceso.cantLecturas,proceso.cantEscrituras);
        // cOMO LIBERAR ENTRADAS DE PROCESO?
        break;

    case (ACCEDER_TABLA): 
        
        // ????

        break;

    case (ACCEDER_MEMO_USUARIO): // TOCA MEMORIA USUARIO
        
        break;

    case (LEER_PAG): // TOCA PSEUSDOCODIGO
        suspender_proceso(proceso);
        break;

    case (ACTUALIZAR_PAG): // TOCA MEMORIA DE USUARIO ?
        suspender_proceso(proceso);
        break;

    case (MEMORY_DUMP): // TOCA UN ARCHIVO PROPIO Y EL VALOR DE MEMORIA
        FILE * dump;
        char* titulo = DUMP_PATH;
        titulo[string_length(titulo)+1]=string_itoa(pro[1]);
        if (dump=fopen(titulo,"w")){

        }
        free (titulo);
        break;
    }*/

    // Permitimos el paso de la siguiente instruccion
    sem_post(consultar_memoria);
    return;
}

int str_to_int(char * txt, int ac){
    int num=0,i=ac;

    while (txt[i]!=' ')
    {
        num=num*10;
        num+=txt[i]-'0';
        printf("%d",num);
        i++;
    }

    return num;
}

void leer_pseudo(){
    if( (FILE_PSEUDOCODIGO=fopen(DIR_PSEUDOCODIGO,"r")) !=NULL){ // VER COMO ENVIAR MENOS DE 255
        char cont [100];
        //int p,s;
        while (fgets(cont,100,FILE_PSEUDOCODIGO)){
            /*struct tarea tarea_act;
            
            p=cont[0]-'0';
            s=cont[1]-'0';
            printf("%d",p);
            switch (p)
            {
            case 30: // NOOP
                tarea_act.id= 0;
                break;
            case 39: // WRITE
                tarea_act.id= 1;
                tarea_act.num1 = str_to_int(cont,6);
                printf("%d",tarea_act.num1);
                break;
            case 34: // READ
                
                break;
            case 23: // GOTO
               
                break;
            case 25:
                if (s== 31){}// IO
                else{}                   // INIT
                break;
            case 20: // DUMP MEMORY
                
                break;
            case 21: // EXIT
               
                break;
            default:
                break;
            }*/
            printf("%s\n",cont);  // BIEN
            char * guarda = malloc(sizeof(char) * 101);
            memcpy(guarda,cont,sizeof(char)*100); //memcpy
            memcpy(guarda+100,"\0",sizeof(char));
            printf("copia %s\n",guarda);



            list_add(lista_procesos,guarda);
        }
    }
    fclose(FILE_PSEUDOCODIGO);
    return;
}
//mmap

void enviar_lista(){
    for (int i = 0; i < list_size(lista_procesos); i++)
    {
        char mensaje[101];
        char *posi=list_get(lista_procesos,i);
        for (int j=0; j < 101; j++)
        {
           mensaje[j]=(posi+j);
        }
        
        printf("%s\n",mensaje);  // IMPRIME BASURA
    }
    
    return;
}

void* gestion_conexiones(){
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
}

int main(int argc, char* argv[]) {

    // Crea un hilo que carga las variables globales. El sistema debe esperar que termine
    consultar_memoria=sem_open("SEM_MOD_MEMO", O_CREAT | O_EXCL, S_IRUSR | S_IRUSR, 0); 

    // Cargamos las variables globales
    iniciar_config();
    TAM_MEMORIA_ACTUAL=TAM_MEMORIA;
    lista_procesos=list_create();

    // Creamos el log de memoria
    log_memo = log_create("memoria.log", "memoria", false, LOG_LEVEL);


    leer_pseudo(lista_procesos);
    enviar_lista(lista_procesos);
    //log_info(log_info,("tiene %d",list_size(lista_procesos)));   // NO TIENE NADA?
    //log_info(log_memo,list_remove(lista_procesos,0));




    // Creamos el hilo que crea el servidor
    pthread_t servidor;
    pthread_create(&servidor,NULL,gestion_conexiones,NULL);
    // Esperamos a que el hilo termine, aunque nunca lo haga
    pthread_join(servidor,NULL);

    // Limpieza general, que no realiza
    config_destroy(nuevo_conf);
    close(socket_conectado);
    log_destroy(log_memo);
    list_destroy_and_destroy_elements(lista_procesos,NULL);
    return 0;
}