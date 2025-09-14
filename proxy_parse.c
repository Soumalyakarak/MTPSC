#include "proxy_parse.h"

// Utility function for safe string duplication
char* strndup_safe(const char* s, size_t n) {
    if (!s) return NULL;
    
    size_t len = strnlen(s, n);
    char* result = malloc(len + 1);
    if (!result) return NULL;
    
    memcpy(result, s, len);
    result[len] = '\0';
    return result;
}

// Trim whitespace from string
void trim_whitespace(char* str) {
    if (!str) return;
    
    // Trim leading whitespace
    char* start = str;
    while (isspace(*start)) start++;
    
    // Trim trailing whitespace
    char* end = start + strlen(start) - 1;
    while (end > start && isspace(*end)) end--;
    *(end + 1) = '\0';
    
    // Move trimmed string to beginning
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

// Check if HTTP method is valid
int is_valid_method(const char* method) {
    if (!method) return 0;
    
    const char* valid_methods[] = {
        "GET", "POST", "PUT", "DELETE", "PATCH", 
        "HEAD", "OPTIONS", "TRACE", "CONNECT"
    };
    
    for (size_t i = 0; i < sizeof(valid_methods) / sizeof(valid_methods[0]); i++) {
        if (strcmp(method, valid_methods[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

// Check if HTTP version is valid
int is_valid_version(const char* version) {
    if (!version) return 0;
    return (strcmp(version, "HTTP/1.0") == 0 || strcmp(version, "HTTP/1.1") == 0);
}

// Create new ParsedRequest
ParsedRequest* ParsedRequest_create() {
    ParsedRequest* pr = calloc(1, sizeof(ParsedRequest));
    if (!pr) return NULL;
    
    pr->host = NULL;
    pr->path = NULL;
    pr->version = NULL;
    pr->port = NULL;
    pr->headers = NULL;
    pr->body = NULL;
    pr->content_length = 0;
    pr->body_length = 0;
    
    return pr;
}

// Destroy ParsedRequest
void ParsedRequest_destroy(ParsedRequest* pr) {
    if (!pr) return;
    
    free(pr->host);
    free(pr->path);
    free(pr->version);
    free(pr->port);
    free(pr->body);
    
    // Free headers
    ParsedHeader* current = pr->headers;
    while (current) {
        ParsedHeader* next = current->next;
        ParsedHeader_destroy(current);
        current = next;
    }
    
    free(pr);
}

// Create new ParsedHeader
ParsedHeader* ParsedHeader_create() {
    ParsedHeader* ph = calloc(1, sizeof(ParsedHeader));
    return ph;
}

// Destroy ParsedHeader
void ParsedHeader_destroy(ParsedHeader* ph) {
    if (ph) {
        free(ph);
    }
}

// Parse HTTP request
int ParsedRequest_parse(ParsedRequest* pr, const char* buffer, int buflen) {
    if (!pr || !buffer || buflen <= 0) return -1;
    
    // Create working copy
    char* buf_copy = malloc(buflen + 1);
    if (!buf_copy) return -1;
    memcpy(buf_copy, buffer, buflen);
    buf_copy[buflen] = '\0';
    
    // Find headers end
    char* headers_end = strstr(buf_copy, "\r\n\r\n");
    if (!headers_end) {
        free(buf_copy);
        return -1;
    }
    
    // Extract body if present
    char* body_start = headers_end + 4;
    size_t body_len = strlen(body_start);
    if (body_len > 0) {
        pr->body = malloc(body_len + 1);
        if (pr->body) {
            memcpy(pr->body, body_start, body_len);
            pr->body[body_len] = '\0';
            pr->body_length = body_len;
        }
    }
    
    // Null terminate headers section
    *headers_end = '\0';
    
    // Parse request line
    char* line_end = strstr(buf_copy, "\r\n");
    if (!line_end) {
        free(buf_copy);
        return -1;
    }
    *line_end = '\0';
    
    // Parse method, URL, version
    char* method = strtok(buf_copy, " ");
    char* url = strtok(NULL, " ");
    char* version = strtok(NULL, " \r\n");
    
    if (!method || !url || !version) {
        free(buf_copy);
        return -1;
    }
    
    // Validate method and version
    if (!is_valid_method(method) || !is_valid_version(version)) {
        free(buf_copy);
        return -1;
    }
    
    // Store method and version
    strncpy(pr->method, method, MAX_METHOD_LEN - 1);
    pr->method[MAX_METHOD_LEN - 1] = '\0';
    
    pr->version = malloc(strlen(version) + 1);
    if (pr->version) {
        strcpy(pr->version, version);
    }
    
    // Parse URL
    char* url_copy = malloc(strlen(url) + 1);
    if (!url_copy) {
        free(buf_copy);
        return -1;
    }
    strcpy(url_copy, url);
    
    // Handle full URLs vs relative paths
    if (strncmp(url_copy, "http://", 7) == 0) {
        // Full URL: http://host:port/path
        char* host_start = url_copy + 7;
        char* path_start = strchr(host_start, '/');
        
        if (path_start) {
            pr->path = malloc(strlen(path_start) + 1);
            if (pr->path) strcpy(pr->path, path_start);
            *path_start = '\0';  // Terminate host part
        } else {
            pr->path = malloc(2);
            if (pr->path) strcpy(pr->path, "/");
        }
        
        // Check for port
        char* port_start = strchr(host_start, ':');
        if (port_start) {
            pr->port = malloc(strlen(port_start + 1) + 1);
            if (pr->port) strcpy(pr->port, port_start + 1);
            *port_start = '\0';  // Terminate host part
        } else {
            pr->port = malloc(3);
            if (pr->port) strcpy(pr->port, "80");
        }
        
        pr->host = malloc(strlen(host_start) + 1);
        if (pr->host) strcpy(pr->host, host_start);
        
    } else {
        // Relative path
        pr->path = malloc(strlen(url_copy) + 1);
        if (pr->path) strcpy(pr->path, url_copy);
        
        pr->port = malloc(3);
        if (pr->port) strcpy(pr->port, "80");
    }
    
    free(url_copy);
    
    // Parse headers
    char* headers_start = line_end + 2;  // Skip past request line
    char* header_line = strtok(headers_start, "\r\n");
    
    while (header_line) {
        char* colon = strchr(header_line, ':');
        if (colon) {
            *colon = '\0';
            char* name = header_line;
            char* value = colon + 1;
            
            trim_whitespace(name);
            trim_whitespace(value);
            
            // Special handling for important headers
            if (strcasecmp(name, "Host") == 0 && !pr->host) {
                // Extract host and port from Host header
                char* port_colon = strchr(value, ':');
                if (port_colon) {
                    pr->port = realloc(pr->port, strlen(port_colon + 1) + 1);
                    if (pr->port) strcpy(pr->port, port_colon + 1);
                    *port_colon = '\0';
                }
                pr->host = malloc(strlen(value) + 1);
                if (pr->host) strcpy(pr->host, value);
            } else if (strcasecmp(name, "Content-Length") == 0) {
                pr->content_length = atoi(value);
            }
            
            // Add to headers list
            ParsedHeader_set(pr, name, value);
        }
        header_line = strtok(NULL, "\r\n");
    }
    
    free(buf_copy);
    return 0;
}

// Set header value
int ParsedHeader_set(ParsedRequest* pr, const char* name, const char* value) {
    if (!pr || !name || !value) return -1;
    
    // Check if header already exists
    ParsedHeader* current = pr->headers;
    while (current) {
        if (strcasecmp(current->name, name) == 0) {
            // Update existing header
            strncpy(current->value, value, MAX_HEADER_VALUE_LEN - 1);
            current->value[MAX_HEADER_VALUE_LEN - 1] = '\0';
            return 0;
        }
        current = current->next;
    }
    
    // Create new header
    ParsedHeader* new_header = ParsedHeader_create();
    if (!new_header) return -1;
    
    strncpy(new_header->name, name, MAX_HEADER_NAME_LEN - 1);
    new_header->name[MAX_HEADER_NAME_LEN - 1] = '\0';
    strncpy(new_header->value, value, MAX_HEADER_VALUE_LEN - 1);
    new_header->value[MAX_HEADER_VALUE_LEN - 1] = '\0';
    
    // Add to front of list
    new_header->next = pr->headers;
    pr->headers = new_header;
    
    return 0;
}

// Get header value
char* ParsedHeader_get(ParsedRequest* pr, const char* name) {
    if (!pr || !name) return NULL;
    
    ParsedHeader* current = pr->headers;
    while (current) {
        if (strcasecmp(current->name, name) == 0) {
            return current->value;
        }
        current = current->next;
    }
    
    return NULL;
}

// Remove header
int ParsedHeader_remove(ParsedRequest* pr, const char* name) {
    if (!pr || !name) return -1;
    
    ParsedHeader* current = pr->headers;
    ParsedHeader* prev = NULL;
    
    while (current) {
        if (strcasecmp(current->name, name) == 0) {
            if (prev) {
                prev->next = current->next;
            } else {
                pr->headers = current->next;
            }
            ParsedHeader_destroy(current);
            return 0;
        }
        prev = current;
        current = current->next;
    }
    
    return -1;  // Header not found
}

// Unparse headers only
int ParsedRequest_unparse_headers(ParsedRequest* pr, char* buffer, size_t buflen) {
    if (!pr || !buffer || buflen == 0) return -1;
    
    size_t offset = 0;
    ParsedHeader* current = pr->headers;
    
    while (current && offset < buflen - 4) {  // Leave space for \r\n
        int written = snprintf(buffer + offset, buflen - offset, 
                              "%s: %s\r\n", current->name, current->value);
        if (written < 0 || (size_t)written >= buflen - offset) {
            return -1;  // Buffer too small
        }
        offset += written;
        current = current->next;
    }
    
    return 0;
}

// Unparse complete request
int ParsedRequest_unparse(ParsedRequest* pr, char* buffer, size_t buflen) {
    if (!pr || !buffer || buflen == 0) return -1;
    
    // Build request line
    int written = snprintf(buffer, buflen, "%s %s %s\r\n", 
                          pr->method, pr->path ? pr->path : "/", 
                          pr->version ? pr->version : "HTTP/1.1");
    if (written < 0 || (size_t)written >= buflen) return -1;
    
    // Add headers
    if (ParsedRequest_unparse_headers(pr, buffer + written, buflen - written) < 0) {
        return -1;
    }
    
    // Add final \r\n to end headers
    size_t current_len = strlen(buffer);
    if (current_len + 2 >= buflen) return -1;
    strcat(buffer, "\r\n");
    
    // Add body if present
    if (pr->body && pr->body_length > 0) {
        current_len = strlen(buffer);
        if (current_len + pr->body_length >= buflen) return -1;
        memcpy(buffer + current_len, pr->body, pr->body_length);
        buffer[current_len + pr->body_length] = '\0';
    }
    
    return 0;
}