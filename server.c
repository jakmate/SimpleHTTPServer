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

#define BUFFER_SIZE 512
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
    const char* ext = strrchr(filepath, '.');
    if (!ext || strcmp(ext, ".txt") == 0) return "text/plain";
    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".gif") == 0) return "image/gif";
    return "application/octet-stream";
}

static void* handle_request(void* arg) {
    printf("Current thread ID: %lu\n", (unsigned long)pthread_self());

    SOCKET client = *((SOCKET*)arg);
    free(arg);

    char* buffer = (char*)malloc(BUFFER_SIZE * sizeof(char) + 1);
    if (!buffer) {
        closesocket(client);
        return NULL;
    }

    int total = 0, received;

    while((received = recv(client, buffer + total, BUFFER_SIZE - total, 0)) > 0){
        total += received;
        buffer[received] = '\0';
        if (strstr(buffer, "\r\n\r\n"))
            break;
    }

    if (received == SOCKET_ERROR) {
        printf("recv failed: %d\n", WSAGetLastError());
        free(buffer);
        closesocket(client);
        return NULL;
    } else if (received == 0) {
        printf("client closed connection\n");
    } else {
        printf("Full message (%d bytes):\n%s\n", total, buffer);
    }

    header_t *headers = NULL;
    size_t header_count = 0, header_cap = 0;
    const char* method = strtok(buffer, " ");
    char* path = strtok(NULL, " ");
    const char* filepath = path;
    if (filepath && filepath[0] == '/') {
        filepath++;
    }
    const char* version = strtok(NULL, "\r\n");
    
    char* line = strtok(NULL, "\r\n");
    while (line && *line) {
        char* colon = strchr(line, ':');
        if (!colon) break;
        *colon = '\0';
        char* name  = line;
        char* value = colon + 1;
        while (*value == ' ') value++;

        if (header_count == header_cap) {
            size_t new_cap = header_cap ? header_cap * 2 : 4;
            header_t *new_headers = realloc(headers, new_cap * sizeof(header_t));
            if (!new_headers) {
                free(buffer);
                closesocket(client);
                return NULL;
            }
            headers = new_headers;
            header_cap = new_cap;
        }
        headers[header_count++] = (header_t){
            strdup(name),
            strdup(value)
        };

        line = strtok(NULL, "\r\n");
    }

    int code = 200;
    if (method && (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0 && strcmp(method, "POST") != 0)) {
        code = 501;
    }

    FILE *fptr = NULL;
    if (filepath) {
        fptr = fopen(filepath, "rb");
    }

    char* bodyBuffer = NULL;
    int bodySize = 0;

    if (fptr == NULL){
        code = 404;
        bodyBuffer = strdup("<html><body><h1>404 Not Found</h1><p>The requested file was not found.</p></body></html>");
        if (bodyBuffer) {
            bodySize = strlen(bodyBuffer);
        }
    }
    else {
        fseek(fptr, 0L, SEEK_END);
        bodySize = ftell(fptr);
        fseek(fptr, 0L, SEEK_SET);
        
        if (bodySize > 0) {
            bodyBuffer = malloc(bodySize + 1);
            if (bodyBuffer != NULL) {
                fread(bodyBuffer, 1, bodySize, fptr);
                bodyBuffer[bodySize] = '\0';
                //printf("%s", bodyBuffer);
            }
        }
        fclose(fptr);
    }

    // Date Header
    char* timeBuffer = (char*)malloc(sizeof(char) * 30);
    time_t now = time(NULL);
    struct tm* t = gmtime(&now);
    strftime(timeBuffer, 30, "%a, %d %b %Y %H:%M:%S GMT", t);

    // Modification Header
    struct stat attr;
    char *modificationBuffer = (char*)malloc(sizeof(char) * 30);
    if (stat(filepath, &attr) == 0) {
        struct tm *timeinfo = gmtime(&attr.st_mtime);
        strftime(modificationBuffer, 30, "%a, %d %b %Y %H:%M:%S GMT", timeinfo);
    } else {
        perror("stat");
    }

    char* headers_str = malloc(sizeof(char) * BUFFER_SIZE);
    int header_len = snprintf(headers_str, BUFFER_SIZE, 
        "%s %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Date: %s\r\n"
        "Server: %s\r\n"
        "Last-Modified: %s\r\n"
        "\r\n", 
        version, code, get_status_message(code), get_content_type(filepath), bodySize, timeBuffer, "MyBadHTTPServer", modificationBuffer);

    if (send(client, headers_str, header_len, 0) == SOCKET_ERROR) {
        printf("send failed: %d\n", WSAGetLastError());
        WSACleanup();
    }

    free(timeBuffer);
    free(modificationBuffer);
    free(headers_str);
    free(headers);

    if (method && strcmp(method, "HEAD") != 0){
        if (bodyBuffer != NULL && bodySize > 0) {
            if (send(client, bodyBuffer, bodySize, 0) == SOCKET_ERROR) {
                printf("send failed: %d\n", WSAGetLastError());
            }
        }
    }

    free(bodyBuffer);
    closesocket(client);
    free(buffer);

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