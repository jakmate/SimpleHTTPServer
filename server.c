#if defined(_MSC_VER)
#pragma comment(lib,"Ws2_32.lib")
#endif

#include <pthread.h>
#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define BUFFER_SIZE 512

void* foo(void* arg) {
    printf("Created a new thread\n");
    pthread_t thisThread = pthread_self();
    printf("Current thread ID: %lu\n", (unsigned long)thisThread);

    int client = *((int*)arg);
    char* buffer = (char*)malloc(BUFFER_SIZE * sizeof(char));
    int received = recv(client, buffer, BUFFER_SIZE, 0);
    if ( received > 0 )
        wprintf(L"Bytes received: %d\n", received);
    else if ( received == 0 )
        wprintf(L"Connection closed\n");
    else
        wprintf(L"recv failed with error: %d\n", WSAGetLastError());

    char* message = "message";
    if (send(client, message, (int)strlen(message), 0) == SOCKET_ERROR) {
        printf("send failed: %d\n", WSAGetLastError());
        closesocket(client);
        WSACleanup();
    }
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
    if (sock == 0) {
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