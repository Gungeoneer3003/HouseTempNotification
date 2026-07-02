#ifndef PORTABLE_SOCKET_H
#define PORTABLE_SOCKET_H

#include <stddef.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET PortableSocket;
#define PORTABLE_SOCKET_INVALID INVALID_SOCKET
#else
typedef int PortableSocket;
#define PORTABLE_SOCKET_INVALID (-1)
#endif

int portableSocketStartup(void);
void portableSocketCleanup(void);
PortableSocket portableSocketCreateTcp(void);
int portableSocketSetReuseAddr(PortableSocket socket_fd);
int portableSocketBind(PortableSocket socket_fd,
                       const char* bind_address,
                       unsigned short port);
int portableSocketListen(PortableSocket socket_fd, int backlog);
PortableSocket portableSocketAccept(PortableSocket socket_fd);
long portableSocketRecv(PortableSocket socket_fd, char* buffer, size_t length);
long portableSocketSend(PortableSocket socket_fd, const char* buffer, size_t length);
void portableSocketClose(PortableSocket* socket_fd);
int portableSocketIsValid(PortableSocket socket_fd);
const char* portableSocketLastError(void);

#endif
