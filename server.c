#if defined(_MSC_VER)
#pragma comment(lib,"Ws2_32.lib")
#endif

#include <pthread.h>
#include <stdio.h>
#include <string.h>  
#include <sys/stat.h>
#include <time.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <ctype.h>

#define BUFFER_SIZE 8192
#define MAX_HEADERS 50

typedef struct {
    char *name;
    char *value;
} header_t;

typedef struct {
    int code;
    const char* message;
} status_entry_t;

static const status_entry_t status_table[] = {
    {200, "OK"}, {201, "Created"}, {202, "Accepted"}, {204, "No Content"},
    {300, "Multiple Choices"}, {301, "Moved Permanently"}, {302, "Moved Temporarily"}, {304, "Not Modified"},
    {400, "Bad Request"}, {401, "Unauthorized"}, {403, "Forbidden"}, {404, "Not Found"}, {411, "Length Required"},
    {500, "Internal Server Error"}, {501, "Not Implemented"}, {502, "Bad Gateway"}, {503, "Service Unavailable"}
};

static const char* get_status_message(int code) {
    for (int i = 0; i < sizeof(status_table)/sizeof(status_table[0]); i++) {
        if (status_table[i].code == code) {
            return status_table[i].message;
        }
    }
    return "Unknown";
}

static const char* get_content_type(const char* filepath) {
    if (!filepath) return "application/octet-stream";
    
    const char* ext = strrchr(filepath, '.');
    if (!ext) return "application/octet-stream";
    
    if (strcmp(ext, ".txt") == 0) return "text/plain";
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".js") == 0) return "application/javascript";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".gif") == 0) return "image/gif";
    if (strcmp(ext, ".ico") == 0) return "image/x-icon";
    return "application/octet-stream";
}

static int is_safe_path(const char* path) {
    if (!path) return 0;
    
    size_t len = strlen(path);
    
    // Check for directory traversal patterns
    if (strstr(path, "..") || strstr(path, "%2e%2e") || strstr(path, "%2E%2E")) {
        return 0;
    }
    
    // Check for colon (Windows drive letters)
    if (strchr(path, ':')) {
        return 0;
    }
    
    // Check for UNC paths (//server/share)
    if (len >= 2 && path[0] == '/' && path[1] == '/') {
        return 0;
    }
    
    // Check for Windows absolute paths (C:\path)
    if (len >= 2 && isalpha((unsigned char)path[0]) && path[1] == ':') {
        return 0;
    }
    
    return 1;
}

static void url_decode(char* dst, const char* src) {
    char* p = dst;
    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            unsigned int hex;
            if (sscanf(src + 1, "%2x", &hex) == 1) {
                *p++ = (char)hex;
                src += 3;
            } else {
                *p++ = *src++;
            }
        } else if (*src == '+') {
            *p++ = ' ';
            src++;
        } else {
            *p++ = *src++;
        }
    }
    *p = '\0';
}

static void cleanup_headers(header_t* headers, int count) {
    for (int i = 0; i < count; i++) {
        free(headers[i].name);
        free(headers[i].value);
    }
}

static void* handle_request(void* arg) {
    SOCKET client = *((SOCKET*)arg);
    free(arg);

    char* buffer = malloc(BUFFER_SIZE);
    if (!buffer) {
        closesocket(client);
        return NULL;
    }

    // Read request
    int total = 0;
    while (total < BUFFER_SIZE - 1) {
        int received = recv(client, buffer + total, BUFFER_SIZE - total - 1, 0);
        if (received <= 0) break;
        
        total += received;
        buffer[total] = '\0';
        
        if (strstr(buffer, "\r\n\r\n")) break;
    }

    if (total == 0) {
        free(buffer);
        closesocket(client);
        return NULL;
    }

    // Parse request line
    char* request_copy = strdup(buffer);
    if (!request_copy) {
        free(buffer);
        closesocket(client);
        return NULL;
    }
    
    const char* method = strtok(request_copy, " ");
    const char* uri = strtok(NULL, " ");
    const char* version = strtok(NULL, "\r\n");
    
    int status_code = 200;
    char* filepath = NULL;
    
    // Validate request
    if (!version || (strcmp(version, "HTTP/1.0") != 0 && strcmp(version, "HTTP/1.1") != 0) || !method || !uri) {
        status_code = 400;
    } else if (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0 && strcmp(method, "POST") != 0) {
        status_code = 501;
    }
    
    // Process URI
    if (uri && status_code == 200) {
        const char* path_start = (uri[0] == '/') ? uri + 1 : uri;
        
        char* decoded_path = malloc(strlen(path_start) + 1);
        if (!decoded_path) {
            status_code = 500;
        } else {
            url_decode(decoded_path, path_start);
            
            if (!is_safe_path(decoded_path)) {
                status_code = 403;
                free(decoded_path);
                decoded_path = NULL;
            } else if (strlen(decoded_path) == 0) {
                free(decoded_path);
                decoded_path = strdup("index.html");
                if (!decoded_path) status_code = 500;
            }
            filepath = decoded_path;
        }
    }

    // Parse headers
    header_t headers[MAX_HEADERS];
    int header_count = 0;
    
    char* line = strtok(NULL, "\r\n");
    while (line && *line && header_count < MAX_HEADERS) {
        char* colon = strchr(line, ':');
        if (!colon) break;
        
        *colon = '\0';
        const char* value = colon + 1;
        while (*value == ' ' || *value == '\t') value++;
        
        headers[header_count].name = strdup(line);
        headers[header_count].value = strdup(value);
        if (!headers[header_count].name || !headers[header_count].value) {
            free(headers[header_count].name);
            free(headers[header_count].value);
            break;
        }
        header_count++;
        line = strtok(NULL, "\r\n");
    }

    // Handle POST requests
    char* request_body = NULL;
    int content_length = -1;
    int is_post = method && strcmp(method, "POST") == 0;
    
    if (is_post && status_code == 200) {
        // Find Content-Length
        for (int i = 0; i < header_count; i++) {
            if (strcasecmp(headers[i].name, "Content-Length") == 0) {
                content_length = atoi(headers[i].value);
                break;
            }
        }

        if (content_length < 0) {
            status_code = 411;
        } else if (content_length == 0) {
            request_body = malloc(1);
            if (request_body) request_body[0] = '\0';
            else status_code = 500;
        } else {
            // Read request body
            char* body_start = strstr(buffer, "\r\n\r\n");
            if (body_start) body_start += 4;
            int body_already_read = body_start ? (buffer + total) - body_start : 0;
            
            request_body = malloc(content_length + 1);
            if (!request_body) {
                status_code = 500;
            } else {
                if (body_already_read > 0) {
                    int copy_size = (body_already_read > content_length) ? content_length : body_already_read;
                    memcpy(request_body, body_start, copy_size);
                }
                
                int remaining = content_length - body_already_read;
                while (remaining > 0) {
                    int bytes_read = recv(client, request_body + (content_length - remaining), remaining, 0);
                    if (bytes_read <= 0) break;
                    remaining -= bytes_read;
                }
                request_body[content_length] = '\0';
            }
        }
    }

    // Handle file operations
    char* response_body = NULL;
    int response_body_size = 0;
    struct stat file_stat;
    int has_file_stat = 0;

    if (status_code == 200) {
        if (is_post && content_length >= 0) {
            // POST: Create/write file
            FILE* fptr = fopen(filepath, "wb");
            if (fptr && (content_length == 0 || request_body)) {
                if (content_length > 0) {
                    fwrite(request_body, 1, content_length, fptr);
                }
                fclose(fptr);
                status_code = 201;
                response_body = strdup("<html><body><h1>Resource Created</h1></body></html>");
                if (response_body) response_body_size = strlen(response_body);
                else status_code = 500;
            } else {
                if (fptr) fclose(fptr);
                status_code = 403;
            }
        } else if (method && (strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0)) {
            // GET/HEAD: Read file
            if (filepath && stat(filepath, &file_stat) == 0) {
                has_file_stat = 1;
                FILE* fptr = fopen(filepath, "rb");
                if (fptr) {
                    fseek(fptr, 0L, SEEK_END);
                    response_body_size = ftell(fptr);
                    fseek(fptr, 0L, SEEK_SET);
                    
                    if (strcmp(method, "GET") == 0 && response_body_size > 0) {
                        response_body = malloc(response_body_size + 1);
                        if (response_body) {
                            fread(response_body, 1, response_body_size, fptr);
                            response_body[response_body_size] = '\0';
                        }
                    }
                    fclose(fptr);
                } else {
                    status_code = 404;
                }
            } else {
                status_code = 404;
            }
        }
    }

    // Generate error pages if needed
    if (status_code >= 400 && !response_body) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
            "<html><body><h1>%d %s</h1></body></html>", 
            status_code, get_status_message(status_code));
        response_body = strdup(error_msg);
        if (response_body) response_body_size = strlen(response_body);
    }

    // Generate response headers
    char date_header[64];
    time_t now = time(NULL);
    strftime(date_header, sizeof(date_header), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&now));

    const char* content_type = (status_code >= 400 || status_code == 201) ? "text/html" : get_content_type(filepath);

    char* response_headers = malloc(BUFFER_SIZE);
    if (response_headers) {
        int len = snprintf(response_headers, BUFFER_SIZE,
            "%s %d %s\r\n"
            "Date: %s\r\n"
            "Server: MyBadHTTPServer\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n",
            version ? version : "HTTP/1.0", status_code, get_status_message(status_code),
            date_header, content_type, response_body_size);

        // Add Last-Modified if we have file stats
        if (has_file_stat) {
            char last_modified[64];
            strftime(last_modified, sizeof(last_modified), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&file_stat.st_mtime));
            len += snprintf(response_headers + len, BUFFER_SIZE - len, "Last-Modified: %s\r\n", last_modified);
        }

        // Add Location header for 201 Created
        if (status_code == 201 && filepath) {
            snprintf(response_headers + len, BUFFER_SIZE - len, "Location: /%s\r\n", filepath);
        }

        // Add final CRLF safely
        int current_len = strlen(response_headers);
        if (current_len < BUFFER_SIZE - 3) {
            strncpy(response_headers + current_len, "\r\n", BUFFER_SIZE - current_len - 1);
            response_headers[BUFFER_SIZE - 1] = '\0';
        }

        // Send response
        send(client, response_headers, strlen(response_headers), 0);
        if (method && strcmp(method, "HEAD") != 0 && response_body && response_body_size > 0) {
            send(client, response_body, response_body_size, 0);
        }
    }

    // Cleanup
    cleanup_headers(headers, header_count);
    free(request_copy);
    free(response_headers);
    free(response_body);
    free(request_body);
    free(filepath);
    free(buffer);
    closesocket(client);

    return NULL;
}

int main(int argc, char *argv[]) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData)) {
        printf("WSAStartup failed with error: %d\n", WSAGetLastError());
        return 1;
    }

    SOCKET sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        printf("socket() failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    struct sockaddr_in6 server_addr = {0};
    server_addr.sin6_family = AF_INET6;
    server_addr.sin6_port = htons(8000);
    inet_pton(AF_INET6, "::1", &server_addr.sin6_addr);

    if (bind(sock, (SOCKADDR*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("bind failed with error %u\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    if (listen(sock, SOMAXCONN) == SOCKET_ERROR) {
        printf("listen failed with error: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    printf("Server listening on [::1]:8000\n");

    while (1) {
        struct sockaddr_in6 client_addr = {0};
        socklen_t client_addr_len = sizeof(client_addr);
        SOCKET* client = malloc(sizeof(SOCKET));
        if (!client) continue;

        *client = accept(sock, (struct sockaddr*)&client_addr, &client_addr_len);
        if (*client == INVALID_SOCKET) {
            free(client);
            continue;
        }

        pthread_t thread;
        pthread_create(&thread, NULL, handle_request, client);
        pthread_detach(thread);
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}