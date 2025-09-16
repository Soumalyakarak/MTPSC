#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>

#define MAX_BYTES 8192
#define MAX_CLIENTS 400
#define MAX_SIZE 200*(1<<20)
#define MAX_ELEMENT_SIZE 10*(1<<20)

typedef struct cache_element cache_element;
struct cache_element
{
    char *data;
    int len;
    char *url;
    char *method;
    time_t lru_time_track;
    cache_element *next;
};

cache_element *find(char *url, char *method);
int add_cache_element(char *data, int size, char *url, char *method);
void remove_cache_element();

int port_number = 8080;
int proxy_socketId;
pthread_t tid[MAX_CLIENTS];
sem_t semaphore;
pthread_mutex_t lock;
cache_element *head;
int cache_size;

int connectRemoteServer(char* host_addr, int port_num)
{
    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
    if(remoteSocket < 0)
    {
        printf("Error in Creating Socket.\n");
        return -1;
    }
    
    struct hostent *host = gethostbyname(host_addr);
    if(host == NULL)
    {
        fprintf(stderr, "No such host exists: %s\n", host_addr);
        close(remoteSocket);
        return -1;
    }
    
    struct sockaddr_in server_addr;
    bzero((char*)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_num);
    memcpy((char *)&server_addr.sin_addr.s_addr, (char *)host->h_addr_list[0], host->h_length);
    
    if(connect(remoteSocket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    {
        fprintf(stderr, "Error in connecting to %s:%d\n", host_addr, port_num);
        close(remoteSocket);
        return -1;
    }
    
    return remoteSocket;
}

int sendErrorMessage(int socket, int status_code)
{
    char str[1024];
    char currentTime[50];
    time_t now = time(0);
    struct tm data = *gmtime(&now);
    strftime(currentTime, sizeof(currentTime), "%a, %d %b %Y %H:%M:%S %Z", &data);
    
    switch(status_code)
    {
        case 400: 
            snprintf(str, sizeof(str), 
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Length: 95\r\n"
                "Connection: close\r\n"
                "Content-Type: text/html\r\n"
                "Date: %s\r\n"
                "Server: ProxyServer/1.0\r\n\r\n"
                "<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n"
                "<BODY><H1>400 Bad Request</H1>\n</BODY></HTML>", currentTime);
            break;
            
        case 404: 
            snprintf(str, sizeof(str), 
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Length: 91\r\n"
                "Content-Type: text/html\r\n"
                "Connection: close\r\n"
                "Date: %s\r\n"
                "Server: ProxyServer/1.0\r\n\r\n"
                "<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n"
                "<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
            break;
            
        case 500: 
            snprintf(str, sizeof(str), 
                "HTTP/1.1 500 Internal Server Error\r\n"
                "Content-Length: 115\r\n"
                "Connection: close\r\n"
                "Content-Type: text/html\r\n"
                "Date: %s\r\n"
                "Server: ProxyServer/1.0\r\n\r\n"
                "<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n"
                "<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
            break;
            
        case 501: 
            snprintf(str, sizeof(str), 
                "HTTP/1.1 501 Not Implemented\r\n"
                "Content-Length: 103\r\n"
                "Connection: close\r\n"
                "Content-Type: text/html\r\n"
                "Date: %s\r\n"
                "Server: ProxyServer/1.0\r\n\r\n"
                "<HTML><HEAD><TITLE>501 Not Implemented</TITLE></HEAD>\n"
                "<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
            break;
            
        default: 
            return -1;
    }
    
    printf("Sent error %d to client\n", status_code);
    send(socket, str, strlen(str), 0);
    return 1;
}

int should_cache(char* method) {
    return (strcmp(method, "GET") == 0);
}

int handle_request(int clientSocket, ParsedRequest *request, char *original_request)
{
    char *buf = (char*)malloc(sizeof(char)*MAX_BYTES);
    if(!buf) {
        printf("Memory allocation failed\n");
        return -1;
    }
    
    // Build request line
    strcpy(buf, request->method);
    strcat(buf, " ");
    strcat(buf, request->path);
    strcat(buf, " ");
    strcat(buf, request->version);
    strcat(buf, "\r\n");
    
    size_t len = strlen(buf);
    
    // Set important headers
    if(ParsedHeader_set(request, "Connection", "close") < 0){
        printf("Failed to set Connection header\n");
    }
    
    if(ParsedHeader_get(request, "Host") == NULL)
    {
        if(ParsedHeader_set(request, "Host", request->host) < 0){
            printf("Failed to set Host header\n");
        }
    }
    
    // Add headers
    if(ParsedRequest_unparse_headers(request, buf + len, (size_t)MAX_BYTES - len) < 0) {
        printf("Failed to unparse headers\n");
    }
    
    strcat(buf, "\r\n");
    
    int server_port = 80;
    if(request->port != NULL)
        server_port = atoi(request->port);
    
    int remoteSocketID = connectRemoteServer(request->host, server_port);
    if(remoteSocketID < 0)
    {
        free(buf);
        return -1;
    }
    
    // Send headers to remote server
    int bytes_sent = send(remoteSocketID, buf, strlen(buf), 0);
    if(bytes_sent < 0) {
        printf("Failed to send headers to server\n");
        free(buf);
        close(remoteSocketID);
        return -1;
    }
    
    // Send request body if present (POST, PUT, PATCH)
    if(request->body && request->body_length > 0) {
        printf("Forwarding request body (%zu bytes) for method: %s\n", 
               request->body_length, request->method);
        int body_sent = send(remoteSocketID, request->body, request->body_length, 0);
        if(body_sent < 0) {
            printf("Failed to send body to server\n");
            free(buf);
            close(remoteSocketID);
            return -1;
        }
    }
    
    bzero(buf, MAX_BYTES);
    
    // Receive and forward response
    char *response_buffer = NULL;
    int total_response_size = 0;
    int response_capacity = MAX_BYTES;
    
    // Only cache GET requests
    if(should_cache(request->method)) {
        response_buffer = (char*)malloc(response_capacity);
        if(!response_buffer) {
            printf("Failed to allocate response buffer\n");
        }
    }
    
    int bytes_recv = recv(remoteSocketID, buf, MAX_BYTES-1, 0);
    
    while(bytes_recv > 0)
    {
        // Forward to client
        int bytes_sent_to_client = send(clientSocket, buf, bytes_recv, 0);
        if(bytes_sent_to_client < 0) {
            perror("Error sending data to client");
            break;
        }
        
        // Store for caching (GET requests only)
        if(response_buffer && should_cache(request->method)) {
            if(total_response_size + bytes_recv >= response_capacity) {
                response_capacity *= 2;
                response_buffer = (char*)realloc(response_buffer, response_capacity);
                if(!response_buffer) {
                    printf("Failed to reallocate response buffer\n");
                    break;
                }
            }
            memcpy(response_buffer + total_response_size, buf, bytes_recv);
            total_response_size += bytes_recv;
        }
        
        bzero(buf, MAX_BYTES);
        bytes_recv = recv(remoteSocketID, buf, MAX_BYTES-1, 0);
    }
    
    // Cache response for GET requests
    if(response_buffer && total_response_size > 0 && should_cache(request->method)) {
        response_buffer[total_response_size] = '\0';
        add_cache_element(response_buffer, total_response_size, original_request, request->method);
        printf("Response cached successfully (%d bytes)\n", total_response_size);
    }
    
    if(response_buffer) free(response_buffer);
    free(buf);
    close(remoteSocketID);
    return 0;
}

int checkHTTPversion(char *msg)
{
    if(strncmp(msg, "HTTP/1.1", 8) == 0 || strncmp(msg, "HTTP/1.0", 8) == 0)
        return 1;
    return -1;
}

int is_supported_method(char* method) {
    return (strcmp(method, "GET") == 0 || 
            strcmp(method, "POST") == 0 || 
            strcmp(method, "PUT") == 0 || 
            strcmp(method, "PATCH") == 0 || 
            strcmp(method, "DELETE") == 0);
}

void *thread_fn(void *socketNew){
    sem_wait(&semaphore);
    
    int *t = (int*)socketNew;
    int socket = *t;
    
    char *buffer = (char*)calloc(MAX_BYTES * 2, sizeof(char));
    if(!buffer) {
        printf("Memory allocation failed\n");
        sem_post(&semaphore);
        return NULL;
    }
    
    // Receive request
    int bytes_recv = recv(socket, buffer, MAX_BYTES * 2 - 1, 0);
    if(bytes_recv <= 0) {
        printf("Failed to receive data from client\n");
        free(buffer);
        close(socket);
        sem_post(&semaphore);
        return NULL;
    }

    // Ensure complete HTTP request
    while(bytes_recv > 0 && !strstr(buffer, "\r\n\r\n")) {
        int len = strlen(buffer);
        if(len < MAX_BYTES * 2 - 1) {
            int additional = recv(socket, buffer + len, MAX_BYTES * 2 - len - 1, 0);
            if(additional <= 0) break;
            bytes_recv += additional;
        } else {
            break;
        }
    }
    
    buffer[bytes_recv] = '\0';
    
    // Create copy for caching
    char *tempReq = (char*)malloc((strlen(buffer) + 1) * sizeof(char));
    if(tempReq) {
        strcpy(tempReq, buffer);
    }
    
    // Parse request using our custom parser
    ParsedRequest* request = ParsedRequest_create();
    if(!request) {
        printf("Failed to create ParsedRequest\n");
        sendErrorMessage(socket, 500);
        free(buffer);
        if(tempReq) free(tempReq);
        close(socket);
        sem_post(&semaphore);
        return NULL;
    }
    
    if(ParsedRequest_parse(request, buffer, strlen(buffer)) < 0) {
        printf("Failed to parse request\n");
        sendErrorMessage(socket, 400);
    } else {
        printf("Method: %s, Host: %s, Path: %s, Content-Length: %d\n", 
               request->method, request->host ? request->host : "NULL", 
               request->path ? request->path : "NULL", request->content_length);
        
        if(!is_supported_method(request->method)) {
            printf("Method %s not supported\n", request->method);
            sendErrorMessage(socket, 501);
        } else if(!request->host || !request->path || 
                  checkHTTPversion(request->version) != 1) {
            printf("Invalid request format\n");
            sendErrorMessage(socket, 400);
        } else {
            // Check cache for GET requests only
            if(should_cache(request->method) && tempReq) {
                cache_element* cached = find(tempReq, request->method);
                if(cached) {
                    printf("Data retrieved from cache\n");
                    send(socket, cached->data, cached->len, 0);
                    free(buffer);
                    if(tempReq) free(tempReq);
                    ParsedRequest_destroy(request);
                    close(socket);
                    sem_post(&semaphore);
                    return NULL;
                }
            }
            
            // Handle the request
            int result = handle_request(socket, request, tempReq ? tempReq : buffer);
            if(result == -1) {
                sendErrorMessage(socket, 500);
            }
        }
    }
    
    ParsedRequest_destroy(request);
    free(buffer);
    if(tempReq) free(tempReq);
    close(socket);
    sem_post(&semaphore);
    
    return NULL;
}

int main(int argc, char *argv[])
{
    int client_socketId, client_len;
    struct sockaddr_in server_addr, client_addr;
    
    sem_init(&semaphore, 0, MAX_CLIENTS);
    pthread_mutex_init(&lock, NULL);
    
    if(argc == 2) {
        port_number = atoi(argv[1]);
    } else {
        printf("Usage: %s <port_number>\n", argv[0]);
        exit(1);
    }
    
    printf("Starting Multi-Method Proxy Server at port: %d\n", port_number);
    printf("Supported methods: GET, POST, PUT, PATCH, DELETE\n");
    
    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);
    if(proxy_socketId < 0) {
        perror("Failed to create socket");
        exit(1);
    }
    
    int reuse = 1;
    if(setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0) {
        perror("setsockopt failed");
        exit(1);
    }
    
    bzero((char*)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    
    if(bind(proxy_socketId, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(1);
    }
    
    if(listen(proxy_socketId, MAX_CLIENTS) < 0) {
        perror("Listen failed");
        exit(1);
    }
    
    printf("Proxy server listening on port %d...\n", port_number);
    
    int i = 0;
    int Connected_socketId[MAX_CLIENTS];
    
    while(1) {
        bzero((char*)&client_addr, sizeof(client_addr));
        client_len = sizeof(client_addr);
        
        client_socketId = accept(proxy_socketId, (struct sockaddr*)&client_addr, (socklen_t*)&client_len);
        
        if(client_socketId < 0) {
            perror("Accept failed");
            continue;
        }
        
        Connected_socketId[i] = client_socketId;
        
        struct sockaddr_in *client_pt = (struct sockaddr_in*)&client_addr;
        struct in_addr ip_addr = client_pt->sin_addr;
        char str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ip_addr, str, INET_ADDRSTRLEN);
        printf("Client connected: %s:%d\n", str, ntohs(client_addr.sin_port));
        
        pthread_create(&tid[i], NULL, thread_fn, (void*)&Connected_socketId[i]);
        
        i = (i + 1) % MAX_CLIENTS;
    }
    
    close(proxy_socketId);
    return 0;
}

// Cache functions
cache_element* find(char* url, char* method){
    cache_element* site = NULL;
    
    int temp_lock_val = pthread_mutex_lock(&lock);
    if(temp_lock_val != 0) {
        printf("Cache lock failed: %d\n", temp_lock_val);
        return NULL;
    }
    
    if(head != NULL) {
        site = head;
        while(site != NULL) {
            if(!strcmp(site->url, url) && !strcmp(site->method, method)) {
                printf("URL found in cache for method %s\n", method);
                site->lru_time_track = time(NULL);
                break;
            }
            site = site->next;
        }
    }
    
    pthread_mutex_unlock(&lock);
    return site;
}

void remove_cache_element(){
    int temp_lock_val = pthread_mutex_lock(&lock);
    if(temp_lock_val != 0) {
        printf("Remove cache lock failed: %d\n", temp_lock_val);
        return;
    }
    
    if(head != NULL) {
        cache_element *p = head;
        cache_element *q = head; 
        cache_element *temp = head;
        
        // Find element with oldest LRU timestamp
        for(q = head; q->next != NULL; q = q->next) {
            if((q->next)->lru_time_track < temp->lru_time_track) {
                temp = q->next;
                p = q;
            }
        }
        
        if(temp == head) {
            head = head->next;
        } else {
            p->next = temp->next;
        }
        
        cache_size = cache_size - temp->len - sizeof(cache_element) - 
                     strlen(temp->url) - strlen(temp->method) - 2;
        
        free(temp->data);
        free(temp->url);
        free(temp->method);
        free(temp);
        
        printf("Cache element removed\n");
    }
    
    pthread_mutex_unlock(&lock);
}

int add_cache_element(char* data, int size, char* url, char* method){
    int temp_lock_val = pthread_mutex_lock(&lock);
    if(temp_lock_val != 0) {
        printf("Add cache lock failed: %d\n", temp_lock_val);
        return 0;
    }
    
    int element_size = size + 1 + strlen(url) + strlen(method) + 2 + sizeof(cache_element);
    
    if(element_size > MAX_ELEMENT_SIZE) {
        printf("Element too large for cache\n");
        pthread_mutex_unlock(&lock);
        return 0;
    }
    
    // Make space if needed
    while(cache_size + element_size > MAX_SIZE) {
        remove_cache_element();
    }
    
    cache_element* element = (cache_element*)malloc(sizeof(cache_element));
    if(!element) {
        pthread_mutex_unlock(&lock);
        return 0;
    }
    
    element->data = (char*)malloc(size + 1);
    element->url = (char*)malloc(strlen(url) + 1);
    element->method = (char*)malloc(strlen(method) + 1);
    
    if(!element->data || !element->url || !element->method) {
        if(element->data) free(element->data);
        if(element->url) free(element->url);
        if(element->method) free(element->method);
        free(element);
        pthread_mutex_unlock(&lock);
        return 0;
    }
    
    memcpy(element->data, data, size);
    element->data[size] = '\0';
    strcpy(element->url, url);
    strcpy(element->method, method);
    element->lru_time_track = time(NULL);
    element->len = size;
    element->next = head;
    
    head = element;
    cache_size += element_size;
    
    printf("Added to cache: %s %s (%d bytes)\n", method, url, size);
    
    pthread_mutex_unlock(&lock);
    return 1;
}