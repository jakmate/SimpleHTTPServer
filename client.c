#if defined(_MSC_VER)
#pragma comment(lib,"Ws2_32.lib")
#endif

#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define DEFAULT_BUFLEN 512

int main(int argc , char *argv[])
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData)) {
        printf("WSAStartup failed with error: %d\n", WSAGetLastError());
        return 1;
    }

    int result;
    SOCKET sock = INVALID_SOCKET;
    struct sockaddr_in6 client_addr = {0};

    int recvbuflen = DEFAULT_BUFLEN;
    char *sendbuf = "HEAD /index.html HTTP/1.0\r\n"
                    "Host: www.example.com\r\n"
                    "User-Agent: MySimpleClient/1.0\r\n"
                    "Accept-encoding: text/html\r\n"
                    "Authorization: haha\r\n"
                    "If-Modified-Since: 01-01-0001 01:01:01\r\n"
                    "\r\n";
    char recvbuf[DEFAULT_BUFLEN] = "";

    sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        printf("socket() failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
    wprintf(L"socket function succeeded\n");

    client_addr.sin6_family = AF_INET6;
    client_addr.sin6_port = htons(8000);
    if (inet_pton(AF_INET6, "::1", &client_addr.sin6_addr) != 1){
        printf("inet_pton failed: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    result = connect(sock, (SOCKADDR *) & client_addr, sizeof (client_addr));
    if (result == SOCKET_ERROR) {
        wprintf(L"connect function failed with error: %ld\n", WSAGetLastError());
        result = closesocket(sock);
        if (result == SOCKET_ERROR)
            wprintf(L"closesocket function failed with error: %ld\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    result = send(sock, sendbuf, (int)strlen(sendbuf), 0);
    if (result == SOCKET_ERROR) {
        wprintf(L"send failed with error: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    result = shutdown(sock, SD_SEND);
    if (result == SOCKET_ERROR) {
        wprintf(L"shutdown failed with error: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    char* buffer = (char*)malloc(4096);
    int totalBytes = 0;

    do {
        result = recv(sock, recvbuf, recvbuflen - 1, 0);
        if (result > 0) {
            if (totalBytes + result < 4095) {
                memcpy(buffer + totalBytes, recvbuf, result);
                totalBytes += result;
            }
        }
        else if (result == 0) {
            printf("Connection closed\n");
        }
        else {
            printf("recv failed with error: %d\n", WSAGetLastError());
        }
    } while (result > 0);

    buffer[totalBytes] = '\0';
    printf("Full response (%d bytes):\n%s\n", totalBytes, buffer);

    result = shutdown(sock, SD_RECEIVE);
    if (result == SOCKET_ERROR) {
        wprintf(L"shutdown failed with error: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    result = closesocket(sock);
    if (result == SOCKET_ERROR) {
        wprintf(L"close failed with error: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
    
    free(buffer);
    WSACleanup();
	return 0;
}