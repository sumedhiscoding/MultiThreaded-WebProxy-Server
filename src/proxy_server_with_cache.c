#include "proxy_parse.h";
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>


#define MAX_CLIENTS 10

struct cache_element{
    char* data;
    int length;
    char* url;
    time_t lru_time_track;   // time based lru cache : (oldest will get deleted first)
    struct cache_element* next;
};


struct cache_element* find(char* url);
int add_cache_element(char* data,int size,char* url);
void remove_cache_element();

int port=8080;

int proxy_socket_id;

pthread_t tid[MAX_CLIENTS];

sem_t semaphore;
pthread_mutex_t lock;
