#ifndef PROXY_PARSE_H
#define PROXY_PARSE_H

// Custom proxy_parse.h that supports all HTTP methods
// Compatible interface with original proxy_parse.h but extended functionality

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Maximum sizes
#define MAX_METHOD_LEN 16
#define MAX_HOSTNAME_LEN 256
#define MAX_PATH_LEN 1024
#define MAX_VERSION_LEN 16
#define MAX_PORT_LEN 8
#define MAX_HEADER_NAME_LEN 64
#define MAX_HEADER_VALUE_LEN 1024
#define MAX_HEADERS 50

// Header structure
typedef struct ParsedHeader {
    char name[MAX_HEADER_NAME_LEN];
    char value[MAX_HEADER_VALUE_LEN];
    struct ParsedHeader* next;
} ParsedHeader;

// Main request structure
typedef struct ParsedRequest {
    char method[MAX_METHOD_LEN];        // GET, POST, PUT, PATCH, DELETE, etc.
    char* host;                         // Host from URL or Host header
    char* path;                         // Path part of URL
    char* version;                      // HTTP version (HTTP/1.0 or HTTP/1.1)
    char* port;                         // Port number as string
    ParsedHeader* headers;              // Linked list of headers
    char* body;                         // Request body (for POST, PUT, PATCH)
    int content_length;                 // Content-Length value
    size_t body_length;                 // Actual body length
} ParsedRequest;

// Function declarations
ParsedRequest* ParsedRequest_create();
void ParsedRequest_destroy(ParsedRequest* pr);
int ParsedRequest_parse(ParsedRequest* pr, const char* buffer, int buflen);
int ParsedRequest_unparse(ParsedRequest* pr, char* buffer, size_t buflen);
int ParsedRequest_unparse_headers(ParsedRequest* pr, char* buffer, size_t buflen);

// Header manipulation functions
ParsedHeader* ParsedHeader_create();
void ParsedHeader_destroy(ParsedHeader* ph);
int ParsedHeader_set(ParsedRequest* pr, const char* name, const char* value);
char* ParsedHeader_get(ParsedRequest* pr, const char* name);
int ParsedHeader_remove(ParsedRequest* pr, const char* name);

// Utility functions
int is_valid_method(const char* method);
int is_valid_version(const char* version);
void trim_whitespace(char* str);
char* strndup_safe(const char* s, size_t n);

#endif // PROXY_PARSE_H