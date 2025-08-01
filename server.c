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

// HTTP 1.0 status codes
static const status_entry_t status_table[] = {
    // 2xx Success
    {200, "OK"},
    {201, "Created"},
    {202, "Accepted"},
    {204, "No Content"},
    
    // 3xx Redirection
    {300, "Multiple Choices"},
    {301, "Moved Permanently"},
    {302, "Moved Temporarily"},
    {304, "Not Modified"},
    
    // 4xx Client Error
    {400, "Bad Request"},
    {401, "Unauthorized"},
    {403, "Forbidden"},
    {404, "Not Found"},
    {411, "Length Required"},
    
    // 5xx Server Error
    {500, "Internal Server Error"},
    {501, "Not Implemented"},
    {502, "Bad Gateway"},
    {503, "Service Unavailable"}
};

static const char* get_status_message(int code) {
    for (int i = 0; i < sizeof(status_table)/sizeof(status_table[0]); i++) {
        if (status_table[i].code == code) {
            return status_table[i].message;
        }
    }
    return "Unknown";
}

// Content-type header
static const char* get_content_type(const char* filepath) {
    if (!filepath) return "application/octet-stream";
    
    const char* ext = strrchr(filepath, '.');
    if (!ext) return "application/octet-stream";
    
    if (strcmp(ext, ".txt") == 0) return "text/plain";
    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".htm") == 0) return "text/html";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".js") == 0) return "application/javascript";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".gif") == 0) return "image/gif";
    if (strcmp(ext, ".ico") == 0) return "image/x-icon";
    return "application/octet-stream";
}

// Security check for path traversal
static int is_safe_path(const char* path) {
    if (!path) return 0;
    
    // Check for directory traversal patterns
    if (strstr(path, "..") != NULL) return 0;
    if (strstr(path, "%2e%2e") != NULL) return 0;
    if (strstr(path, "%2E%2E") != NULL) return 0;
    if (strchr(path, ':') != NULL) return 0;  // Windows drive letters
    
    // Check for absolute paths
    if (path[0] == '/' && path[1] == '/') return 0;  // UNC paths
    if (strlen(path) > 1 && path[1] == ':') return 0;  // Windows absolute paths
    
    return 1;
}

// URL decode function
static void url_decode(char* dst, const char* src) {
    char* p = dst;
    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            int hex;
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

static void* handle_request(void* arg) {
    printf("Current thread ID: %lu\n", (unsigned long)pthread_self());

    SOCKET client = *((SOCKET*)arg);
    free(arg);

    char* buffer = (char*)malloc(BUFFER_SIZE);
    if (!buffer) {
        closesocket(client);
        return NULL;
    }

    // Read request with proper buffering
    int total = 0, received;
    while (total < BUFFER_SIZE - 1) {
        received = recv(client, buffer + total, BUFFER_SIZE - total - 1, 0);
        if (received == SOCKET_ERROR) {
            printf("recv failed: %d\n", WSAGetLastError());
            free(buffer);
            closesocket(client);
            return NULL;
        } else if (received == 0) {
            break;
        }
        
        total += received;
        buffer[total] = '\0';
        
        // Check for end of headers
        if (strstr(buffer, "\r\n\r\n")) {
            break;
        }
    }

    if (total == 0) {
        printf("Client closed connection\n");
        free(buffer);
        closesocket(client);
        return NULL;
    }

    printf("Full message (%d bytes):\n%s\n", total, buffer);

    // Parse request line
    char* request_copy = strdup(buffer);
    char* method = strtok(request_copy, " ");
    char* uri = strtok(NULL, " ");
    char* version = strtok(NULL, "\r\n");
    
    int status_code = 200;
    char* filepath = NULL;
    
    // Validate HTTP version
    if (!version || (strcmp(version, "HTTP/1.0") != 0 && strcmp(version, "HTTP/1.1") != 0)) {
        if (method && strcmp(method, "GET") == 0 && !version) {
            // HTTP/0.9 Simple-Request - not fully supported
            status_code = 400;
        } else {
            status_code = 400;
        }
    }
    
    // Validate method
    if (method && status_code == 200) {
        if (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0 && strcmp(method, "POST") != 0) {
            status_code = 501;
        }
    } else if (!method) {
        status_code = 400;
    }
    
    // Process URI
    if (uri && status_code == 200) {
        // Remove leading slash
        if (uri[0] == '/') {
            filepath = uri + 1;
        } else {
            filepath = uri;
        }
        
        // URL decode
        char* decoded_path = malloc(strlen(filepath) + 1);
        url_decode(decoded_path, filepath);
        
        // Security check
        if (!is_safe_path(decoded_path)) {
            status_code = 403;
        }
        
        // Set default file if empty path
        if (strlen(decoded_path) == 0) {
            free(decoded_path);
            decoded_path = strdup("index.html");
        }
        
        filepath = decoded_path;
    } else if (!uri) {
        status_code = 400;
    }

    // Parse headers
    header_t headers[MAX_HEADERS];
    int header_count = 0;
    
    char* line = strtok(NULL, "\r\n");
    while (line && *line && header_count < MAX_HEADERS) {
        char* colon = strchr(line, ':');
        if (!colon) break;
        
        *colon = '\0';
        char* name = line;
        char* value = colon + 1;
        
        // Trim whitespace
        while (*value == ' ' || *value == '\t') value++;
        
        headers[header_count].name = strdup(name);
        headers[header_count].value = strdup(value);
        header_count++;
        
        line = strtok(NULL, "\r\n");
    }

    // Handle POST requests
    char* request_body = NULL;
    int content_length = -1;
    int is_post = method && strcmp(method, "POST") == 0;
    
    if (is_post && status_code == 200) {
        // Find Content-Length header
        for (int i = 0; i < header_count; i++) {
            if (strcasecmp(headers[i].name, "Content-Length") == 0) {
                content_length = atoi(headers[i].value);
                break;
            }
        }
        
        if (content_length < 0) {
            status_code = 411; // Length Required
        } else {
            // Read request body
            char* body_start = strstr(buffer, "\r\n\r\n");
            if (body_start) body_start += 4;
            int body_already_read = body_start ? (buffer + total) - body_start : 0;
            
            request_body = malloc(content_length + 1);
            if (!request_body) {
                status_code = 500;
            } else {
                // Copy already read body data
                if (body_already_read > 0) {
                    int copy_size = (body_already_read > content_length) ? content_length : body_already_read;
                    memcpy(request_body, body_start, copy_size);
                }
                
                // Read remaining body if needed
                int remaining = content_length - body_already_read;
                if (remaining > 0) {
                    int bytes_read = 0;
                    while (remaining > 0) {
                        bytes_read = recv(client, request_body + (content_length - remaining), remaining, 0);
                        if (bytes_read <= 0) break;
                        remaining -= bytes_read;
                    }
                }
                request_body[content_length] = '\0';
            }
        }
    }

    // Handle file operations
    FILE *fptr = NULL;
    char* response_body = NULL;
    int response_body_size = 0;
    struct stat file_stat;
    int has_file_stat = 0;

    if (status_code == 200) {
        if (is_post && content_length > 0) {
            // POST: Create/write file
            if (filepath) {
                fptr = fopen(filepath, "wb");
                if (fptr && request_body) {
                    size_t written = fwrite(request_body, 1, content_length, fptr);
                    fclose(fptr);
                    
                    if (written == content_length) {
                        status_code = 201;
                        response_body = strdup("<html><body><h1>Resource Created</h1></body></html>");
                        response_body_size = strlen(response_body);
                    } else {
                        status_code = 500;
                    }
                } else {
                    if (fptr) fclose(fptr);
                    status_code = 403;
                }
            } else {
                status_code = 400;
            }
        } else if (method && (strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0)) {
            // GET/HEAD: Read file
            if (filepath) {
                if (stat(filepath, &file_stat) == 0) {
                    has_file_stat = 1;
                    fptr = fopen(filepath, "rb");
                }
                
                if (!fptr) {
                    status_code = 404;
                } else {
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
                }
            } else {
                status_code = 404;
            }
        }
    }

    // Generate error pages
    if (status_code >= 400 && !response_body) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
            "<html><body><h1>%d %s</h1></body></html>", 
            status_code, get_status_message(status_code));
        response_body = strdup(error_msg);
        response_body_size = strlen(response_body);
    }

    // Generate response headers
    char date_header[64];
    time_t now = time(NULL);
    struct tm* gmt = gmtime(&now);
    strftime(date_header, sizeof(date_header), "%a, %d %b %Y %H:%M:%S GMT", gmt);

    char last_modified[64] = "";
    if (has_file_stat) {
        struct tm* mod_time = gmtime(&file_stat.st_mtime);
        strftime(last_modified, sizeof(last_modified), "%a, %d %b %Y %H:%M:%S GMT", mod_time);
    }

    // Determine content type
    const char* content_type;
    if (status_code == 201) {
        content_type = "text/html";
    } else if (status_code >= 400) {
        content_type = "text/html";
    } else {
        content_type = get_content_type(filepath);
    }

    // Build response
    char* response_headers = malloc(BUFFER_SIZE);
    int header_len = snprintf(response_headers, BUFFER_SIZE,
        "%s %d %s\r\n"
        "Date: %s\r\n"
        "Server: MyBadHTTPServer\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "%s%s%s",
        version ? version : "HTTP/1.0", status_code, get_status_message(status_code),
        date_header,
        content_type,
        response_body_size,
        (strlen(last_modified) > 0) ? "Last-Modified: " : "",
        (strlen(last_modified) > 0) ? last_modified : "",
        (strlen(last_modified) > 0) ? "\r\n" : ""
    );

    // Add Location header for 201 Created BEFORE final CRLFCRLF
    if (status_code == 201 && filepath) {
        // Properly escape filepath for URL
        char* encoded_path = malloc(strlen(filepath) * 3 + 1);
        char* ptr = encoded_path;
        for (int i = 0; filepath[i]; i++) {
            if (isalnum(filepath[i]) || filepath[i] == '-' || filepath[i] == '_' || 
                filepath[i] == '.' || filepath[i] == '~' || filepath[i] == '/') {
                *ptr++ = filepath[i];
            } else {
                sprintf(ptr, "%%%02X", (unsigned char)filepath[i]);
                ptr += 3;
            }
        }
        *ptr = '\0';

        char location_header[512];
        snprintf(location_header, sizeof(location_header), "Location: /%s\r\n", encoded_path);
        free(encoded_path);

        strncat(response_headers, location_header, BUFFER_SIZE - strlen(response_headers) - 1);
    }

    // Add final CRLFCRLF to terminate headers
    strncat(response_headers, "\r\n", BUFFER_SIZE - strlen(response_headers) - 1);
    header_len = strlen(response_headers);

    // Send response headers
    if (send(client, response_headers, header_len, 0) == SOCKET_ERROR) {
        printf("send headers failed: %d\n", WSAGetLastError());
    }

    // Send response body (not for HEAD requests)
    if (method && strcmp(method, "HEAD") != 0 && response_body && response_body_size > 0) {
        if (send(client, response_body, response_body_size, 0) == SOCKET_ERROR) {
            printf("send body failed: %d\n", WSAGetLastError());
        }
    }

    // Cleanup
    for (int i = 0; i < header_count; i++) {
        free(headers[i].name);
        free(headers[i].value);
    }
    
    free(request_copy);
    free(response_headers);
    free(response_body);
    free(request_body);
    if (filepath && filepath != uri + 1) {
        free(filepath);
    }
    free(buffer);
    closesocket(client);

    return NULL;
}

int main(int argc , char *argv[])
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData)) {
        printf("WSAStartup failed with error: %d\n", WSAGetLastError());
        return 1;
    }

    int result;
    SOCKET sock;
    struct sockaddr_in6 server_addr = {0};

    sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        printf("socket() failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
    wprintf(L"socket function succeeded\n");

    server_addr.sin6_family = AF_INET6;
    server_addr.sin6_port = htons(8000);
    if (inet_pton(AF_INET6, "::1", &server_addr.sin6_addr) != 1){
        printf("inet_pton failed: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    result = bind(sock, (SOCKADDR *) &server_addr, sizeof (server_addr));
    if (result == SOCKET_ERROR) {
        wprintf(L"bind failed with error %u\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }
    wprintf(L"bind returned success\n");

    if (listen(sock, SOMAXCONN) == SOCKET_ERROR)
        wprintf(L"listen function failed with error: %d\n", WSAGetLastError());
    
    wprintf(L"Listening on socket...\n");

    while(1){
        wprintf(L"Waiting for client to connect...\n");
        struct sockaddr_in6 client_addr = {0};
        socklen_t client_addr_len = sizeof(client_addr);
        SOCKET *client = malloc(sizeof(SOCKET));
        if (!client) {
            wprintf(L"Memory allocation failed\n");
            closesocket(sock);
            WSACleanup();
            return 1;
        }

        if ((*client = accept(sock, (struct sockaddr *)&client_addr, &client_addr_len)) == INVALID_SOCKET) {
            wprintf(L"accept failed with error: %d\n", WSAGetLastError());
            free(client);
            closesocket(sock);
            WSACleanup();
            return 1;
        }
        wprintf(L"Client connected.\n");

        pthread_t thread1;
        pthread_create(&thread1, NULL, handle_request, (void*)client);
        pthread_detach(thread1);
    }

    result = closesocket(sock);
    if (result == SOCKET_ERROR) {
        wprintf(L"closesocket failed with error = %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
    
    WSACleanup();
	return 0;
}