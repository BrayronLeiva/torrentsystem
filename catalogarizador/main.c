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


#define PORT 12345
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 100  // Número máximo de clientes
char directory[PATH_MAX];

//STRUCTS
typedef struct {
    char *filename;
    char *filepath;
    long long int file_size;
    long long int hashR;
} File;
//int search = 0; //Se cambia a 1 cuando se encuentra algo

//TOOLS
File *file_array = NULL;  // Arreglo global de archivos
size_t file_count = 0;    // Contador global de archivos

int clients[MAX_CLIENTS]; // Array para almacenar los descriptores de socket de los clientes
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex para proteger el acceso al array de clientes

void getLocalIpAndPort(int sockfd, char *ip, int *port) {
    struct sockaddr_in local_address;
    socklen_t address_length = sizeof(local_address);
    getsockname(sockfd, (struct sockaddr*)&local_address, &address_length);
    inet_ntop(AF_INET, &local_address.sin_addr, ip, INET_ADDRSTRLEN);
    *port = ntohs(local_address.sin_port);
}

void *client_handler(void *socket_desc);

void print_all_files_info_client() {
    printf("=== Información de todos los archivos ===\n");
    for (size_t i = 0; i < file_count; i++) {
        printf("Nombre: %s\n", file_array[i].filename);
        printf("Ruta: %s\n", file_array[i].filepath);
        printf("Tamaño: %lld bytes\n", file_array[i].file_size);
        printf("Hash: %lld\n", file_array[i].hashR);
        printf("\n");
    }
}

void init_array_sockeys(){
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i] = 0; // 0 significa que el socket no está en uso
    }
}

// Añade un cliente al array
void add_client(int client_sock) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] == 0) {
            clients[i] = client_sock;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Elimina un cliente del array
void remove_client(int client_sock) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] == client_sock) {
            clients[i] = 0;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void receiveClientBinaryFile(void* socket_desc){
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
    if ((read_size = recv(sock, buffername, BUFFER_SIZE, 0)) > 0) {
        buffername[read_size] = '\0';
        //printf("%s", buffername);
        snprintf(savePath, PATH_MAX, "binary_files/%s", buffername);
        binaryFile = fopen(savePath, "wb");
        if (binaryFile == NULL) {
            perror("Error abriendo el archivo para escribir");
            close(sock);
        }
        printf("Recibiendo archivo: %s\n", buffername);
        // Recibir el contenido del archivo
        while ((read_size = recv(sock, buffer, BUFFER_SIZE, 0)) > 0) {
            fwrite(buffer, sizeof(char), read_size, binaryFile);
            if (read_size < BUFFER_SIZE) break; // fin de archivo
        }
        fclose(binaryFile);
        //printf("Archivo %s recibido y guardado en: %s\n", rfilename, filepath);
        char msg[BUFFER_SIZE];
        strcpy(msg, "\nProceso exitoso\n");
        printf("%s", msg);
        send(sock, msg, BUFFER_SIZE, 0);
    }

    char client_ip[INET_ADDRSTRLEN];
    int client_port;
    getLocalIpAndPort(sock, client_ip, &client_port);
    // Formatear nombre del archivo con IP y puerto
    char metadata[BUFFER_SIZE];
    snprintf(metadata, BUFFER_SIZE, "%s_%d_metadata", client_ip, client_port);
    printf("%s", metadata);
    printf("\n");
}

void sendSuccesful(){
    for (int i = 0; i < MAX_CLIENTS; i++) {
        int client_socket = clients[i];
        if (client_socket != 0) {
            char success[BUFFER_SIZE];
            strcpy(success, "successful");
            send(client_socket, success, BUFFER_SIZE, 0);
        } //fin de if
    }//fin de for
}

void sendBinaryFile(char* filepath){
    char buffer[BUFFER_SIZE];
    char msg[BUFFER_SIZE];
    int bytes_read;
    int read_size;
    FILE *target = fopen(filepath, "rb");
    if (target == NULL) {
        perror("Error al abrir el archivo");
        return;
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        int client_socket = clients[i];
        if (client_socket != 0) {
            // Envío del nombre del archivo
            printf("%s", filepath);
            send(client_socket, filepath, BUFFER_SIZE, 0);

            // Envío del archivo en bloques
            while (!feof(target)) {
                bytes_read = fread(buffer, sizeof(char), BUFFER_SIZE, target);
                if (bytes_read > 0) {
                    send(client_socket, buffer, bytes_read, 0);
                }
            }
            // Reiniciar el puntero del archivo para el próximo cliente
            fseek(target, 0, SEEK_SET);
            //envia de mensaje de exito
            if ((read_size = recv(client_socket, msg, BUFFER_SIZE, 0)) > 0) {
                msg[read_size] = '\0';
                printf("%s", msg);
            }
            //fin de while
        } //fin de if
    }//fin de for

    fclose(target);
}

void broadcastBinaryFiles(){
    // Abrir el directorio "binary_files"
    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir("binary_files")) != NULL) {
        // Leer archivos en el directorio
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_type == DT_REG) {  // Si es un archivo regular
                char filepath[PATH_MAX];
                snprintf(filepath, PATH_MAX, "binary_files/%s", ent->d_name);
                printf("%s", filepath);
                sendBinaryFile(filepath);
            }
        }
        //sendSuccesful();
        closedir(dir);
    } else {
        perror("Error al abrir el directorio");
        return;
    }
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

File search_byhash(long long int target){
    for (size_t i = 0; i < file_count; i++) {
        if(file_array[i].hashR == target){
            printf("ENCONTRE A: ");
            printf("Archivo %zu:\n", i + 1);
            printf("Nombre: %s\n", file_array[i].filename);
            printf("Ruta: %s\n", file_array[i].filepath);
            printf("Tamaño: %lld bytes\n", file_array[i].file_size);
            printf("Hash: %lld\n", file_array[i].hashR);
            printf("\n");
            return file_array[i];
        }
    }
    printf("\nNO EXISTE EL ARCHIVO\n");
    File result;
    char notfound[BUFFER_SIZE];
    strcpy(notfound, "notFound");
    result.filename = notfound;

    return result;
}

File search_byfilename(char* target){
    for (size_t i = 0; i < file_count; i++) {
        if(strcmp(file_array[i].filename, target) == 0){
            printf("ENCONTRE A: ");
            printf("Archivo %zu:\n", i + 1);
            printf("Nombre: %s\n", file_array[i].filename);
            printf("Ruta: %s\n", file_array[i].filepath);
            printf("Tamaño: %lld bytes\n", file_array[i].file_size);
            printf("Hash: %lld\n", file_array[i].hashR);
            printf("\n");
            return file_array[i];
        }
    }
    printf("\nNO EXISTE EL ARCHIVO\n");
    File result;
    char notfound[BUFFER_SIZE];
    strcpy(notfound, "notFound");
    result.filename = notfound;

    return result;
}


//METODOS DE HILOS

void *receiveFiles(void *socket_desc) {
    int sock = *(int*)socket_desc;
    char buffer[BUFFER_SIZE];
    char bufferBinaryFile[BUFFER_SIZE];
    int read_size;
    FILE *file;
    FILE *binaryfile;
    char filename[BUFFER_SIZE];
    char filenameReal[BUFFER_SIZE];
    char msgR[BUFFER_SIZE];
    char hashStr[BUFFER_SIZE];
    long long hash;
    char path[BUFFER_SIZE];
    char size[BUFFER_SIZE];
    char binaryFileName[BUFFER_SIZE];
    char binaryFilepath[BUFFER_SIZE]; // Definir una variable para la ruta completa del archivo
    struct stat st = {0};

    while (1) {
        // Recibir el nombre del archivo
        if ((read_size = recv(sock, filename, BUFFER_SIZE, 0)) > 0) {
            filename[read_size] = '\0';
            printf("Recibiendo solicitud: %s\n", filename);
            if(isHashQuery(filename)){
                printf("ES POR HASH\n");
                hash = string_to_longlong(filename);
                printf("Hash convertido: %lld\n", hash);
                File r = search_byhash(hash);

                // Enviar el nombre del archivo
                send(sock, r.filename, strlen(r.filename) + 1, 0);
                if(strcmp(r.filename, "notFound") != 0) { // si el archivo existe

                    FILE *target = fopen(r.filepath, "rb");
                    if (target == NULL) {
                        perror("Error abriendo el archivo para enviar");
                    }
                    // Enviar el contenido del archivo
                    while (!feof(target)) {
                        int bytes_read = fread(buffer, sizeof(char), BUFFER_SIZE, target);
                        if (bytes_read > 0) {
                            send(sock, buffer, bytes_read, 0);
                        }
                    }

                    fclose(target);
                } else {printf("\nNo se encontro el archivo\n");}

            }else{
                printf("ES POR NOMBRE\n");
                File r = search_byfilename(filename);
                // Enviar el nombre del archivo
                send(sock, r.filename, strlen(r.filename) + 1, 0);
                if(strcmp(r.filename, "notFound") != 0) { // si el archivo existe

                    FILE *target = fopen(r.filepath, "rb");
                    if (target == NULL) {
                        perror("Error abriendo el archivo para enviar");
                    }
                    // Enviar el contenido del archivo
                    while (!feof(target)) {
                        int bytes_read = fread(buffer, sizeof(char), BUFFER_SIZE, target);
                        if (bytes_read > 0) {
                            send(sock, buffer, BUFFER_SIZE, 0);
                        }
                    }

                    fclose(target);
                } else { printf("\nNo se encontro el archivo\n");}
            }

            //printf("\nIngrese el nombre del archivo a enviar: \n");
        }
    }

    return NULL;
}

void *sendFiles(void *socket_desc) {
    int newsockfd = *(int *) socket_desc;
    int by_hash;
    char msg[BUFFER_SIZE];
    char buffer[BUFFER_SIZE];
    char rfilename[BUFFER_SIZE];
    int read_size;
    FILE *file;
    //msg[0] = 's';//para solicitar archivo binario
    //msg[strcspn(msg, "\n")] = 0;
    //send(newsockfd, msg, strlen(msg) + 1, 0);
    // Enviar archivos
    // Crear el directorio recived_files si no existe
    struct stat st = {0};
    if (stat("received_files", &st) == -1) {
        mkdir("received_files", 0700);
    }
    while (1) {
        char filename[BUFFER_SIZE];

        //Solicitar al usuario ingresar el nombre del archivo o hash a buscar
        printf("Ingrese el nombre o hash del archivo a buscar: \n");

        fgets(filename, BUFFER_SIZE, stdin);

        filename[strcspn(filename, "\n")] = 0;  // Remover el salto de línea

        //Enviar el nombre del archivo
        send(newsockfd, filename, strlen(filename) + 1, 0);


        //rfilename es por si acaso se encontro el archivo por hash en el otro lado
        if ((read_size = recv(newsockfd, rfilename, BUFFER_SIZE, 0)) > 0) {
            rfilename[read_size] = '\0';

            char filepath[BUFFER_SIZE]; // Definir una variable para la ruta completa del archivo
            sprintf(filepath, "./received_files/%s", rfilename); // Especificar la ruta completa del archivo
            file = fopen(filepath, "wb");
            if (file == NULL) {
                perror("Error abriendo el archivo para escribir");
                close(newsockfd);
                return NULL;
            }
            printf("Recibiendo archivo: %s\n", filename);

            // Recibir el contenido del archivo
            while ((read_size = recv(newsockfd, buffer, BUFFER_SIZE, 0)) > 0) {
                fwrite(buffer, sizeof(char), read_size, file);
                if (read_size < BUFFER_SIZE) break; // fin de archivo
            }
            fclose(file);
            printf("Archivo %s recibido y guardado en: %s\n", filename, filepath);
            printf("\nIngrese el nombre del archivo a enviar: \n");

        }

    }
}

//METODO COMUNES

void print_all_files_info() {
    printf("=== Información de todos los archivos ===\n");
    for (size_t i = 0; i < file_count; i++) {
        printf("Nombre: %s\n", file_array[i].filename);
        printf("Ruta: %s\n", file_array[i].filepath);
        printf("Tamaño: %lld bytes\n", file_array[i].file_size);
        printf("Hash: %lld\n", file_array[i].hashR);
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
    File *temp_array = NULL;

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

        File new_file;
        new_file.filename = filename;
        new_file.filepath = filepath;
        new_file.file_size = file_size;
        new_file.hashR = hashR;

        temp_array = (File *)realloc(temp_array, (count + 1) * sizeof(File));
        temp_array[count] = new_file;
        count++;

        //free(filename);
        //free(filepath);
    }
    fclose(file);

    // Liberar memoria de file_array previo si existe
    if (file_array != NULL) {
        for (size_t i = 0; i < file_count; i++) {
            free(file_array[i].filename);
            free(file_array[i].filepath);
        }
        free(file_array);
    }

    file_array = temp_array;
    file_count = count;

    print_all_files_info();
}

void process_file(const char *filepath, FILE *output_file) {
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

    char *file_content = (char *)malloc(file_size);
    if (file_content == NULL) {
        perror("Error de memoria");
        fclose(file);
        return;
    }

    fread(file_content, 1, file_size, file);
    //file_content[file_size] = '\0';

    long long int hashR = makeHash(file_content,file_size, 331, 1000000007);
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

void process_directory(const char *directory, FILE *output_file) {
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
            process_directory(path, output_file);
        } else {
            // Si es un archivo, procesarlo
            process_file(path, output_file);
        }
    }

    closedir(dir);
}

int init_catalogador(){


    // Solicitar al usuario ingresar el directorio
    printf("Ingrese el directorio a procesar: ");
    if (fgets(directory, sizeof(directory), stdin) == NULL) {
        perror("Error leyendo la entrada");
        return 1;
    }
    // Remover el salto de línea al final
    directory[strcspn(directory, "\n")] = 0;

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
    process_directory(directory, output_file);
    // Cerrar el archivo binario
    fclose(output_file);

    // Leer y mostrar la información guardada en el archivo binario
    printf("Leyendo la información del archivo binario:\n");
    read_file_info("file_info.bin");


    return 0;
}

void *client_handler(void *socket_desc) {
    pthread_t recv_thread, send_thread;
    int sock = *(int*)socket_desc;
    add_client(sock);
    receiveClientBinaryFile((void*)&sock);
    broadcastBinaryFiles();
    //pthread_create(&recv_thread, NULL, receiveFiles, (void*)&sock);
    //pthread_create(&send_thread, NULL, sendFiles, (void*)&sock);

    //pthread_join(recv_thread, NULL);
    //pthread_join(send_thread, NULL);

    return NULL;
}

int init_connection(){
    int sockfd, newsockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size;
    char buffer[BUFFER_SIZE];
    pthread_t recv_thread;
    pthread_t send_thread;
    char hash[BUFFER_SIZE];
    char size[BUFFER_SIZE];
    //char abs_path[PATH_MAX];
    char directory[BUFFER_SIZE]; //para buscar
    char filepath[BUFFER_SIZE];
    pthread_t client_thread;

    // Crear socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket creation failed");
        exit(1);
    }

    // Configurar dirección del servidor
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Enlazar socket
    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(sockfd);
        exit(1);
    }

    // Escuchar conexiones
    listen(sockfd, 1);

    printf("Esperando conexiones...\n");
    addr_size = sizeof(struct sockaddr_in);
    //newsockfd = accept(sockfd, (struct sockaddr*)&client_addr, &addr_size);

    while ((newsockfd = accept(sockfd, (struct sockaddr*)&client_addr, &addr_size)) > 0) {
        printf("Conexión aceptada\n");

        if (pthread_create(&client_thread, NULL, client_handler, (void*)&newsockfd) < 0) {
            perror("Could not create thread");
            close(newsockfd);
        }

        pthread_detach(client_thread); // Para no tener que hacer join de cada hilo
    }

    if (newsockfd < 0) {
        perror("Accept failed");
        close(sockfd);
        exit(1);
    }

    //close(newsockfd);
    close(sockfd);

    // Obtener el directorio actual
    if (getcwd(directory, sizeof(directory)) == NULL) {
        perror("Error obteniendo el directorio actual");
        return 1;
    }
    return 0;
}



int main() {

    //init_catalogador();

    init_connection();


    return 0;
}


