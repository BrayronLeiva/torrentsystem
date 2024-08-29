#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <limits.h>  // Incluye PATH_MAX
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>

#define PORT 12345
#define LISTEN_PORT 12346
//#define SERVER_IP "192.168.100.19"  // Cambiar a la IP del Nodo A si no es localhost
#define SERVER_IP "127.0.0.1"
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 10  // Por ejemplo, 10 clientes

//STRUCTS
typedef struct {
    char *filename;
    char *filepath;
    long long int file_size;
    long long int hashR;
    char ipPlace[INET_ADDRSTRLEN];
    int portPlace;
} FileStruct;

typedef struct {
    int socket;
    int part;
} DistriInfo;

char myIp[INET_ADDRSTRLEN];
char directoryWork[BUFFER_SIZE]; //para buscar
//para transferencia distribuida
char client_ip[MAX_CLIENTS][INET_ADDRSTRLEN];
int client_port[MAX_CLIENTS];
size_t clienteCountToSend = 0;
//TOOLS
FileStruct *file_array = NULL;  // Arreglo global de archivos de lo que encuentro para solcitar - tiene avance distribuido
size_t file_count = 0;    // Contador global de archivos

FileStruct *my_file_array = NULL;  // Arreglo global de archivos de lo que encuentro para solcitar - tiene avance distribuido
size_t my_file_count = 0;

char directory[PATH_MAX];//directoria elejido
char fileChosed[BUFFER_SIZE];//nombre archivo parcial

char buffers[MAX_CLIENTS][BUFFER_SIZE];//??
FILE* part_files[MAX_CLIENTS];//sobrescrito directo
int read_sizes[MAX_CLIENTS];//sobrescrito directo
long filesizes[MAX_CLIENTS];//sobrescrito directo
long bytesRecibidos[MAX_CLIENTS];//se vacia
long bytesToWait[MAX_CLIENTS];//sobrescrito directo
FILE* metadata_files_received[MAX_CLIENTS];//sobrescrito directo


void init_archivo_armado(int num_parts){
    // Send metadata
    char enumera[BUFFER_SIZE];
    FILE *metadata_file = fopen("metadata.txt", "wb");
    fprintf(metadata_file, "%d\n", num_parts);
    for (int i = 0; i < num_parts; ++i) {
        snprintf(enumera, sizeof(enumera), "%d_", i);
        fprintf(metadata_file, "%s%ld\n", enumera,filesizes[i]);
    }
    fclose(metadata_file);

}

void dividir_archivo(const char* filepath, int num_parts) {
    FILE *file = fopen(filepath, "rb");
    if (file == NULL) {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }


    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    long part_size = file_size / num_parts;
    long remaining_size = file_size % num_parts;
    char buffer[BUFFER_SIZE];

    // Obtener el nombre del archivo sin la ruta completa
    const char *filename = strrchr(filepath, '/');
    if (filename == NULL) {
        filename = filepath;
    } else {
        filename++; // Avanzar el puntero para omitir el '/'
    }

    // Crear la carpeta "Archivos Divididos" si no existe
    const char *base_folder = "Archivos_Divididos";
    struct stat st0 = {0};
    if (stat(base_folder, &st0) == -1) {
        mkdir(base_folder, 0700);
    }

    char foldername[BUFFER_SIZE];
    snprintf(foldername, sizeof(foldername), "./%s/%s_parts", base_folder, filename);
    struct stat st1 = {0};
    if (stat(foldername, &st1) == -1) {
        mkdir(foldername, 0700);
    }

    for (int i = 0; i < num_parts; ++i) {
        char part_filename[BUFFER_SIZE];

        snprintf(part_filename, sizeof(part_filename), "%s/part_%d", foldername, i);
        //sprintf(part_filename, "part_%c", 'a' + i);
        FILE *part_file = fopen(part_filename, "wb");

        if (part_file == NULL) {
            perror("Error opening part file");
            exit(EXIT_FAILURE);
        }

        long current_part_size = part_size + (i < remaining_size ? 1 : 0);
        printf("Size de parte %d segun metodo: %ld", i, current_part_size);
        //filesizes[i] = current_part_size;

        long bytes_written = 0;
        while (bytes_written < current_part_size) {
            size_t bytes_to_read = BUFFER_SIZE;
            if (bytes_written + BUFFER_SIZE > current_part_size) {
                bytes_to_read = current_part_size - bytes_written;
            }
            size_t bytes_read = fread(buffer, sizeof(char), bytes_to_read, file);
            if (bytes_read == 0) break;
            fwrite(buffer, sizeof(char), bytes_read, part_file);
            bytes_written += bytes_read;
        }
        fclose(part_file);
        FILE *part_file2 = fopen(part_filename, "rb");

        if (part_file2 == NULL) {
            perror("Error opening part file");
            exit(EXIT_FAILURE);
        }
        // Obtener el tamaño del archivo de la parte y mostrarlo en consola
        fseek(part_file, 0, SEEK_END);
        long part_file_size = ftell(part_file2);
        fseek(part_file, 0, SEEK_SET);
        filesizes[i] = part_file_size;
        printf("Tamaño del archivo %s: %ld bytes\n", part_filename, part_file_size);
        fclose(part_file2);
    }

    fclose(file);
    printf("\nSe divio el archivo correctamente\n");
    init_archivo_armado(num_parts);
}

void init_vector_bytes_recibidos(){
    for (int i = 0; i < MAX_CLIENTS; i++) {
        //printf("Vaciando: %d", i);
        bytesRecibidos[i] = 0;
    }
}

void vaciar_arreglo_bytes_recibidos(){
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        bytesRecibidos[i] = 0;
        //printf("Vaciando arreglo de bytes recibidos en pos %d", i);
    }

}

void vaciar_arreglos_ip_ports() {
    int i;
    // Vaciar arreglo de direcciones IP
    for (i = 0; i < MAX_CLIENTS; ++i) {
        strcpy(client_ip[i], "");  // Copia una cadena vacía ("") en cada elemento
    }
    // Vaciar arreglo de puertos
    for (i = 0; i < MAX_CLIENTS; ++i) {
        client_port[i] = 0;  // Asigna 0 a cada elemento del arreglo de puertos
    }
    clienteCountToSend = 0;
}

void liberarFileStruct(FileStruct *file) {
    free(file->filename);
    // Liberar otros campos si tienen memoria dinámica
}

void vaciarFileArray(FileStruct **file_array, size_t *file_count) {
    if (*file_array != NULL) {
        for (size_t i = 0; i < *file_count; i++) {
            liberarFileStruct(&(*file_array)[i]);
        }
        free(*file_array);
        *file_array = NULL;
        *file_count = 0;
    }
    if (file_array == NULL && file_count == 0) {
        printf("\nEl arreglo ha sido vaciado correctamente\n");
    }
}

FileStruct* copiarFileArray() {
    FileStruct* res_array = malloc(file_count * sizeof(FileStruct));
    if (res_array == NULL) {
        perror("Error al asignar memoria");
        exit(EXIT_FAILURE);
    }

    for (size_t i = 0; i < file_count; i++) {
        printf("Copiando vector copia - valor de i %zu", i);
        res_array[i].filename = strdup(file_array[i].filename);
        res_array[i].filepath = strdup(file_array[i].filepath);
        res_array[i].file_size = file_array[i].file_size;
        res_array[i].hashR = file_array[i].hashR;
    }

    return res_array;
}

FileStruct* getVectorOpcionesBusquedaParcial(size_t* count){
    size_t countR = file_count;
    FileStruct* op_array = copiarFileArray();
    printf("El valor de count copia es: %zu\n", countR);

    FileStruct* new_array = malloc(countR * sizeof(FileStruct));
    size_t new_count = 0;

    for (size_t i = 0; i < countR; i++) {
        int is_duplicate = 0;
        for (size_t j = 0; j < new_count; j++) {
            if (op_array[i].hashR == new_array[j].hashR) {
                is_duplicate = 1;
                break;
            }
        }
        if (!is_duplicate) {
            new_array[new_count] = op_array[i];
            new_count++;
        } else {
            free(op_array[i].filename);
            free(op_array[i].filepath);
        }
    }

    free(op_array);

    *count = new_count;
    return realloc(new_array, new_count * sizeof(FileStruct));
}

void filtrarPorSeleccion(FileStruct selected) {
    size_t new_count = 0;
    for (size_t i = 0; i < file_count; ++i) {
        if (file_array[i].hashR == selected.hashR) {
            if (i != new_count) {
                file_array[new_count] = file_array[i];
            }
            new_count++;
        } else {
            liberarFileStruct(&file_array[i]);
        }
    }

    if (new_count != file_count) {
        FileStruct* temp = realloc(file_array, new_count * sizeof(FileStruct));
        if (temp == NULL && new_count > 0) {
            perror("Error al reasignar memoria");
            exit(EXIT_FAILURE);
        }
        file_array = temp;
    }

    file_count = new_count;
}

void filtrarBusquedaParcialPorNombre(){
    //FileStruct *file_array = NULL;  // Arreglo global de archivos de lo que encuentro para solcitar - tiene avance distribuido
    char* op[BUFFER_SIZE];
    size_t op_count = 0;    // Contador global de archivos
    FileStruct* op_array = getVectorOpcionesBusquedaParcial(&op_count);
    printf("\nSe encontraron varias concidencias\n");
    // Imprimir el valor de op_count
    printf("El valor de opciones a elejir es: %zu\n", op_count);
    printf("\nSeleccione el archivo que desea\n");
    if (op_count>1) {
        for (int i = 0; i < op_count; ++i) {
            printf("%d - %s\n", i, op_array[i].filename);
        }
        if (fgets(op, sizeof(op), stdin) == NULL) {
            perror("Error leyendo la entrada");
            return;
        }
        // Convertir la entrada a un entero
        char *endptr;
        int index = strtol(op, &endptr, 10);
        if (endptr == op || index < 0 || index >= (int) op_count) {
            printf("Selección inválida.\n");
            return;
        }
        filtrarPorSeleccion(op_array[index]);
    }else{
        printf("\nNo hay varios archivos parciales encontrados\n");
    }


}

void prepareVectoresDeIpsPuertos(){
    for (int i = 0; i < file_count; i++) {
        strncpy(client_ip[i], file_array[i].ipPlace, INET_ADDRSTRLEN);
        client_port[i] = file_array[i].portPlace;
        clienteCountToSend++;
    }
}

void imprimirVectorDeIpsPuertos(){
    for (int i = 0; i < file_count; i++) {
        printf("\n-VECTOR DE IPS Y PUERTOS\n");
        printf("\n-IP: %s \n", client_ip[i]);
        printf("\n-PUERTO: %d \n", client_port[i]);
    }
}

void getLocalIpAndPort(int sockfd, char *ip, int *port) {
    struct sockaddr_in local_address;
    socklen_t address_length = sizeof(local_address);
    getsockname(sockfd, (struct sockaddr*)&local_address, &address_length);
    inet_ntop(AF_INET, &local_address.sin_addr, ip, INET_ADDRSTRLEN);
    *port = ntohs(local_address.sin_port);
}

int isHashQuery(char* filename){
    int by_hash;
    // Determinar si la consulta es por hash o por nombre de archivo
    for (int i = 0; filename[i] != '\0'; ++i) {
        if ((filename[i] < '0' || filename[i] > '9') && filename[i] != '-') {
            by_hash = 0;
            break;
        }
        by_hash = 1;
    }
    return by_hash;
}

char* extraer_filename(const char* path) {
    // Encontrar el último '/' en la ruta
    const char *last_slash = strrchr(path, '/');
    if (last_slash == NULL) {
        // No se encontró '/', devolver una copia de la cadena completa
        return strdup(path);
    } else {
        // Devolver una copia de la parte después del último '/'
        return strdup(last_slash + 1);
    }
}

char* longlong_to_string(long long int valor) {
    // Determinar el tamaño necesario para la cadena
    int longitud = snprintf(NULL, 0, "%lld", valor);

    // Asignar memoria para la cadena
    char *cadena = (char *)malloc(longitud + 1);  // +1 para el terminador nulo
    if (cadena == NULL) {
        perror("Error de memoria");
        return NULL;
    }

    // Convertir el valor a cadena
    snprintf(cadena, longitud + 1, "%lld", valor);

    return cadena;
}

long long int string_to_longlong(char *str) {
    char *endptr; // Puntero para detectar caracteres no numéricos

    // Convertir la cadena a long long int
    long long int num = strtoll(str, &endptr, 10);

    // Verificar errores de conversión
    if ((num == LLONG_MIN || num == LLONG_MAX) && errno == ERANGE) {
        perror("Error: fuera de rango");
        exit(EXIT_FAILURE);
    }

    // Verificar si no se pudo convertir ningún dígito
    if (endptr == str) {
        fprintf(stderr, "Error: no se encontraron dígitos válidos\n");
        exit(EXIT_FAILURE);
    }

    return num;
}

FileStruct search_byhash(long long int target){
    for (size_t i = 0; i < my_file_count; i++) {
        if(my_file_array[i].hashR == target){
            printf("ENCONTRE A: ");
            printf("Archivo %zu:\n", i + 1);
            printf("Nombre: %s\n", my_file_array[i].filename);
            printf("Ruta: %s\n", my_file_array[i].filepath);
            printf("Tamaño: %lld bytes\n", my_file_array[i].file_size);
            printf("Hash: %lld\n", my_file_array[i].hashR);
            printf("\n");
            return my_file_array[i];
        }
    }
    printf("\nNO EXISTE EL ARCHIVO\n");
    FileStruct result;
    char notfound[BUFFER_SIZE];
    strcpy(notfound, "notFound");
    result.filename = notfound;
    return result;
}

FileStruct search_byfilename(char* target){
    for (size_t i = 0; i < my_file_count; i++) {
        if(strcmp(my_file_array[i].filename, target) == 0){
            printf("ENCONTRE A: ");
            printf("Archivo %zu:\n", i + 1);
            printf("Nombre: %s\n", my_file_array[i].filename);
            printf("Ruta: %s\n", my_file_array[i].filepath);
            printf("Tamaño: %lld bytes\n", my_file_array[i].file_size);
            printf("Hash: %lld\n", my_file_array[i].hashR);
            printf("\n");
            return my_file_array[i];
        }
    }
    printf("\nNO EXISTE EL ARCHIVO\n");
    FileStruct result;
    char notfound[BUFFER_SIZE];
    strcpy(notfound, "notFound");
    result.filename = notfound;
    return result;
}

long long int makeHash(char* s, long long int len, int primo1, int primo2) {
    long long int h = 0;
    int primoActual = 1;
    for (long long int i = 0; i < len; i++) {
        h += s[i] * primoActual;
        primoActual *= primo1;
        primoActual %= primo2;
        h %= primo2;
    }
    return h;
}

void formaterGetIpandPort(char* str, char* ip, int* port){
    // Hacer una copia de la cadena de entrada porque strtok modifica la cadena original
    char str_copy[PATH_MAX];
    strncpy(str_copy, str, sizeof(str_copy));
    str_copy[sizeof(str_copy) - 1] = '\0'; // Asegurar la terminación nula

    // Encuentra la última ocurrencia de '/' para aislar la parte relevante
    char *last_slash = strrchr(str_copy, '/');
    if (last_slash == NULL) {
        // No se encontró '/', la entrada no es válida
        fprintf(stderr, "Formato de entrada no válido\n");
        return;
    }

    // Avanza el puntero después del último '/'
    last_slash++;

    // Encuentra la primera ocurrencia de '_' en la parte relevante
    char *underscore = strchr(last_slash, '_');
    if (underscore == NULL) {
        // No se encontró '_', la entrada no es válida
        fprintf(stderr, "Formato de entrada no válido\n");
        return;
    }

    // Divide la parte relevante en IP y puerto
    *underscore = '\0';
    const char *ip_str = last_slash;
    const char *port_str = underscore + 1;

    // Validar y copiar la IP
    if (inet_pton(AF_INET, ip_str, ip) == 1) {
        strncpy(ip, ip_str, INET_ADDRSTRLEN);
        ip[INET_ADDRSTRLEN - 1] = '\0'; // Asegurar la terminación nula
    } else {
        // IP no válida
        fprintf(stderr, "Dirección IP no válida\n");
        return;
    }

    // Convertir y validar el puerto
    *port = atoi(port_str);
    if (*port <= 0) {
        // Puerto no válido
        fprintf(stderr, "Puerto no válido\n");
        return;
    }

    // Imprimir los resultados
    printf("IP: %s\n", ip);
    printf("PUERTO: %d\n", *port);
}

void print_all_files_info() {
    printf("\nDATOS DE VECTOR DE BUSQUEDA\n");
    for (size_t i = 0; i < file_count; i++) {
        printf("Archivo %zu:\n", i + 1);
        printf("Nombre: %s\n", file_array[i].filename);
        printf("Ruta: %s\n", file_array[i].filepath);
        printf("Tamaño: %lld bytes\n", file_array[i].file_size);
        printf("Hash: %lld\n", file_array[i].hashR);
        printf("\n");
    }
}

void my_print_all_files_info() {
    for (size_t i = 0; i < my_file_count; i++) {
        printf("Archivo %zu:\n", i + 1);
        printf("Nombre: %s\n", my_file_array[i].filename);
        printf("Ruta: %s\n", my_file_array[i].filepath);
        printf("Tamaño: %lld bytes\n", my_file_array[i].file_size);
        printf("Hash: %lld\n", my_file_array[i].hashR);
        printf("\n");
    }
}

void shareBinaryFile(void* socket_desc){
    char buffer[BUFFER_SIZE];
    char metadata[BUFFER_SIZE];
    char filename[BUFFER_SIZE];
    char msg[BUFFER_SIZE];
    int read_size;
    int sockfd = *(int*)socket_desc;
    char client_ip[INET_ADDRSTRLEN];
    strcpy(filename, "file_info.bin");

    int client_port;
    getLocalIpAndPort(sockfd, client_ip, &client_port);

    // Formatear nombre del archivo con IP y puerto
    snprintf(metadata, BUFFER_SIZE, "%s_%d_file_info.bin", client_ip, LISTEN_PORT);
    //printf((const char *) client_port);
    //printf("%s", client_ip);
    //printf("%s", metadata);
    //printf("\n");
    send(sockfd, metadata, BUFFER_SIZE, 0);


    FILE *target = fopen(filename, "rb");
    if (target == NULL) {
        perror("Error abriendo el archivo para enviar");
    }
    // Enviar el contenido del archivo
    while (!feof(target)) {
        int bytes_read = fread(buffer, sizeof(char), BUFFER_SIZE, target);
        if (bytes_read > 0) {
            send(sockfd, buffer, bytes_read, 0);
        }
    }
    fclose(target);
    if ((read_size = recv(sockfd, msg, BUFFER_SIZE, 0)) > 0) {
        msg[read_size] = '\0';
        printf("%s", msg);
        printf("\n");
    }

}

void read_file_info(const char *binary_file) {
    FILE *file = fopen(binary_file, "rb");
    if (file == NULL) {
        perror("Error abriendo el archivo binario para leer");
        return;
    }

    size_t count = 0;
    FileStruct *temp_array = NULL;

    while (1) {
        size_t filename_len;
        if (fread(&filename_len, sizeof(size_t), 1, file) != 1) break;

        char *filename = (char *)malloc(filename_len);
        fread(filename, sizeof(char), filename_len, file);

        size_t filepath_len;
        fread(&filepath_len, sizeof(size_t), 1, file);

        char *filepath = (char *)malloc(filepath_len);
        fread(filepath, sizeof(char), filepath_len, file);

        long long int file_size;
        fread(&file_size, sizeof(long long int), 1, file);

        long long int hashR;
        fread(&hashR, sizeof(long long int), 1, file);

        //printf("Archivo: %s\n", filename);
        //printf("Ruta absoluta del archivo: %s\n", filepath);
        //printf("Tamaño del archivo: %lld bytes\n", file_size);
        //printf("El hash del archivo es: %lld\n", hashR);

        FileStruct new_file;
        new_file.filename = filename;
        new_file.filepath = filepath;
        new_file.file_size = file_size;
        new_file.hashR = hashR;

        temp_array = (FileStruct *)realloc(temp_array, (count + 1) * sizeof(FileStruct ));
        temp_array[count] = new_file;
        count++;

        //free(filename);
        //free(filepath);
    }

    fclose(file);

    vaciarFileArray(&my_file_array, &my_file_count);

    my_file_array = temp_array;
    my_file_count = count;

    my_print_all_files_info();
}

void process_file(const char *filepath, const char* fileChosed, FILE *output_file) {
    extraer_filename(filepath);
    if(strstr(extraer_filename(filepath), fileChosed)!=NULL) {
        FILE *file = fopen(filepath, "rb");
        if (file == NULL) {
            perror("Error abriendo el archivo para leer");
            return;
        }

        // Obtener el nombre del archivo desde la ruta
        const char *filename = strrchr(filepath, '/');
        if (filename == NULL) {
            filename = filepath;  // Si no hay '/', el nombre es la ruta completa
        } else {
            filename++;  // Saltar el '/'
        }

        // Leer contenido del archivo y calcular el hash
        fseek(file, 0, SEEK_END);
        long long int file_size = ftell(file);
        fseek(file, 0, SEEK_SET);

        char *file_content = (char *) malloc(file_size);
        if (file_content == NULL) {
            perror("Error de memoria");
            fclose(file);
            return;
        }

        fread(file_content, 1, file_size, file);
        //file_content[file_size] = '\0';

        long long int hashR = makeHash(file_content, file_size, 331, 1000000007);
        //printf("Archivo: %s\n", filename);
        //printf("Ruta absoluta del archivo: %s\n", filepath);
        //printf("Tamaño del archivo: %lld bytes\n", file_size);
        //printf("El hash del archivo es: %lld\n", hashR);

        // Aquí puedes enviar los datos o hacer lo que necesites con ellos
        // Ejemplo:
        // send(sockfd, filepath, BUFFER_SIZE, 0);

        // Guardar la información del archivo en el archivo binario
        // Guardar la información del archivo en el archivo binario
        size_t filename_len = strlen(filename) + 1;
        fwrite(&filename_len, sizeof(size_t), 1, output_file);
        fwrite(filename, sizeof(char), filename_len, output_file);

        size_t filepath_len = strlen(filepath) + 1;
        fwrite(&filepath_len, sizeof(size_t), 1, output_file);
        fwrite(filepath, sizeof(char), filepath_len, output_file);

        fwrite(&file_size, sizeof(long long int), 1, output_file);
        fwrite(&hashR, sizeof(long long int), 1, output_file);


        /////////////////////////////////////////////////////////////

        free(file_content);
        fclose(file);
    }
}

void process_directory(const char *directory, const char* fileChosed, FILE *output_file) {
    DIR *dir;
    struct dirent *entry;
    struct stat statbuf;
    char path[BUFFER_SIZE];

    if ((dir = opendir(directory)) == NULL) {
        perror("Error abriendo el directorio");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);

        // Obtenemos la información del archivo/directorio
        if (stat(path, &statbuf) == -1) {
            perror("Error obteniendo la información del archivo/directorio");
            continue;
        }

        // Si es un directorio, hacemos una llamada recursiva
        if (S_ISDIR(statbuf.st_mode)) {
            // Ignorar los directorios "." y ".."
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            // Llamada recursiva
            process_directory(path, fileChosed, output_file);
        } else {
            // Si es un archivo, procesarlo
            process_file(path, fileChosed, output_file);
        }
    }

    closedir(dir);
}

void listenRequestSimple(int newsockserver){

    FILE *output_file = fopen("file_info.bin", "wb");
    if (output_file == NULL) {
        perror("Error abriendo el archivo binario para escribir");
        return;
    }
    // Procesar todos los archivos en el directorio ingresado
    process_directory(directory, fileChosed, output_file);
    // Cerrar el archivo binario
    fclose(output_file);

    // Leer y mostrar la información guardada en el archivo binario
    //printf("Cargando en my vector la información del archivo binario:\n");
    read_file_info("file_info.bin");

    char buffer[BUFFER_SIZE];
    int read_size;
    char filename[BUFFER_SIZE];
    char partsBuffer[BUFFER_SIZE];
    int parts;
    char msg[BUFFER_SIZE];
    long long hash;

    // Recibir el nombre del archivo
    if ((read_size = recv(newsockserver, filename, BUFFER_SIZE, 0)) > 0) {
        filename[read_size] = '\0';
        //printf("Recibiendo solicitud: %s\n", filename);
        //printf("ES POR HASH\n");
        hash = string_to_longlong(filename);
        //printf("Hash convertido: %lld\n", hash);
        FileStruct resp = search_byhash(hash);

        // Enviar el nombre del archivo
        send(newsockserver, resp.filename, BUFFER_SIZE, 0); //comparto tambien si no se encontro
        if (strcmp(resp.filename, "notFound") != 0) { // si el archivo existe

            FILE *target = fopen(resp.filepath, "rb");
            if (target == NULL) {
                perror("Error abriendo el archivo para enviar");
            }
            // Obtener el tamaño del archivo
            fseek(target, 0, SEEK_END);
            long filesize = ftell(target);
            fseek(target, 0, SEEK_SET); // Volver al principio del archivo

            // Convertir el tamaño del archivo a cadena y almacenarlo en un buffer
            char filesize_str[BUFFER_SIZE];
            snprintf(filesize_str, BUFFER_SIZE, "%ld", filesize);
            // Enviar el tamaño del archivo
            send(newsockserver, filesize_str, sizeof(filesize), 0);


            if ((read_size = recv(newsockserver, msg, BUFFER_SIZE, 0)) > 0) {
                msg[read_size] = '\0';
                printf("%s", msg);
                printf("\n");
            }
            // Enviar el contenido del archivo
            //while (!feof(target)) {
            //printf("\nEnviando el archivo\n");
            //int bytes_read = fread(buffer, sizeof(char), BUFFER_SIZE, target);
            //if (bytes_read > 0) {
            //  send(newsockserver, buffer, bytes_read, 0);
            //}
            //}

            int bytes_read;
            while ((bytes_read = fread(buffer, sizeof(char), BUFFER_SIZE, target)) > 0) {
                if (send(newsockserver, buffer, bytes_read, 0) != bytes_read) {
                    perror("Send failed");
                    exit(EXIT_FAILURE);
                }
            }
            printf("\nEnvie todo el contenido del archivo\n");
            //printf("\nEnvie: %d bytes \n", bytes_read);
            fclose(target);
            printf("\nCerre el archivo esperando respuesta\n");
            if ((read_size = recv(newsockserver, msg, BUFFER_SIZE, 0)) > 0) {
                msg[read_size] = '\0';
                printf("%s", msg);
                printf("\n");
            }
        } else { printf("\nNo se encontro el archivo para enviar\n"); }
        //printf("\nIngrese el nombre del archivo a enviar: \n");
    }

    printf("\nCerrando socketserver: ");
    printf("#: %d", newsockserver);
    close(newsockserver);
    return;
}

void listenRequestDistribuido(int newsockserver){
    //CODIGO PARA ACTUALIZAR INFO INTERNA--------------------------------------
    FILE *output_file = fopen("file_info.bin", "wb");
    if (output_file == NULL) {
        perror("Error abriendo el archivo binario para escribir");
        return;
    }
    process_directory(directory, fileChosed, output_file);
    fclose(output_file);
    read_file_info("file_info.bin");
    //------------------------------------------------------------------------
    char buffer[BUFFER_SIZE];
    char meta_buffer[BUFFER_SIZE];
    int read_size;
    char filename[BUFFER_SIZE];
    char partsBuffer[BUFFER_SIZE];
    char partToSend[BUFFER_SIZE];
    int parts;
    int part = 0;
    char msg[BUFFER_SIZE];
    long long hash;

    // Recibir el nombre del archivo
    if ((read_size = recv(newsockserver, filename, BUFFER_SIZE, 0)) > 0) {
        filename[read_size] = '\0';
        printf("Recibiendo solicitud : %s\n", filename);
        hash = string_to_longlong(filename);
        //printf("Hash convertido: %lld\n", hash);
        FileStruct resp = search_byhash(hash);

        if ((read_size = recv(newsockserver, partsBuffer, BUFFER_SIZE, 0)) > 0) {
            printf("Es un archivo de : %s partes\n", partsBuffer);
            parts = atoi(partsBuffer);
        }

        dividir_archivo(resp.filepath, parts);

        if ((read_size = recv(newsockserver, partToSend, BUFFER_SIZE, 0)) > 0) {
            printf("El numero de parte que me toca es: %s\n", partToSend);
            part = atoi(partToSend);
            part = part;
        }

        char base_folder[] = "Archivos_Divididos";
        char foldername[BUFFER_SIZE];
        char* filecomplete = extraer_filename(resp.filepath);
        snprintf(foldername, sizeof(foldername), "%s/%s/%s_parts",directoryWork, base_folder, filecomplete);

        //printf("\nfolder name %s", foldername);
        char part_filename[BUFFER_SIZE];
        snprintf(part_filename, sizeof(part_filename), "%s/part_%d", foldername, part);


        //printf("\npart file name %s", part_filename);


        //ENVIANDO TAMANO DE LO QUE VOY A ENVIAR --- METADATOS
        /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        FILE *metadata_file = fopen("metadata.txt", "rb");
        if (metadata_file == NULL) {
            perror("Error opening part file");
            exit(EXIT_FAILURE);
        }
        char* metadata_filename[BUFFER_SIZE];
        snprintf(metadata_filename, sizeof(metadata_filename), "%s", "metadata.txt");
        printf("%s", metadata_filename);
        send(newsockserver, metadata_filename, BUFFER_SIZE, 0);
        // Enviar el contenido del archivo
        while (!feof(metadata_file)) {
            int bytes_read2 = fread(meta_buffer, sizeof(char), BUFFER_SIZE, metadata_file);
            if (bytes_read2 > 0) {
                send(newsockserver, meta_buffer, bytes_read2, 0);
            }
        }

        fclose(metadata_file);
        printf("\nTermine de enviar el archivo de metadatos\n");
        if ((read_size = recv(newsockserver, msg, BUFFER_SIZE, 0)) > 0) {
            msg[read_size] = '\0';
            printf("%s", msg);
            printf("\n");
        }
        /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


        // Enviar el contenido del archivo


        FILE *part_file = fopen(part_filename, "rb");
        //FILE *part_file = fopen("/home/brayron/CLionProjects/catalogadorCLient/cmake-build-debug/Archivos_Divididos/musica.mp3_parts/etst.html", "rb");
        if (part_file == NULL) {
            perror("Error opening part file");
            exit(EXIT_FAILURE);
        }
        //while (!feof(part_file)) {
        //int bytes_read = fread(buffer, sizeof(char), BUFFER_SIZE, part_file);
        //if (bytes_read > 0) {
        //send(newsockserver, buffer, bytes_read, 0);
        //}
        //}

        int bytes_read;
        while ((bytes_read = fread(buffer, sizeof(char), BUFFER_SIZE, part_file)) > 0) {
            if (send(newsockserver, buffer, bytes_read, 0) != bytes_read) {
                perror("Send failed");
                exit(EXIT_FAILURE);
            }
        }
        printf("\nEnvie todo el contenido del archivo\n");
        //printf("\nEnvie: %d bytes \n", bytes_read);

        fclose(part_file);
        if ((read_size = recv(newsockserver, msg, BUFFER_SIZE, 0)) > 0) {
            msg[read_size] = '\0';
            printf("%s", msg);
            printf("\n");
        }

    }

    printf("\nCerrando socketserver: ");
    printf("#: %d", newsockserver);
    close(newsockserver);
    return;
}
//IMPORTANT
void makeRequest(int sock, char* arg, int p){
    char msg[BUFFER_SIZE];
    char buffer[BUFFER_SIZE];
    char binaryFileBuffer[BUFFER_SIZE];
    char rfilename[BUFFER_SIZE];
    char size[BUFFER_SIZE];
    char partsBuffer[BUFFER_SIZE];
    char filesize_str[BUFFER_SIZE];
    long filesize;
    int parts;
    int read_size;
    FILE *file;
    // Crear el directorio recived_files si no existe


    struct stat st = {0};
    if (stat("received_files", &st) == -1) {
        mkdir("received_files", 0700);
    }
    // Enviar el nombre del archivo
    send(sock, arg, BUFFER_SIZE, 0);

    //rfilename es por si acaso se encontro el archivo por hash en el otro lado
    if ((read_size = recv(sock, rfilename, BUFFER_SIZE, 0)) > 0) {
        rfilename[read_size] = '\0';
        if (strcmp(rfilename, "notFound") != 0) {
            char filepath[BUFFER_SIZE]; // Definir una variable para la ruta completa del archivo
            sprintf(filepath, "./received_files/%s", rfilename); // Especificar la ruta completa del archivo
            //printf("Archivo: ");
            //printf("%s", rfilename);
            //printf("\n");
            //printf("filepath: ");
            //printf("%s", filepath);
            printf("\n");
            file = fopen(filepath, "wb");
            if (file == NULL) {
                perror("Error abriendo el archivo para escribir");
                close(sock);
                return;
            }
            printf("Recibiendo archivo: %s\n", rfilename);
            if ((read_size = recv(sock, filesize_str, BUFFER_SIZE, 0)) > 0) {
                filesize_str[read_size] = '\0';
                filesize = atol(filesize_str);  // Convertir la cadena a un valor long
                printf("Tamaño del archivo: %ld bytes\n", filesize);
            }

            strcpy(msg, "\nRecibi El Size\n");
            printf("%s", msg);
            send(sock, msg, BUFFER_SIZE, 0);

            // Recibir el contenido del archivo
            //while ((read_size = recv(sock, buffer, BUFFER_SIZE, 0)) > 0) {
            //fwrite(buffer, sizeof(char), read_size, file);
            //if (read_size < BUFFER_SIZE) break; // fin de archivo
            //}

            long bytesR = 0;
            while (bytesR < filesize) {
                printf("\n He recibido %ld bytes\n ", bytesR);

                read_size = recv(sock, buffer, BUFFER_SIZE, 0);
                if (read_size <= 0) break;
                fwrite(buffer, sizeof(char), read_size, file);
                bytesR += read_size;
            }


            fclose(file);
            strcpy(msg, "\nProceso exitoso\n");
            printf("%s", msg);
            send(sock, msg, BUFFER_SIZE, 0);
            printf("Archivo %s recibido y guardado en: %s\n", rfilename, filepath);

        } else { //nose encontro - puede ser que el alojador le haya cambiado el nombre
            printf("\nEl archivo no se encontro para recibir\n");
            //printf("\nSi es por hash se debe realizar la consulta\n");
            //o lo que dije en el audio
        }

    }

}

int connectToPeerSimple(char* ip, int port){
    int sockPeer;
    struct sockaddr_in peer_addr;
    char buffer[BUFFER_SIZE];
    char hash[BUFFER_SIZE];
    // char abs_path[PATH_MAX];
    char size[BUFFER_SIZE];
    char directory[BUFFER_SIZE]; //para buscar
    char filepath[BUFFER_SIZE];
    char typeConnection[BUFFER_SIZE];
    printf("\nEsta es la ip a conectar\n");
    printf("%s", client_ip[0]);
    printf("\nEsta es el puerto a conectar\n");
    printf("El número es: %d\n", client_port[0]);
    printf("\n");


    // Crear socket
    if ((sockPeer = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket creation failed");
        return 999999;
    }

    // Configurar dirección del servidor
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(client_port[0]);
    peer_addr.sin_addr.s_addr = inet_addr(client_ip[0]);

    // Conectar al servidor
    if (connect(sockPeer, (struct sockaddr*)&peer_addr, sizeof(peer_addr)) < 0) {
        perror("Connection failed");
        close(sockPeer);
        return 99999;
    }
    printf("Conectado al servidor\n");

    strcpy(typeConnection, "SIMPLE");
    // Enviar el numero de partes del archivo
    send(sockPeer, typeConnection, BUFFER_SIZE, 0);

    // Crear hilo para recibir archivos
    //pthread_create(&recv_thread, NULL, receiveFiles, (void*)&sockfd);

    // Crear hilo para enviar archivos

    return sockPeer;
    //close(sockPeer);
    //return;
}

// Función que recibe el número de parte y devuelve el tamaño en bytes de esa parte
long get_part_size(const char* metadata_filename, int part_number) {
    metadata_files_received[part_number] = fopen(metadata_filename, "r");
    if (metadata_files_received[part_number] == NULL) {
        perror("Error opening metadata file");
        exit(EXIT_FAILURE);
    }

    int num_parts;
    fscanf(metadata_files_received[part_number], "%d", &num_parts);

    int part_num;
    long part_size;

    for (int i = 0; i < num_parts; ++i) {
        fscanf(metadata_files_received[part_number], "%d_%ld", &part_num, &part_size);
        if (part_num == part_number) {
            fclose(metadata_files_received[part_number]);
            return part_size;
        }
    }

    fclose(metadata_files_received[part_number]);
    fprintf(stderr, "Parte %d no encontrado en metadata\n", part_number);
    exit(EXIT_FAILURE);
}

void* makeRequestDistribuido(void* arg){
    DistriInfo* info = (DistriInfo*)arg;  // Convertir el argumento a DistriInfo*
    int sockPeer = info->socket;
    int part = info->part;
    int buffer_index = info->part;
    char* buffer = buffers[buffer_index];  // Asignar el buffer correspondiente

    char partesBuffer[BUFFER_SIZE];
    char partBuffer[BUFFER_SIZE];
    char nameFilePart[BUFFER_SIZE];
    char msg[BUFFER_SIZE];
    char metadata_filename[BUFFER_SIZE];
    char medata_fileOpen[BUFFER_SIZE];
    int* read_size = &read_sizes[buffer_index]; // Asignar la posición correspondiente en el arreglo
    //long bytes_received = bytesRecibidos[part];


    //PREPARACION DE FOLDERES
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Crear la carpeta "received_files" si no existe
    const char *base_folder = "received_files";
    struct stat st0 = {0};
    if (stat(base_folder, &st0) == -1) {
        mkdir(base_folder, 0700);
    }
    char folderPorPartes[BUFFER_SIZE];
    // Construir la ruta completa para "Por_Partes"
    snprintf(folderPorPartes, sizeof(folderPorPartes), "%s/%s", base_folder, "Por_Partes");
    struct stat st1 = {0};
    if (stat(folderPorPartes, &st1) == -1) {
        mkdir(folderPorPartes, 0700);
    }
    char finalFolder[BUFFER_SIZE];
    snprintf(finalFolder, sizeof(finalFolder), "%s/%s_parts", folderPorPartes, file_array[0].filename);
    struct stat st2 = {0};
    if (stat(finalFolder, &st1) == -1) {
        mkdir(finalFolder, 0700);
    }
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    char typeConnection[BUFFER_SIZE];
    strcpy(typeConnection, "DISTRIBUIDA");
    send(sockPeer, typeConnection, BUFFER_SIZE, 0);

    char *hash = longlong_to_string(file_array[0].hashR);
    send(sockPeer, hash, BUFFER_SIZE, 0); //para todos el archivo es igual, el vector es para ver concidencias

    int n_parts;
    if (clienteCountToSend <= INT_MAX) {  // Verificar que el valor se puede almacenar en un int
        n_parts = (int)clienteCountToSend;
    }
    sprintf(partesBuffer, "%d", n_parts);
    send(sockPeer, partesBuffer, BUFFER_SIZE, 0);

    //comparto el numero de parte que le toca al servidor enviarme
    sprintf(partBuffer, "%d", part);
    send(sockPeer, partBuffer, BUFFER_SIZE, 0);


    //METADATA FILE
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    if ((*read_size = recv(sockPeer, metadata_filename, BUFFER_SIZE, 0)) > 0) {
        metadata_filename[*read_size] = '\0';
        printf("Recibiendo archivo de metadatos: %s\n", metadata_filename);
    }
    snprintf(medata_fileOpen, sizeof(medata_fileOpen), "%s_%d", metadata_filename, part);
    metadata_files_received[part] = fopen(medata_fileOpen, "wb");
    if (metadata_files_received[part]  == NULL) {
        perror("Error abriendo el archivo para escribir");
        return NULL;
    }
    // Recibir el contenido del archivo
    while ((*read_size = recv(sockPeer, buffer, BUFFER_SIZE, 0)) > 0) {
        fwrite(buffer, sizeof(char), *read_size, metadata_files_received[part]);
        if (*read_size < BUFFER_SIZE) break; // fin de archivo
    }
    printf("\nTermine de recibir el archivo de metadatos\n");
    fclose(metadata_files_received[part]);
    strcpy(msg, "\nProceso de metadatos exitoso\n");
    printf("%s", msg);
    send(sockPeer, msg, BUFFER_SIZE, 0);
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////

    // Read metadata

    bytesToWait[part] = get_part_size(medata_fileOpen, part);
    printf("A mi me como %d me toca recibir %ld bytes",part, bytesToWait[part] );



    //RECIBIENDO ARCHIVO/////////////////////////////////////////////////////////////////////////////////////////
    char partFilename[BUFFER_SIZE];
    snprintf(partFilename, sizeof(partFilename), "%s/%d",finalFolder, part);

    part_files[part] = fopen(partFilename, "wb");
    if (part_files[part] == NULL) {
        perror("Error abriendo el archivo para escribir");
        close(sockPeer);
        return NULL;
    }
    printf("\nVoy a escribir en: %s\n", partFilename);


    //while ((*read_size = recv(sockPeer, buffer, BUFFER_SIZE, 0)) > 0) {
    //fwrite(buffer, sizeof(char), *read_size, part_files[part]);
    // if (*read_size < BUFFER_SIZE) break; // fin de archivo
    //}

    //long bytes_received = 0;
    while (bytesRecibidos[part] < bytesToWait[part]) {
        //printf("\n He recibido %ld bytes\n ", bytesRecibidos[part]);

        *read_size = recv(sockPeer, buffer, BUFFER_SIZE, 0);
        if (*read_size <= 0) break;
        fwrite(buffer, sizeof(char), *read_size, part_files[part]);
        bytesRecibidos[part] += *read_size;
    }
    printf("\n He recibido %ld bytes\n ", bytesRecibidos[part]);

    fclose(part_files[part]);
    strcpy(msg, "\nProceso de transferencia de archivo exitoso\n");
    printf("%s desde %d", msg, part);
    send(sockPeer, msg, BUFFER_SIZE, 0);
    printf("\nArchivo recibido y guardado desde %d\n", part);



    return NULL;
}

void reconstruirFile(char* pathFinal){
    char buffer[BUFFER_SIZE];
    int bytes_received;
    FILE *final_file = fopen(pathFinal, "wb");
    printf("\nPARTES DEL ARCHIVO PARA RECONSTRUIR: %zu\n", clienteCountToSend);
    for (int i = 0; i < clienteCountToSend; ++i) {
        char part_filename[BUFFER_SIZE];
        char folderPorPartes[BUFFER_SIZE];
        snprintf(folderPorPartes, sizeof(folderPorPartes), "%s/%s/%s_parts", "received_files", "Por_Partes", file_array[0].filename);
        sprintf(part_filename, "%s/%d",folderPorPartes, i);
        //printf("\nAnalizando digito %d\n", i);
        printf("\nPartfilename: %s\n",part_filename);
        FILE *part_file = fopen(part_filename, "rb");

        if (part_file == NULL) {
            perror("Error opening part file");
            exit(EXIT_FAILURE);
        }

        while ((bytes_received = fread(buffer, sizeof(char), BUFFER_SIZE, part_file)) > 0) {
            fwrite(buffer, sizeof(char), bytes_received, final_file);
        }

        fclose(part_file);
        //remove(part_filename); // Remove temporary part file
    }
    fclose(final_file);
}

void connectToPeersConexionDistribuida(){
    pthread_t servers[MAX_CLIENTS]; // Arreglo de identificadores de hilos
    int sockPeers[MAX_CLIENTS];   // Arreglo de sockets
    DistriInfo distriInfos[MAX_CLIENTS];
    struct sockaddr_in peer_addr;

    for (int i = 0; i < clienteCountToSend; i++) {

        printf("\nEsta es la ip a conectar del cliente: %d\n", i);
        printf("%s", client_ip[i]);
        printf("\nEsta es el puerto a conectar\n: ");
        printf("El número de puerto es: %d\n", client_port[i]);
        printf("\n");

        // Crear socket
        if ((sockPeers[i] = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
            perror("Socket creation failed");
            return;
        }

        // Configurar dirección del servidor
        peer_addr.sin_family = AF_INET;
        peer_addr.sin_port = htons(client_port[i]);
        peer_addr.sin_addr.s_addr = inet_addr(client_ip[i]);

        // Conectar al servidor
        if (connect(sockPeers[i], (struct sockaddr*)&peer_addr, sizeof(peer_addr)) < 0) {
            perror("Connection failed");
            close(sockPeers[i]);
            return;
        }
        printf("\nConectado al servidor para transferencia distribuida\n");
        // Crear y llenar la estructura de datos para pasar al hilo
        DistriInfo* info = (DistriInfo*)malloc(sizeof(DistriInfo));
        if (info == NULL) {
            perror("Malloc failed");
            close(sockPeers[i]);
            continue; // Pasa a la siguiente iteración si falla
        }
        distriInfos[i].socket = sockPeers[i];
        distriInfos[i].part = i;

        // Crear hilo para hacer request a un par
        if (pthread_create(&servers[i], NULL, makeRequestDistribuido, (void*)&distriInfos[i]) != 0) {
            perror("Thread creation failed");
            close(sockPeers[i]);
            free(info);
            continue; // Pasa a la siguiente iteración si falla
        }
    }
    // Esperar a que todos los hilos terminen
    for (int i = 0; i < clienteCountToSend; i++) {
        pthread_join(servers[i], NULL);
        close(sockPeers[i]);
    }
    printf("\nSE HIZO JOIN DE LOS HILOS-CERRANDO TODOS LOS SOCKTES-YA TERMINE\n");


    char pathFinal[BUFFER_SIZE];
    snprintf(pathFinal, sizeof(pathFinal), "%s/%s", "received_files", file_array[0].filename);
    reconstruirFile(pathFinal);
}

//METODOS DE HILOS
void *listenPeersConnections(){
    printf("\nCreando hilo de espera de conexiones de peers\n");
    int sockserver, newsockserver;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size;
    int opt = 1;
    int read_size;
    char typeRequest[BUFFER_SIZE];

    // Crear socket
    if ((sockserver = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket creation failed");
        exit(1);
    }

    if (setsockopt(sockserver, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // Configurar dirección del servidor-cliente
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(LISTEN_PORT);

    // Enlazar socket
    if (bind(sockserver, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(sockserver);
        exit(1);
    }

    // Obtener el puerto asignado
    socklen_t len = sizeof(server_addr);
    getsockname(sockserver, (struct sockaddr *)&server_addr, &len);
    printf("Server listening on port %d\n", ntohs(server_addr.sin_port));

    // Configurar el socket para escuchar conexiones entrantes
    // Escuchar conexiones
    listen(sockserver, 1);

    printf("Esperando conexiones...\n");
    addr_size = sizeof(struct sockaddr_in);
    //newsockfd = accept(sockfd, (struct sockaddr*)&client_addr, &addr_size);

    while ((newsockserver = accept(sockserver, (struct sockaddr*)&client_addr, &addr_size)) > 0) {
        printf("Conexión aceptada\n");
        // Obtener la dirección IP y puerto del servidor-cliente
        char client_ip[INET_ADDRSTRLEN];
        //struct sockaddr_in client_addr;
        addr_size = sizeof(client_addr);
        getpeername(newsockserver, (struct sockaddr *)&client_addr, &addr_size);
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        int client_port = ntohs(client_addr.sin_port);

        printf("Cliente-server IP: %s, Puerto: %d\n", client_ip, client_port);

        if ((read_size = recv(newsockserver, typeRequest, BUFFER_SIZE, 0)) > 0) {
            printf("%s", typeRequest);
        }
        if(strcmp(typeRequest, "SIMPLE") == 0) {
            listenRequestSimple(newsockserver);
        }else if(strcmp(typeRequest, "DISTRIBUIDA") == 0){
            listenRequestDistribuido(newsockserver);
        }
        printf("\nIngrese el nombre o hash del archivo a enviar: \n");
        //exit(EXIT_FAILURE);
    }

    if (newsockserver < 0) {
        perror("Accept failed");
        close(sockserver);
        exit(1);
    }


    close(sockserver);

}

void *listen_binaryfiles(void *socket_desc){
    char buffer[BUFFER_SIZE];
    char buffername[BUFFER_SIZE];
    char savePath[PATH_MAX];
    int read_size;
    int sock = *(int*) socket_desc;
    FILE *binaryFile;
    struct stat st = {0};
    if (stat("binary_files", &st) == -1) {
        mkdir("binary_files", 0700);
    }
    while(1) {
        if ((read_size = recv(sock, buffername, BUFFER_SIZE, 0)) > 0) {
            buffername[read_size] = '\0';
            //printf("%s", buffername);
            //snprintf(savePath, PATH_MAX, "binary_files/%s", buffername);
            binaryFile = fopen(buffername, "wb");
            if (binaryFile == NULL) {
                perror("Error abriendo el archivo para escribir");
                close(sock);
            }
            //printf("Recibiendo archivo: %s\n", buffername);
            // Recibir el contenido del archivo
            while ((read_size = recv(sock, buffer, BUFFER_SIZE, 0)) > 0) {
                fwrite(buffer, sizeof(char), read_size, binaryFile);
                if (read_size < BUFFER_SIZE) break; // fin de archivo
            }
            fclose(binaryFile);
            //printf("Archivo %s recibido y guardado en: %s\n", rfilename, filepath);
            char msg[BUFFER_SIZE];
            strcpy(msg, "\nBinary FileStruct Exitoso\n");
            //printf("%s", msg);
            send(sock, msg, BUFFER_SIZE, 0);
        }
    }
}

void get_ip_address(char *ip_buffer, size_t ip_buffer_size) {
    struct ifaddrs *ifaddr, *ifa;
    int family;

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;

        family = ifa->ifa_addr->sa_family;

        if (family == AF_INET) { // IPv4
            if (strcmp(ifa->ifa_name, "lo") != 0) { // Excluir la interfaz loopback
                struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
                inet_ntop(family, &addr->sin_addr, ip_buffer, ip_buffer_size);
                break;
            }
        }
    }

    freeifaddrs(ifaddr);
}

void extract_ip_port_to_search(const char *filename, char *ip, int *port) {
    // Copiamos el nombre del archivo para trabajar con él
    char *file_copy = strdup(filename);

    // Buscamos el primer guion bajo
    char *underscore = strchr(file_copy, '_');
    if (underscore != NULL) {
        // Terminamos la cadena en el primer guion bajo para obtener la IP
        *underscore = '\0';
        strcpy(ip, file_copy);

        // Avanzamos para encontrar el segundo guion bajo
        char *second_underscore = strchr(underscore + 1, '_');
        if (second_underscore != NULL) {
            // Terminamos la cadena en el segundo guion bajo para obtener el puerto
            *second_underscore = '\0';
            *port = atoi(underscore + 1);
        }
    }

    free(file_copy);
}

int searchInBinaryFileByHash(char *binary_file, long long int hash, char* ip, int *port) {
    FILE *file = fopen(binary_file, "rb");
    if (file == NULL) {
        perror("Error abriendo el archivo binario para leer");
        return 0;
    }
    //printf("\nLeyendo la información del archivo binario:\n");

    char ipToS[INET_ADDRSTRLEN];
    int portToS;
    printf("\nPath A Analizar: %s\n", binary_file);
    printf("\nNombre A Analizar: %s\n", extraer_filename(binary_file));
    extract_ip_port_to_search(extraer_filename(binary_file), ipToS, &portToS);
    //if(strcmp(my_file_array[i].filename, target) == 0){
    printf("\nIP a analizar: %s", ipToS);
    printf("\nMy Ip: %s", myIp);
    printf("\nPuerto A ANALIZAR: %d", portToS);
    printf("\nMy puerto: %d\n", LISTEN_PORT);

    if(strcmp(ipToS, myIp) == 0 && portToS==LISTEN_PORT){
        return 0;//No quiero buscar en mi propio archivo
    }

    //size_t count = 0;
    //FileStruct *temp_array = NULL;

    while (1) {
        size_t filename_len;
        if (fread(&filename_len, sizeof(size_t), 1, file) != 1) break;

        char *filename = (char *)malloc(filename_len);
        fread(filename, sizeof(char), filename_len, file);

        size_t filepath_len;
        fread(&filepath_len, sizeof(size_t), 1, file);

        char *filepath = (char *)malloc(filepath_len);
        fread(filepath, sizeof(char), filepath_len, file);

        long long int file_size;
        fread(&file_size, sizeof(long long int), 1, file);

        long long int hashR;
        fread(&hashR, sizeof(long long int), 1, file);

        //printf("Archivo: %s\n", filename);
        //printf("Ruta absoluta del archivo: %s\n", filepath);
        //printf("Tamaño del archivo: %lld bytes\n", file_size);
        //printf("El hash del archivo es: %lld\n", hashR);


        if(hashR==hash){
            //printf("\nCONCIDENCIA\n");
            char cip[INET_ADDRSTRLEN];
            int cport;
            formaterGetIpandPort(binary_file,cip,&cport);
            // Ejemplo de inicialización

            strcpy(client_ip[clienteCountToSend], cip);
            client_port[clienteCountToSend] = cport;
            clienteCountToSend++;

            FileStruct new_file;
            new_file.filename = filename;
            new_file.filepath = filepath;
            new_file.file_size = file_size;
            new_file.hashR = hashR;

            // Guardar en la posición correspondiente del vector
            if (file_array == NULL) {
                file_array = (FileStruct *)malloc(sizeof(FileStruct));
            } else {
                file_array = (FileStruct *)realloc(file_array, (file_count + 1) * sizeof(FileStruct));
            }
            file_array[file_count] = new_file;
            file_count++;

            fclose(file);
            return 1;
        }

    }
    return 0;//si no lo encontro

}

int searchInBinaryFileByFileName(char *binary_file, char* argname, char* ip, int *port) {
    FILE *file = fopen(binary_file, "rb");
    if (file == NULL) {
        perror("Error abriendo el archivo binario para leer");
        return 0;
    }
    int findSomeThing = 0;

    char ipToS[INET_ADDRSTRLEN];
    int portToS;
    printf("\nPath A Analizar: %s\n", binary_file);
    printf("\nNombre A Analizar: %s\n", extraer_filename(binary_file));
    extract_ip_port_to_search(extraer_filename(binary_file), ipToS, &portToS);
    //if(strcmp(my_file_array[i].filename, target) == 0){
    printf("\nIP a analizar: %s", ipToS);
    printf("\nMy Ip: %s", myIp);
    printf("\nPuerto A ANALIZAR: %d", portToS);
    printf("\nMy puerto: %d\n", LISTEN_PORT);

    if(strcmp(ipToS, myIp) == 0 && portToS==LISTEN_PORT){
        return 0;//No quiero buscar en mi propio archivo
    }

    //printf("\nLeyendo la información del archivo binario:\n");

    //size_t count = 0;
    //FileStruct *temp_array = NULL;

    while (1) {
        size_t filename_len;
        if (fread(&filename_len, sizeof(size_t), 1, file) != 1) break;

        char *filename = (char *)malloc(filename_len);
        fread(filename, sizeof(char), filename_len, file);

        size_t filepath_len;
        fread(&filepath_len, sizeof(size_t), 1, file);

        char *filepath = (char *)malloc(filepath_len);
        fread(filepath, sizeof(char), filepath_len, file);

        long long int file_size;
        fread(&file_size, sizeof(long long int), 1, file);

        long long int hashR;
        fread(&hashR, sizeof(long long int), 1, file);

        //printf("Archivo: %s\n", filename);
        //printf("Ruta absoluta del archivo: %s\n", filepath);
        //printf("Tamaño del archivo: %lld bytes\n", file_size);
        //printf("El hash del archivo es: %lld\n", hashR);


        if (strstr(filename, argname) != NULL) {
            //printf("\nCONCIDENCIA\n");
            char cip[INET_ADDRSTRLEN];
            int cport;
            formaterGetIpandPort(binary_file, cip, &cport);
            // Ejemplo de inicialización
            //if(clienteAdded==0) {

            //strcpy(client_ip[clienteCountToSend], cip);
            //client_port[clienteCountToSend] = cport;
            //clienteCountToSend++;
            //clienteAdded = 1;
            //}
            findSomeThing = 1;

            FileStruct new_file;
            new_file.filename = filename;
            new_file.filepath = filepath;
            new_file.file_size = file_size;
            new_file.hashR = hashR;
            strncpy(new_file.ipPlace, cip, INET_ADDRSTRLEN);
            //printf("Prueba borrar esto: Ip-Address-File: %s\n", new_file.ipPlace);
            new_file.portPlace = cport;

            // Guardar en la posición correspondiente del vector
            if (file_array == NULL) {
                file_array = (FileStruct *)malloc(sizeof(FileStruct));
            } else {
                file_array = (FileStruct *)realloc(file_array, (file_count + 1) * sizeof(FileStruct));
            }
            file_array[file_count] = new_file;
            file_count++;


        }
        //else{free(filename);free(filepath);}

    }
    fclose(file);
    if(findSomeThing==1) {//si encontro 1
        return 1;
    }else {
        return 0;//si no lo encontro
    }
}

void searchFileInBinaryFiles(char* arg){
    long long int hash;
    char client_ip[INET_ADDRSTRLEN];
    int client_port;
    int sock_find;
    // Abrir el directorio "binary_files"
    DIR *dir;
    struct dirent *ent;
    if(isHashQuery(arg)){
        printf("ES POR HASH\n");
        hash = string_to_longlong(arg);
        printf("Hash convertido: %lld\n", hash);
        if ((dir = opendir("binary_files")) != NULL) {
            // Leer archivos en el directorio
            while ((ent = readdir(dir)) != NULL) {
                if (ent->d_type == DT_REG) {  // Si es un archivo regular
                    char filepath[PATH_MAX];
                    snprintf(filepath, PATH_MAX, "binary_files/%s", ent->d_name);
                    //printf("%s", filepath);
                    printf("\nANALIZANDO ARCHIVO\n");
                    printf("%s", filepath);
                    printf("\n-----------------------------------------\n");
                    if(searchInBinaryFileByHash(filepath,hash, client_ip, &client_port)){
                        printf("\nSE ENCONTRO UNA CONCIDENCIA\n");
                    }
                }
            }//FIN WHILE
            imprimirVectorDeIpsPuertos();
            if(file_count>=1) {
                if(file_count==1) {
                    printf("\nVoy a intentar la conexion 1 a 1\n");
                    sock_find = connectToPeerSimple(client_ip, client_port);
                    char *criterio = longlong_to_string(file_array[0].hashR);
                    makeRequest(sock_find,
                                criterio, 1);//se lo mando arg ya que el otro lado lo recibe asi char* aunque sea hash
                    printf("\nCerrando conexion con socket: ");
                    printf(" #: %d\n", sock_find);
                    close(sock_find);
                    //sendSuccesful();
                }else if(file_count>1){
                    printf("\nVoy a intentar la conexion 1 a N\n");
                    connectToPeersConexionDistribuida();
                    //close(sock_find);
                }
            }else{
                printf("\nNo se encontro el archivo buscado\n");
            }
            closedir(dir);
        } else {
            perror("Error al abrir el directorio");
            return;
        }

        //print_all_files_info();
    }else {
        printf("ES POR NOMBRE\n");
        //hash = string_to_longlong(arg);
        printf("Nombre: %s", arg);
        if ((dir = opendir("binary_files")) != NULL) {
            // Leer archivos en el directorio
            while ((ent = readdir(dir)) != NULL) {
                if (ent->d_type == DT_REG) {  // Si es un archivo regular
                    char filepath[PATH_MAX];
                    snprintf(filepath, PATH_MAX, "binary_files/%s", ent->d_name);
                    printf("%s\n", filepath);
                    printf("\nANALIZANDO ARCHIVO\n");
                    printf("%s", filepath);
                    printf("\n-----------------------------------------\n");
                    if(searchInBinaryFileByFileName(filepath,arg, client_ip, &client_port)){
                        printf("\nSE ENCONTRO UNA CONCIDENCIA\n");
                    }
                }
            }// FIN WHILE
            filtrarBusquedaParcialPorNombre();
            prepareVectoresDeIpsPuertos();//esto es debido a que es por nombre
            imprimirVectorDeIpsPuertos();
            if(file_count>=1) {
                if(file_count==1) {
                    printf("\nVoy a intentar la conexion 1 a 1\n");
                    sock_find = connectToPeerSimple(client_ip, client_port);
                    char *criterio = longlong_to_string(file_array[0].hashR);
                    makeRequest(sock_find,criterio, 1);//se lo mando arg ya que el otro lado lo recibe asi char* aunque sea hash
                    printf("\nCerrando conexion con socket: ");
                    printf(" #: %d\n", sock_find);
                    close(sock_find);
                    //sendSuccesful();
                }else if(file_count>1){
                    printf("\nVoy a intentar la conexion 1 a N\n");
                    connectToPeersConexionDistribuida();
                }
            }else{
                printf("\nNo se encontro el archivo buscado\n");
            }
            //sendSuccesful();
            closedir(dir);
        } else {
            perror("Error al abrir el directorio");
            return;
        }

        //print_all_files_info();
    }// else de filenane

}

void *requestFiles(void *socket_desc) {
    int sockfd = *(int*)socket_desc;
    char msg[BUFFER_SIZE];
    char buffer[BUFFER_SIZE];
    char binaryFileBuffer[BUFFER_SIZE];
    char rfilename[BUFFER_SIZE];
    int read_size;
    FILE *file;
    // Crear el directorio recived_files si no existe
    struct stat st = {0};
    if (stat("received_files", &st) == -1) {
        mkdir("received_files", 0700);
    }

    // Enviar archivos
    while (1) {
        char arg[BUFFER_SIZE];
        printf("\nIngrese el nombre o hash del archivo a solicitar: ");
        fgets(arg, BUFFER_SIZE, stdin);
        arg[strcspn(arg, "\n")] = 0;  // Remover el salto de línea
        // Enviar el nombre del archivo
        printf("\nArgumentos a utilizar: ");
        printf("%s", arg);
        printf("\n");
        //send(sockfd, filename, strlen(filename) + 1, 0);
        searchFileInBinaryFiles(arg);
        //rfilename es por si acaso se encontro el archivo por hash en el otro lado
        //hacer conexion con los datos obtenidos
        // Vaciar el arreglo en cierto momento
        vaciarFileArray(&file_array, &file_count);
        vaciar_arreglos_ip_ports();
        vaciar_arreglo_bytes_recibidos();
        // ver que el arreglo está vacío

    }


}

//METODOS COMUNES

int init_catalogador(){

    // Solicitar al usuario ingresar el directorio
    printf("Ingrese el directorio a procesar: ");
    if (fgets(directory, sizeof(directory), stdin) == NULL) {
        perror("Error leyendo la entrada");
        return 1;
    }
    // Remover el salto de línea al final
    directory[strcspn(directory, "\n")] = 0;

    // Solicitar al usuario ingresar el directorio
    printf("Ingrese el nombre parcial a procesar: ");
    if (fgets(fileChosed, sizeof(fileChosed), stdin) == NULL) {
        perror("Error leyendo la entrada");
        return 1;
    }
    // Remover el salto de línea al final
    fileChosed[strcspn(fileChosed, "\n")] = 0;

    // Verificar si el directorio ingresado es válido
    struct stat statbuf;
    if (stat(directory, &statbuf) == -1 || !S_ISDIR(statbuf.st_mode)) {
        perror("Directorio no válido");
        return 1;
    }
    // Abrir el archivo binario para escribir
    FILE *output_file = fopen("file_info.bin", "wb");
    if (output_file == NULL) {
        perror("Error abriendo el archivo binario para escribir");
        return 1;
    }
    // Procesar todos los archivos en el directorio ingresado
    process_directory(directory, fileChosed, output_file);
    // Cerrar el archivo binario
    fclose(output_file);

    // Leer y mostrar la información guardada en el archivo binario
    printf("Cargando en my vector la información del archivo binario:\n");
    read_file_info("file_info.bin");
    return 0;
}

int init_connection(){
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    //pthread_t recv_thread;
    pthread_t listen_peers;
    pthread_t request_thread;
    pthread_t broadcast_binaryfiles;
    char hash[BUFFER_SIZE];
    // char abs_path[PATH_MAX];
    char size[BUFFER_SIZE];
    char filepath[BUFFER_SIZE];

    // Crear socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket creation failed");
        exit(1);
    }
    init_vector_bytes_recibidos();

    // Configurar dirección del servidor
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    // Conectar al servidor
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sockfd);
        exit(1);
    }
    printf("Conectado al servidor\n");

    // Crear hilo para recibir archivos
    //pthread_create(&recv_thread, NULL, receiveFiles, (void*)&sockfd);

    // Crear hilo para enviar archivos
    pthread_create(&listen_peers, NULL, listenPeersConnections, NULL);

    shareBinaryFile((void*)&sockfd);

    pthread_create(&broadcast_binaryfiles, NULL, listen_binaryfiles, (void*)&sockfd);

    pthread_create(&request_thread, NULL, requestFiles, (void*)&sockfd);

    // Obtener el directorio actual
    if (getcwd(directoryWork, sizeof(directoryWork)) == NULL) {
        perror("Error obteniendo el directorio actual");
        return 1;
    }
    get_ip_address(myIp, sizeof (myIp));

    // Esperar a que los hilos terminen (opcional)
    pthread_join(broadcast_binaryfiles, NULL);
    pthread_join(request_thread, NULL);

    close(sockfd);
    return 0;
}


int main() {


    init_catalogador();

    init_connection();


    return 0;
}