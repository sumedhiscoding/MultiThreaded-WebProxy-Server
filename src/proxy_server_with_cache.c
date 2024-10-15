#include "proxy_parse.h";
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <netinet/in.h> //Internet domain sockets
#include <sys/socket.h>

#define MAX_CLIENTS 10
#define MAX_BYTES 10*(1<<10)   // 10 * 2^10
struct cache_element
{
    char *data;
    int length;
    char *url;
    time_t lru_time_track; // time based lru cache : (oldest will get deleted first)
    struct cache_element *next;
};

struct cache_element *find(char *url);
int add_cache_element(char *data, int size, char *url);
void remove_cache_element();

int port = 8080;

int proxy_socket_id;

pthread_t tid[MAX_CLIENTS];

sem_t semaphore;
pthread_mutex_t lock;

struct cache_element *head;
int cache_size;

void *thread_fn(void *socketNew)
{
    sem_wait(&semaphore); // semaphore -1 and checks if the value is -1 if -1 then waits otherwise does -1
    int p;
    sem_getvalue(&semaphore, (int* )&p);
    printf("semaphore value %d\n", p);

    int *t =( int *) socketNew;
    int socket = *t ;
    int bytes_send_client,len;

    
    //creating a buffer and initializing it
    char* buffer =( char*) calloc(MAX_BYTES,sizeof(char));
    bzero(buffer,MAX_BYTES); //initialising it
    bytes_send_client=recv(socket,buffer,MAX_BYTES,0);

    while(bytes_send_client > 0){
        len=strlen(buffer);

        if(strstr(buffer, "\r\n\r\n") == NULL){
            bytes_send_client = recv(socket, buffer+len, MAX_BYTES-len, 0);
        }
        
    }    
    


}

int main(int argc, char *argv[])
{
    int client_socket_id, client_length;
    struct sockaddr_in server_addr, client_addr;
    sem_init(&semaphore, 0, MAX_CLIENTS);
    /*
         Here we initialize the value of semaphores to 10 ,
         middle argument is "The pshared argument indicates whether this semaphore is to be shared between the threads of a process, or between processes."
        .If you pass it 0 you will get a semaphore that can be accessed by other threads in the same
        process -- essentially an in-process lock. You can use this as a mutex, or you can use it more
        generally for the resource-counting properties of a semaphore. Arguably if pthreads supported
        a semaphore API you wouldn't need this feature of sem_init, but semaphores in Unix
        precede pthreads by quite a bit of time.
        It would be better if the boolean was some kind of enumeration
        (e.g. SEM_PROCESS_PRIVATE vs SEM_PROCESS_SHARED), because then you wouldn't have had this question,
        but POSIX semaphores are a fairly old API as these things go.

    */

    //    Lets check the number of arguments
    if (argc == 2)
    {
        port = atoi(argv[1]);
    }
    else
    {
        printf("Too few arguments\n");
        exit(1);
    }

    printf("Starting proxy server at port %d\n", port);

    proxy_socket_id = socket(AF_INET, SOCK_STREAM, 0);
    if (proxy_socket_id < 0)
    {
        perror("Error creating socket\n");
        exit(1);
    }

    int reuse = 1;
    // 3rd argument : reuse the same socket if same url aaya
    if (setsockopt(proxy_socket_id, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) < 0)
    {
        perror("setSockOpt failed\n");
    }

    bzero((char *)&server_addr, size_of(server_addr)); // cleans the server_addr

    server_addr.sin_family = AF_INET;   // ipv4 address use
    server_addr.sin_port = htons(port); // port number
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(proxy_socket_id, (struct sockaddr *)&server_addr, sizeof(server_addr) < 0))
    {
        perror("Error binding socket, Port is not available \n");
        exit(1);
    }

    printf("binding on port %d\n", port);

    int listen_status = listen(proxy_socket_id, MAX_CLIENTS);
    if (listen_status < 0)
    {
        perror("Error listening on socket\n");
        exit(1);
    }

    int i = 0;
    int Connected_socketId[MAX_CLIENTS];

    while (1)
    {
        bzero((char *)&client_addr, sizeof(client_addr));
        client_length = sizeof(client_addr);
        client_socket_id = accept(proxy_socket_id, (struct sockaddr *)&client_addr, (socklen_t *)&client_length);

        if (client_socket_id < 0)
        {
            perror("Error connecting to client socket\n");
        }
        else
        {
            Connected_socketId[i] = client_socket_id;
        }

        struct sockaddr_in *client_ptr = (struct sockaddr_in *)&client_addr;

        // getting the client ip address
        struct in_addr ip_addr = client_ptr->sin_addr;

        char str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ip_addr, str, INET_ADDRSTRLEN);

        printf("Client connected from %s:%d\n ", str, ntohs(client_ptr->sin_port));

        pthread_create(&tid[i], NULL, thread_fn, (void *)&Connected_socketId);
        i++;
    }

    close(proxy_socket_id);
    return 0;
}
