#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "portable_socket.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
static int winsock_started = 0;
#else
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

int portableSocketStartup(void) {
#ifdef _WIN32
    if (winsock_started) {
        winsock_started++;
        return 1;
    }

    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return 0;
    }

    winsock_started = 1;
#endif
    return 1;
}

void portableSocketCleanup(void) {
#ifdef _WIN32
    if (winsock_started <= 0) {
        return;
    }

    winsock_started--;
    if (winsock_started == 0) {
        WSACleanup();
    }
#endif
}

PortableSocket portableSocketCreateTcp(void) {
    return socket(AF_INET, SOCK_STREAM, 0);
}

int portableSocketSetReuseAddr(PortableSocket socket_fd) {
    int reuse = 1;
#ifdef _WIN32
    return setsockopt(socket_fd,
                      SOL_SOCKET,
                      SO_REUSEADDR,
                      (const char*)&reuse,
                      sizeof(reuse)) == 0;
#else
    return setsockopt(socket_fd,
                      SOL_SOCKET,
                      SO_REUSEADDR,
                      &reuse,
                      sizeof(reuse)) == 0;
#endif
}

int portableSocketBind(PortableSocket socket_fd,
                       const char* bind_address,
                       unsigned short port) {
    char port_text[16];
    snprintf(port_text, sizeof(port_text), "%u", (unsigned)port);

    int bind_any = !bind_address ||
                   !*bind_address ||
                   strcmp(bind_address, "*") == 0 ||
                   strcmp(bind_address, "0.0.0.0") == 0;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = bind_any ? AI_PASSIVE : 0;

    struct addrinfo* addresses = NULL;
    int resolved = getaddrinfo(bind_any ? NULL : bind_address,
                               port_text,
                               &hints,
                               &addresses);
    if (resolved != 0) {
        return 0;
    }

    int ok = 0;
    for (struct addrinfo* current = addresses; current; current = current->ai_next) {
        if (bind(socket_fd, current->ai_addr, (int)current->ai_addrlen) == 0) {
            ok = 1;
            break;
        }
    }

    freeaddrinfo(addresses);
    return ok;
}

int portableSocketListen(PortableSocket socket_fd, int backlog) {
    return listen(socket_fd, backlog) == 0;
}

PortableSocket portableSocketAccept(PortableSocket socket_fd) {
    return accept(socket_fd, NULL, NULL);
}

long portableSocketRecv(PortableSocket socket_fd, char* buffer, size_t length) {
#ifdef _WIN32
    if (length > (size_t)INT_MAX) {
        length = (size_t)INT_MAX;
    }
    return (long)recv(socket_fd, buffer, (int)length, 0);
#else
    return (long)recv(socket_fd, buffer, length, 0);
#endif
}

long portableSocketSend(PortableSocket socket_fd, const char* buffer, size_t length) {
#ifdef _WIN32
    if (length > (size_t)INT_MAX) {
        length = (size_t)INT_MAX;
    }
    return (long)send(socket_fd, buffer, (int)length, 0);
#else
    return (long)send(socket_fd, buffer, length, 0);
#endif
}

void portableSocketClose(PortableSocket* socket_fd) {
    if (!socket_fd || !portableSocketIsValid(*socket_fd)) {
        return;
    }

#ifdef _WIN32
    shutdown(*socket_fd, SD_BOTH);
    closesocket(*socket_fd);
#else
    shutdown(*socket_fd, SHUT_RDWR);
    close(*socket_fd);
#endif
    *socket_fd = PORTABLE_SOCKET_INVALID;
}

int portableSocketIsValid(PortableSocket socket_fd) {
    return socket_fd != PORTABLE_SOCKET_INVALID;
}

const char* portableSocketLastError(void) {
    static char message[128];

#ifdef _WIN32
    snprintf(message, sizeof(message), "WSA error %d", WSAGetLastError());
#else
    snprintf(message, sizeof(message), "%s", strerror(errno));
#endif

    return message;
}
