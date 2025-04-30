#include <utils/hello.h>
#include <commons/log.h>
#include <commons/config.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <commons/collections/list.h>
#include <commons/string.h>
#include <pthread.h>

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

// Estructuras
struct proceso
{
    int accesoTablaPag;
    int instruccionSolicitada;
    int bajadaSWAP;
    int subidasMemo;
    int cantLecturas;
    int cantEscrituras;
};


// Funciones
/*void* iniciar_config()
{
	t_config* nuevo_conf = config_create("memoria.conf");
	
    // Carga todas las globales del archivo
    PUERTO_ESCUCHA= config_get_int_value(nuevo_conf, "PUERTO_ESCUCHA");
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
    // Libera el char* y destruye la config que ya no sirve.
    free(nivel_log);
    config_destroy(nuevo_conf);

    return;
}*/

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
    return;
}

void* ingresar_conexion(void* conexion)
{
    // Las tareas a ejecutar cuando entra un hilo
    int aux=(int*) conexion;
    recibir_mensaje(aux,log_memo);
    char* mensaje="me llego tu mensaje";
    enviar_mensaje(mensaje,conexion,log_memo);
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
    // VER POR QUE EL PROCESO MATA EL HILO Y NO CONECTA BIEN EL WHILE
    // CONECTA BIEN EN 1 SOLO.
    
    close(socket_escucha);
    return;
}

int main(int argc, char* argv[]) {
    // Crea un hilo que carga las variables globales. El sistema debe esperar que termine
    /*pthread_t configurar;
    pthread_create(&configurar,NULL,iniciar_config,NULL);
    pthread_join(configurar,NULL);*/

    
    iniciar_config();
    log_memo = log_create("memoria.log", "memoria", false, LOG_LEVEL);

    pthread_t servidor;
    pthread_create(&servidor,NULL,gestion_conexiones,NULL);
    pthread_join(servidor,NULL);

    /*socket_conectado = establecer_conexion(socket_escucha, log_memo);
    pthread_t manejo_servidor;
    pthread_create(&manejo_servidor,NULL,ingresar_conexion,(void*) socket_conectado);
    pthread_detach(manejo_servidor);
    pthread_join(manejo_servidor,NULL);*/

    // Limpieza general
    config_destroy(nuevo_conf);
    close(socket_conectado);
    log_destroy(log_memo);
    return 0;
}
