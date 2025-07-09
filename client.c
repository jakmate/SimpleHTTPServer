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
    char *sendbuf = "Client: sending data test";
    char recvbuf[DEFAULT_BUFLEN] = "";

    sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (sock == 0) {
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

    result = recv(sock, recvbuf, recvbuflen, 0);
    if ( result > 0 )
        wprintf(L"Bytes received: %d\n", result);
    else if ( result == 0 )
        wprintf(L"Connection closed\n");
    else
        wprintf(L"recv failed with error: %d\n", WSAGetLastError());

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
    
    WSACleanup();
	return 0;
}