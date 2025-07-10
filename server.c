#if defined(_MSC_VER)
#pragma comment(lib,"Ws2_32.lib")
#endif

#include <pthread.h>
#include <stdio.h>
#include <string.h>  
#include <winsock2.h>
#include <ws2tcpip.h>

#define BUFFER_SIZE 512
typedef struct {
  char *name;
  char *value;
} header_t;

header_t *headers = NULL;
size_t header_count = 0, header_cap = 0;

void* foo(void* arg) {
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

    const char * method = strtok(buffer, " ");
    char * path = strtok(NULL, " ");
    const char* filepath = path;
    if (filepath && filepath[0] == '/') {
        filepath++;
    }
    const char * version = strtok(NULL, "\r\n");
    
    char *line = strtok(NULL, "\r\n");
    while (line && *line) {
        char *colon = strchr(line, ':');
        if (!colon) break;
        *colon = '\0';
        char *name  = line;
        char *value = colon + 1;
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

    const char* status;
    int code;
    if (method && strcmp(method, "GET") == 0){
        status = "OK";
        code = 200;
    }
    else{
        status = "Method Not Allowed";
        code = 405;
    }

    FILE *fptr = NULL;
    if (filepath) {
        fptr = fopen(filepath, "rb");
    }

    char* bodyBuffer = NULL;
    int bodySize = 0;

    if (fptr == NULL){
        status = "Not Found";
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
                printf("%s", bodyBuffer);
            }
        }
        fclose(fptr);
    }

    char headers_str[512];
    int header_len = snprintf(headers_str, sizeof(headers_str), 
        "%s %d %s\r\n"
        "Content-Length: %d\r\n"
        "\r\n", 
        version, code, status, bodySize);

    if (send(client, headers_str, header_len, 0) == SOCKET_ERROR) {
        printf("send failed: %d\n", WSAGetLastError());
        WSACleanup();
    }

    if (bodyBuffer != NULL && bodySize > 0) {
        if (send(client, bodyBuffer, bodySize, 0) == SOCKET_ERROR) {
            printf("send failed: %d\n", WSAGetLastError());
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
        pthread_create(&thread1, NULL, foo, (void*)client);
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