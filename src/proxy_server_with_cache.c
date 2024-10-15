#include "proxy_parse.h"
#include <arpa/inet.h>
#include <ctype.h>
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

#define MAX_CLIENTS 10
#define MAX_BYTES 10 * (1 << 10) // 10 * 2^10
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

int port = 8080;

int proxy_socket_id;

pthread_t tid[MAX_CLIENTS];

sem_t semaphore;
pthread_mutex_t lock;

struct cache_element *head;
int cache_size;

int connectRemoteServer(char *host_addr, int port_num) {
  int remote_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (remote_socket < 0) {
    printf("error in creating socket\n");
    return -1;
  }

  struct hostent *host = gethostbyname(host_addr);
  if (host == NULL) {
    perror("No such host exists");
    return -1;
  }

  struct sockaddr_in server_addr;
  bzero((char *)&server_addr, sizeof(server_addr));

  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port_num);

  bcopy((char *)host->h_addr, (char *)&server_addr.sin_addr.s_addr,
        host->h_length);

  if (connect(remote_socket, (struct sockaddr *)&server_addr,
              sizeof(server_addr)) < 0) {
    perror("Errror in connecting \n");
    return -1;
  }

  return remote_socket;
}


int sendErrorMessage(int socket, int status_code)
{
	char str[1024];
	char currentTime[50];
	time_t now = time(0);

	struct tm data = *gmtime(&now);
	strftime(currentTime,sizeof(currentTime),"%a, %d %b %Y %H:%M:%S %Z", &data);

	switch(status_code)
	{
		case 400: snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Rqeuest</H1>\n</BODY></HTML>", currentTime);
				  printf("400 Bad Request\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 403: snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
				  printf("403 Forbidden\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 404: snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
				  printf("404 Not Found\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 500: snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
				  //printf("500 Internal Server Error\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 501: snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
				  printf("501 Not Implemented\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 505: snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
				  printf("505 HTTP Version Not Supported\n");
				  send(socket, str, strlen(str), 0);
				  break;

		default:  return -1;

	}
	return 1;
}

int checkHTTPversion(char *msg) {
  int version = -1;
  if (strcmp(msg, "HTTP/1.0") == 0) {
    version=1;
  } else if (strcmp(msg, "HTTP/1.1") == 0) {
    version=1;
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
      printf("Set Host Header Key is not working");
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

  free(temp_buffer);
  close(remote_socket_id);
  return 0;
}

void *thread_fn(void *socketNew) {
  sem_wait(&semaphore); // semaphore -1 and checks if the value is -1 if -1 then
                        // waits otherwise does -1
  int p;
  sem_getvalue(&semaphore, (int *)&p);
  printf("semaphore value %d\n", p);

  int *t = (int *)socketNew;
  int socket = *t;
  int bytes_send_client, len;

  // creating a buffer and initializing it
  char *buffer = (char *)calloc(MAX_BYTES, sizeof(char));
  bzero(buffer, MAX_BYTES); // initialising it
  bytes_send_client = recv(socket, buffer, MAX_BYTES, 0);

  while (bytes_send_client > 0) {
    len = strlen(buffer);

    if (strstr(buffer, "\r\n\r\n") == NULL) {
      bytes_send_client = recv(socket, buffer + len, MAX_BYTES - len, 0);
    } else {
      break;
    }
  }

  char *temp_req = (char *)malloc(strlen(buffer) * sizeof(char) + 1);
  for (int i = 0; i < strlen(buffer); i++) {
    temp_req[i] = buffer[i];
  }

  struct cache_element *temp = find(temp_req);

  if (temp != NULL) {
    int size = temp->length / sizeof(char);
    int pos = 0;
    char response[MAX_BYTES];

    while (pos < size) {
      bzero(response, MAX_BYTES);
      for (int i = 0; i < MAX_BYTES; i++) {
        response[i] = temp->data[i];
        pos++;
      }
      send(socket, response, MAX_BYTES, 0);
    }
    printf("data Retrieved from the cache");
    printf("%s\n\n", response);

  } else if (bytes_send_client > 0) {
    len = strlen(buffer);
    struct ParsedRequest *request = ParsedRequest_create();
    if (ParsedRequest_parse(request, buffer, len) < 0) {
      printf("Parsing Failed\n");

    }

    else {

      bzero(buffer, MAX_BYTES);
      if (!strcmp(request->method, "GET")) {
        // if GET request
        if (request->host && request->path &&
            checkHTTPversion(request->version) == 1) {
          bytes_send_client = handle_HTTP_Request(socket, request, temp_req);
          if (bytes_send_client == -1) {
            sendErrorMessage(socket, 500);
          }
        } else {
          sendErrorMessage(socket, 500);
        }
      } else {
        printf(
            "this code doesnt support any other method apart from GET request");
      }
    }

    ParsedRequest_destroy(request);

  } else if (bytes_send_client == 0) {
    printf("client is disconnected\n");
  }

  shutdown(socket, SHUT_RDWR);
  close(socket);
  free(buffer);
  sem_post(&semaphore);
  sem_getvalue(&semaphore, (int *)&p);
  printf("Semaphore post value is %d\n", p);
  free(temp_req);

  return NULL;
}

int main(int argc, char *argv[]) {
  int client_socket_id, client_length;
  struct sockaddr_in server_addr, client_addr;
  sem_init(&semaphore, 0, MAX_CLIENTS);
  /*
       Here we initialize the value of semaphores to 10 ,
       middle argument is "The pshared argument indicates whether this semaphore
     is to be shared between the threads of a process, or between processes."
      .If you pass it 0 you will get a semaphore that can be accessed by other
     threads in the same process -- essentially an in-process lock. You can use
     this as a mutex, or you can use it more generally for the resource-counting
     properties of a semaphore. Arguably if pthreads supported a semaphore API
     you wouldn't need this feature of sem_init, but semaphores in Unix precede
     pthreads by quite a bit of time. It would be better if the boolean was some
     kind of enumeration (e.g. SEM_PROCESS_PRIVATE vs SEM_PROCESS_SHARED),
     because then you wouldn't have had this question, but POSIX semaphores are
     a fairly old API as these things go.

  */

  //    Lets check the number of arguments
  if (argc == 2) {
    port = atoi(argv[1]);
  } else {
    printf("Too few arguments\n");
    exit(1);
  }

  printf("Starting proxy server at port %d\n", port);

  proxy_socket_id = socket(AF_INET, SOCK_STREAM, 0);
  if (proxy_socket_id < 0) {
    perror("Error creating socket\n");
    exit(1);
  }

  int reuse = 1;
  // 3rd argument : reuse the same socket if same url aaya
  if (setsockopt(proxy_socket_id, SOL_SOCKET, SO_REUSEADDR,
                 (const char *)&reuse, sizeof(reuse)) < 0) {
    perror("setSockOpt failed\n");
  }

  bzero((char *)&server_addr, sizeof(server_addr)); // cleans the server_addr

  server_addr.sin_family = AF_INET;   // ipv4 address use
  server_addr.sin_port = htons(port); // port number
  server_addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(proxy_socket_id, (struct sockaddr *)&server_addr,
           sizeof(server_addr) < 0)) {
    perror("Error binding socket, Port is not available \n");
    exit(1);
  }

  printf("binding on port %d\n", port);

  int listen_status = listen(proxy_socket_id, MAX_CLIENTS);
  if (listen_status < 0) {
    perror("Error listening on socket\n");
    exit(1);
  }

  int i = 0;
  int Connected_socketId[MAX_CLIENTS];

  while (1) {
    bzero((char *)&client_addr, sizeof(client_addr));
    client_length = sizeof(client_addr);
    client_socket_id = accept(proxy_socket_id, (struct sockaddr *)&client_addr,
                              (socklen_t *)&client_length);

    if (client_socket_id < 0) {
      perror("Error connecting to client socket\n");
    } else {
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
