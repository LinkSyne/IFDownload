#include "tunnel.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketType = SOCKET;
#define IS_VALIDSOCK(s) ((s) != INVALID_SOCKET)
#define CLOSESOCK(s) closesocket(s)
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
using SocketType = int;
#define IS_VALIDSOCK(s) ((s) >= 0)
#define CLOSESOCK(s) close(s)
#endif

static std::atomic<bool> gRunning{false};
static SocketType gServerSock = -1;
static std::thread gServerThread;

static void forwardClient(SocketType clientSock) {
    char reqBuf[4096] = {};
    int totalRead = 0;
    while (totalRead < (int)sizeof(reqBuf) - 1) {
        int rd = recv(clientSock, reqBuf + totalRead, (int)sizeof(reqBuf) - 1 - totalRead, 0);
        if (rd <= 0) {
            CLOSESOCK(clientSock);
            return;
        }
        totalRead += rd;
        if (strstr(reqBuf, "\r\n\r\n")) break;
    }

    if (strncmp(reqBuf, "CONNECT ", 8) != 0) {
        CLOSESOCK(clientSock);
        return;
    }

    char* targetStart = reqBuf + 8;
    char* targetEnd = strchr(targetStart, ' ');
    if (!targetEnd) {
        CLOSESOCK(clientSock);
        return;
    }
    *targetEnd = '\0';

    char* colon = strchr(targetStart, ':');
    if (!colon) {
        CLOSESOCK(clientSock);
        return;
    }
    *colon = '\0';
    std::string host = targetStart;
    int port = atoi(colon + 1);

    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    char portStr[16];
    snprintf(portStr, sizeof(portStr), "%d", port);

    if (getaddrinfo(host.c_str(), portStr, &hints, &res) != 0 || !res) {
        CLOSESOCK(clientSock);
        return;
    }

    SocketType targetSock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (!IS_VALIDSOCK(targetSock)) {
        freeaddrinfo(res);
        CLOSESOCK(clientSock);
        return;
    }

    int opt = 1;
    setsockopt(targetSock, IPPROTO_TCP, TCP_NODELAY, (const char*)&opt, sizeof(opt));

    if (connect(targetSock, res->ai_addr, (int)res->ai_addrlen) != 0) {
        freeaddrinfo(res);
        CLOSESOCK(targetSock);
        CLOSESOCK(clientSock);
        return;
    }
    freeaddrinfo(res);

    const char* resp200 = "HTTP/1.1 200 Connection Established\r\n\r\n";
    send(clientSock, resp200, (int)strlen(resp200), 0);

    char helloBuf[4096];
    int helloLen = recv(clientSock, helloBuf, sizeof(helloBuf), 0);
    if (helloLen > 2) {
        send(targetSock, helloBuf, 2, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        send(targetSock, helloBuf + 2, helloLen - 2, 0);
    } else if (helloLen > 0) {
        send(targetSock, helloBuf, helloLen, 0);
    }

    int sockBuf = 1048576;
    setsockopt(targetSock, SOL_SOCKET, SO_RCVBUF, (const char*)&sockBuf, sizeof(sockBuf));
    setsockopt(targetSock, SOL_SOCKET, SO_SNDBUF, (const char*)&sockBuf, sizeof(sockBuf));
    setsockopt(clientSock, SOL_SOCKET, SO_RCVBUF, (const char*)&sockBuf, sizeof(sockBuf));
    setsockopt(clientSock, SOL_SOCKET, SO_SNDBUF, (const char*)&sockBuf, sizeof(sockBuf));

    char buf[65536];
    while (gRunning) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(clientSock, &fds);
        FD_SET(targetSock, &fds);

        SocketType maxFd = clientSock > targetSock ? clientSock : targetSock;
        struct timeval tv = { 5, 0 };
        int sel = select((int)(maxFd + 1), &fds, nullptr, nullptr, &tv);
        if (sel <= 0) break;

        if (FD_ISSET(clientSock, &fds)) {
            int r = recv(clientSock, buf, sizeof(buf), 0);
            if (r <= 0) break;
            send(targetSock, buf, r, 0);
        }
        if (FD_ISSET(targetSock, &fds)) {
            int r = recv(targetSock, buf, sizeof(buf), 0);
            if (r <= 0) break;
            send(clientSock, buf, r, 0);
        }
    }

    CLOSESOCK(targetSock);
    CLOSESOCK(clientSock);
}

static void serverLoop() {
    while (gRunning) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(gServerSock, &fds);
        struct timeval tv = { 1, 0 };
        int sel = select((int)(gServerSock + 1), &fds, nullptr, nullptr, &tv);
        if (sel > 0 && FD_ISSET(gServerSock, &fds)) {
            sockaddr_in clientAddr = {};
#ifdef _WIN32
            int clientLen = sizeof(clientAddr);
#else
            socklen_t clientLen = sizeof(clientAddr);
#endif
            SocketType clientSock = accept(gServerSock, (sockaddr*)&clientAddr, &clientLen);
            if (IS_VALIDSOCK(clientSock)) {
                std::thread(forwardClient, clientSock).detach();
            }
        }
    }
}

bool startTunnel(int& outPort) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif

    gServerSock = socket(AF_INET, SOCK_STREAM, 0);
    if (!IS_VALIDSOCK(gServerSock)) return false;

    int opt = 1;
#ifndef _WIN32
    setsockopt(gServerSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(gServerSock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        CLOSESOCK(gServerSock);
        return false;
    }

#ifdef _WIN32
    int len = sizeof(addr);
#else
    socklen_t len = sizeof(addr);
#endif
    if (getsockname(gServerSock, (sockaddr*)&addr, &len) != 0) {
        CLOSESOCK(gServerSock);
        return false;
    }

    if (listen(gServerSock, 16) != 0) {
        CLOSESOCK(gServerSock);
        return false;
    }

    outPort = ntohs(addr.sin_port);
    gRunning = true;
    gServerThread = std::thread(serverLoop);
    return true;
}

void stopTunnel() {
    if (!gRunning) return;
    gRunning = false;
    if (IS_VALIDSOCK(gServerSock)) {
        CLOSESOCK(gServerSock);
        gServerSock = -1;
    }
    if (gServerThread.joinable()) {
        gServerThread.join();
    }
}
