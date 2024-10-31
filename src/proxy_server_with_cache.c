#include "proxy_parse.h"
#include <arpa/inet.h>
#include <netdb.h> //Internet domain sockets
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define MAX_BYTES 4096  // max allowed size of request/response
#define MAX_CLIENTS 400 // max number of client requests served at a time
#define MAX_SIZE 200 * (1 << 20)        // size of the cache
#define MAX_ELEMENT_SIZE 10 * (1 << 20) // max size of an element in cache

typedef struct cache_element cache_element;
struct cache_element {
  char *data;
  int length;
  char *url;
  time_t
      lru_time_track; // time based lru cache : (oldest will get deleted first)
  struct cache_element *next;
};

struct cache_element *find(char *url);
int add_cache_element(char *data, int size, char *url);
void remove_cache_element();

int port = 2000;

int proxy_socket_id;

pthread_t tid[MAX_CLIENTS];

sem_t semaphore;
pthread_mutex_t lock;

struct cache_element *head;
int cache_size;


int connectRemoteServer(char* host_addr, int port_num){
	// Creating Socket for remote server ---------------------------

	int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);

	if( remoteSocket < 0)
	{
		printf("Error in Creating Socket.\n");
		return -1;
	}
	
	// Get host by the name or ip address provided

	struct hostent *host = gethostbyname(host_addr);	
	if(host == NULL)
	{
		fprintf(stderr, "No such host exists.\n");	
		return -1;
	}

	// inserts ip address and port number of host in struct `server_addr`
	struct sockaddr_in server_addr;

	bzero((char*)&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port_num);

	bcopy((char *)host->h_addr,(char *)&server_addr.sin_addr.s_addr,host->h_length);

	// Connect to Remote server ----------------------------------------------------

	if( connect(remoteSocket, (struct sockaddr*)&server_addr, (socklen_t)sizeof(server_addr)) < 0 )
	{
		fprintf(stderr, "Error in connecting !\n"); 
		return -1;
	}
	// free(host_addr);
	return remoteSocket;
}

int sendErrorMessage(int socket, int status_code) {
  char str[1024];
  char currentTime[50];
  time_t now = time(0);

  struct tm data = *gmtime(&now);
  strftime(currentTime, sizeof(currentTime), "%a, %d %b %Y %H:%M:%S %Z", &data);

  switch (status_code) {
  case 400:
    snprintf(str, sizeof(str),
             "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: "
             "keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: "
             "VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>400 Bad "
             "Request</TITLE></HEAD>\n<BODY><H1>400 Bad "
             "Rqeuest</H1>\n</BODY></HTML>",
             currentTime);
    printf("400 Bad Request\n");
    send(socket, str, strlen(str), 0);
    break;

  case 403:
    snprintf(str, sizeof(str),
             "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: "
             "text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: "
             "VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>403 "
             "Forbidden</TITLE></HEAD>\n<BODY><H1>403 "
             "Forbidden</H1><br>Permission Denied\n</BODY></HTML>",
             currentTime);
    printf("403 Forbidden\n");
    send(socket, str, strlen(str), 0);
    break;

  case 404:
    snprintf(
        str, sizeof(str),
        "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: "
        "text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: "
        "VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not "
        "Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>",
        currentTime);
    printf("404 Not Found\n");
    send(socket, str, strlen(str), 0);
    break;

  case 500:
    snprintf(
        str, sizeof(str),
        "HTTP/1.1 500 Internal Server Error\r\nContent-Length: "
        "115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: "
        "%s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>500 Internal "
        "Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server "
        "Error</H1>\n</BODY></HTML>",
        currentTime);
    // printf("500 Internal Server Error\n");
    send(socket, str, strlen(str), 0);
    break;

  case 501:
    snprintf(
        str, sizeof(str),
        "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: "
        "keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: "
        "VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not "
        "Implemented</TITLE></HEAD>\n<BODY><H1>501 Not "
        "Implemented</H1>\n</BODY></HTML>",
        currentTime);
    printf("501 Not Implemented\n");
    send(socket, str, strlen(str), 0);
    break;

  case 505:
    snprintf(
        str, sizeof(str),
        "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: "
        "125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: "
        "%s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>505 HTTP "
        "Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not "
        "Supported</H1>\n</BODY></HTML>",
        currentTime);
    printf("505 HTTP Version Not Supported\n");
    send(socket, str, strlen(str), 0);
    break;

  default:
    return -1;
  }
  return 1;
}

int checkHTTPversion(char *msg) {
  int version = -1;
  if (strcmp(msg, "HTTP/1.0") == 0) {
    version = 1;
  } else if (strcmp(msg, "HTTP/1.1") == 0) {
    version = 1;
  } else {
    version = -1;
  }
  return version;
}

int handle_Request(int client_socket_id, struct ParsedRequest *request,
                   char *tempReq) {
  char *buf = (char *)malloc(MAX_BYTES * sizeof(char));
  strcpy(buf, "GET");
  strcat(buf, request->path);
  strcat(buf, " ");
  strcat(buf, request->version);
  strcat(buf, "\r\n");
  size_t len = strlen(buf);

  if (ParsedHeader_set(request, "Connection", "close") < 0) {
    printf("Set Header key is not working");
  }

  if (ParsedHeader_get(request, "Host") == NULL) {
    if (ParsedHeader_set(request, "Host", request->host) < 0) {
      printf("Set \"Host\" header key not working\n");
    }
  }

  if (ParsedRequest_unparse_headers(request, buf + len,
                                    (size_t)MAX_BYTES - len) < 0) {
    printf("unparsed Failed");
  }

  int server_port = 80;

  if (request->port != NULL) {
    server_port = atoi(request->port);
  }

  int remote_socket_id = connectRemoteServer(request->host, server_port);

  if (remote_socket_id < 0) {
    return -1;
  }

  int byte_sends = send(remote_socket_id, buf, strlen(buf), 0);
  bzero(buf, MAX_BYTES);

  byte_sends = recv(remote_socket_id, buf, MAX_BYTES - 1, 0);

  char *temp_buffer = (char *)malloc(sizeof(char) * MAX_BYTES);

  int temp_buffer_size = MAX_BYTES;
  int temp_buffer_index = 0;

  while (byte_sends > 0) {
    byte_sends = send(client_socket_id, buf, byte_sends, 0);
    for (int i = 0; i < byte_sends / sizeof(char); i++) {
      temp_buffer[temp_buffer_index] = buf[i];
      temp_buffer_index++;
    }

    temp_buffer_size += MAX_BYTES;
    temp_buffer = (char *)realloc(temp_buffer, temp_buffer_size);

    if (byte_sends < 0) {
      perror("error in sending data to the client");
      break;
    }
    bzero(buf, MAX_BYTES);
    byte_sends = recv(remote_socket_id, buf, MAX_BYTES - 1, 0);
  }

  temp_buffer[temp_buffer_index] = '\0';
  free(buf);
  add_cache_element(temp_buffer, strlen(temp_buffer), tempReq);
  printf("Done\n");
  free(temp_buffer);
  close(remote_socket_id);
  return 0;
}

void* thread_fn(void* socketNew)
{
	sem_wait(&semaphore); 
	int p;
	sem_getvalue(&semaphore,&p);
	printf("semaphore value:%d\n",p);
    int* t= (int*)(socketNew);
	int socket=*t;           // Socket is socket descriptor of the connected Client
	int bytes_send_client,len;	  // Bytes Transferred

	
	char *buffer = (char*)calloc(MAX_BYTES,sizeof(char));	// Creating buffer of 4kb for a client
	
	
	bzero(buffer, MAX_BYTES);								// Making buffer zero
	bytes_send_client = recv(socket, buffer, MAX_BYTES, 0); // Receiving the Request of client by proxy server
	
	while(bytes_send_client > 0)
	{
		len = strlen(buffer);
        //loop until u find "\r\n\r\n" in the buffer
		if(strstr(buffer, "\r\n\r\n") == NULL)
		{	
			bytes_send_client = recv(socket, buffer + len, MAX_BYTES - len, 0);
		}
		else{
			break;
		}
	}

	// printf("--------------------------------------------\n");
	// printf("%s\n",buffer);
	// printf("----------------------%d----------------------\n",strlen(buffer));
	
	char *tempReq = (char*)malloc(strlen(buffer)*sizeof(char)+1);
    //tempReq, buffer both store the http request sent by client
	for (int i = 0; i < strlen(buffer); i++)
	{
		tempReq[i] = buffer[i];
	}
	
	//checking for the request in cache 
	struct cache_element* temp = find(tempReq);

	if( temp != NULL){
        //request found in cache, so sending the response to client from proxy's cache
		int size=temp->length/sizeof(char);
		int pos=0;
		char response[MAX_BYTES];
		while(pos<size){
			bzero(response,MAX_BYTES);
			for(int i=0;i<MAX_BYTES;i++){
				response[i]=temp->data[pos];
				pos++;
			}
			send(socket,response,MAX_BYTES,0);
		}
		printf("Data retrived from the Cache\n\n");
		printf("%s\n\n",response);
		// close(socketNew);
		// sem_post(&semaphore);
		// return NULL;
	}
	
	
	else if(bytes_send_client > 0)
	{
		len = strlen(buffer); 
		//Parsing the request
		struct ParsedRequest* request = ParsedRequest_create();
		
        //ParsedRequest_parse returns 0 on success and -1 on failure.On success it stores parsed request in
        // the request
      int parsedRequest=  ParsedRequest_parse(request, buffer, len);
		if ( parsedRequest < 0) 
		{
		   	printf("Parsing failed\n");
		}
		else
		{	
			bzero(buffer, MAX_BYTES);
			if(!strcmp(request->method,"GET"))							
			{
                
				if( request->host && request->path && (checkHTTPversion(request->version) == 1) )
				{
					bytes_send_client = handle_Request(socket, request, tempReq);		// Handle GET request
					if(bytes_send_client == -1)
					{	
						sendErrorMessage(socket, 500);
					}

				}
				else
					sendErrorMessage(socket, 500);			// 500 Internal Error

			}
            else
            {
                printf("This code doesn't support any method other than GET\n");
            }
    
		}
        //freeing up the request pointer
		ParsedRequest_destroy(request);

	}

	else if( bytes_send_client < 0)
	{
		perror("Error in receiving from client.\n");
	}
	else if(bytes_send_client == 0)
	{
		printf("Client disconnected!\n");
	}

	shutdown(socket, SHUT_RDWR);
	close(socket);
	free(buffer);
	sem_post(&semaphore);	
	
	sem_getvalue(&semaphore,&p);
	printf("Semaphore post value:%d\n",p);
	free(tempReq);
	return NULL;
}

int main(int argc, char *argv[]) {

  int client_socketId,
      client_len; // client_socketId == to store the client socket id
  struct sockaddr_in server_addr,
      client_addr; // Address of client and server to be assigned

  sem_init(&semaphore, 0, MAX_CLIENTS); // Initializing semaphore and lock
  pthread_mutex_init(&lock, NULL);      // Initializing lock for cache

  if (argc == 2) // checking whether two arguments are received or not
  {
    port = atoi(argv[1]);
  } else {
    printf("Too few arguments\n");
    exit(1);
  }

  printf("Setting Proxy Server Port : %d\n", port);

  // creating the proxy socket
  proxy_socket_id = socket(AF_INET, SOCK_STREAM, 0);

  if (proxy_socket_id < 0) {
    perror("Failed to create socket.\n");
    exit(1);
  }

  int reuse = 1;
  if (setsockopt(proxy_socket_id, SOL_SOCKET, SO_REUSEADDR,
                 (const char *)&reuse, sizeof(reuse)) < 0)
    perror("setsockopt(SO_REUSEADDR) failed\n");

  bzero((char *)&server_addr, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);       // Assigning port to the Proxy
  server_addr.sin_addr.s_addr = INADDR_ANY; // Any available adress assigned

  // Binding the socket
  if (bind(proxy_socket_id, (struct sockaddr *)&server_addr,
           sizeof(server_addr)) < 0) {
    perror("Port is not free\n");
    exit(1);
  }
  printf("Binding on port: %d\n", port);

  // Proxy socket listening to the requests
  int listen_status = listen(proxy_socket_id, MAX_CLIENTS);

  if (listen_status < 0) {
    perror("Error while Listening !\n");
    exit(1);
  }

  int i = 0; // Iterator for thread_id (tid) and Accepted Client_Socket for each
             // thread
  int Connected_socketId[MAX_CLIENTS]; // This array stores socket descriptors
                                       // of connected clients

  // Infinite Loop for accepting connections
  while (1) {

    bzero((char *)&client_addr,
          sizeof(client_addr)); // Clears struct client_addr
    client_len = sizeof(client_addr);

    // Accepting the connections
    client_socketId = accept(proxy_socket_id, (struct sockaddr *)&client_addr,
                             (socklen_t *)&client_len); // Accepts connection
    if (client_socketId < 0) {
      fprintf(stderr, "Error in Accepting connection !\n");
      exit(1);
    } else {
      Connected_socketId[i] =
          client_socketId; // Storing accepted client into array
    }

    // Getting IP address and port number of client
    struct sockaddr_in *client_pt = (struct sockaddr_in *)&client_addr;
    struct in_addr ip_addr = client_pt->sin_addr;
    char str[INET_ADDRSTRLEN]; // INET_ADDRSTRLEN: Default ip address size
    inet_ntop(AF_INET, &ip_addr, str, INET_ADDRSTRLEN);
    printf("Client is connected with port number: %d and ip address: %s \n",
           ntohs(client_addr.sin_port), str);
    // printf("Socket values of index %d in main function is %d\n",i,
    // client_socketId);
    pthread_create(&tid[i], NULL, thread_fn,
                   (void *)&Connected_socketId[i]); // Creating a thread for
                                                    // each client accepted
    i++;
  }
  close(proxy_socket_id); // Close socket
  return 0;
}

// LRU CACHE FUNCTIONS

struct cache_element *find(char *url) {
  struct cache_element *site = NULL;

  // Acquire the lock
  int lock_result = pthread_mutex_lock(&lock);
  if (lock_result != 0) {
    fprintf(stderr, "Failed to acquire lock: %d\n", lock_result);
    return NULL; // Return NULL if locking fails
  }
  printf("Cache Lock Acquired\n");

  // Traverse the cache
  site = head;
  while (site != NULL) {
    if (site->url != NULL && strcmp(site->url, url) == 0) {
      printf("URL found: %s\n", url);
      printf("LRU Time Track Before: %ld\n", site->lru_time_track);
      site->lru_time_track = time(NULL);
      printf("LRU Time Track After: %ld\n", site->lru_time_track);
      break; // Exit once found
    }
    site = site->next;
  }

  if (site == NULL) {
    printf("URL not found: %s\n", url);
  }

  // Release the lock
  lock_result = pthread_mutex_unlock(&lock);
  if (lock_result != 0) {
    fprintf(stderr, "Failed to release lock: %d\n", lock_result);
  }
  printf("Cache Lock Unlocked\n");
  return site; // Return found cache element or NULL
}

void remove_cache_element() {
  // If cache is not empty searches for the node which has the least
  // lru_time_track and deletes it
  struct cache_element *p;    // Cache_element Pointer (Prev. Pointer)
  struct cache_element *q;    // Cache_element Pointer (Next Pointer)
  struct cache_element *temp; // Cache element to remove
  // sem_wait(&cache_lock);
  int temp_lock_val = pthread_mutex_lock(&lock);
  printf("Remove Cache Lock Acquired %d\n", temp_lock_val);
  if (head != NULL) { // Cache != empty
    for (q = head, p = head, temp = head; q->next != NULL;
         q = q->next) { // Iterate through entire cache and search for oldest
                        // time track
      if (((q->next)->lru_time_track) < (temp->lru_time_track)) {
        temp = q->next;
        p = q;
      }
    }
    if (temp == head) {
      head = head->next; /*Handle the base case*/
    } else {
      p->next = temp->next;
    }
    cache_size = cache_size - (temp->length) - sizeof(struct cache_element) -
                 strlen(temp->url) - 1; // updating the cache size
    free(temp->data);
    free(temp->url); // Free the removed element
    free(temp);
  }
  // sem_post(&cache_lock);
  temp_lock_val = pthread_mutex_unlock(&lock);
  printf("Remove Cache Lock Unlocked %d\n", temp_lock_val);
}

int add_cache_element(char *data, int size, char *url) {
  // Adds element to the cache
  // sem_wait(&cache_lock);
  int temp_lock_val = pthread_mutex_lock(&lock);
  printf("Add Cache Lock Acquired %d\n", temp_lock_val);
  int element_size =
      size + 1 + strlen(url) +
      sizeof(struct cache_element); // Size of the new element which will be
                                    // added to the cache
  if (element_size > MAX_BYTES) {
    // sem_post(&cache_lock);
    // If element size is greater than MAX_ELEMENT_SIZE we don't add the element
    // to the cache
    temp_lock_val = pthread_mutex_unlock(&lock);
    printf("Add Cache Lock Unlocked %d\n", temp_lock_val);
    // free(data);
    // printf("--\n");
    // free(url);
    return 0;
  } else {
    while (cache_size + element_size > MAX_SIZE) {
      // We keep removing elements from cache until we get enough space to add
      // the element
      remove_cache_element();
    }
    struct cache_element *element = (struct cache_element *)malloc(sizeof(
        struct cache_element)); // Allocating memory for the new cache element
    element->data =
        (char *)malloc(size + 1); // Allocating memory for the response to be
                                  // stored in the cache element
    strcpy(element->data, data);
    element->url = (char *)malloc(
        1 + (strlen(url) *
             sizeof(char))); // Allocating memory for the request to be stored
                             // in the cache element (as a key)
    strcpy(element->url, url);
    element->lru_time_track = time(NULL); // Updating the time_track
    element->next = head;
    element->length = size;
    head = element;
    cache_size += element_size;
    temp_lock_val = pthread_mutex_unlock(&lock);
    printf("Add Cache Lock Unlocked %d\n", temp_lock_val);
    // sem_post(&cache_lock);
    //  free(data);
    //  printf("--\n");
    //  free(url);
    return 1;
  }
  return 0;
}
