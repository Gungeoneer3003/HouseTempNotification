//Statement of Purpose:
/*
The purpose of this file is to provide the implementation for starting
and running the logger web server. Socket details live behind the portable
socket layer so the same server lifecycle works on POSIX and Windows.
*/

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "loggerWeb.h"
#include "loggerWebInternal.h"

#include <stdint.h>
#include <stdio.h>

#ifdef _WIN32
#include <process.h>
static unsigned __stdcall serverLoop(void* arg);
#else
static void* serverLoop(void* arg);
#endif

int loggerWebStartServer(LoggerWebServer* server,
                         const char* bind_address,
                         unsigned short port) {
    if (!server || port == 0) {
        return 0;
    }

    if (!portableSocketStartup()) {
        fprintf(stderr, "Failed to initialize logger web sockets: %s\n",
                portableSocketLastError());
        return 0;
    }

    server->port = port;
    server->server_socket = portableSocketCreateTcp();
    if (!portableSocketIsValid(server->server_socket)) {
        fprintf(stderr, "Failed to create logger web socket: %s\n",
                portableSocketLastError());
        portableSocketCleanup();
        return 0;
    }

    portableSocketSetReuseAddr(server->server_socket);

    if (!portableSocketBind(server->server_socket, bind_address, port)) {
        fprintf(stderr,
                "Failed to bind logger web viewer on %s:%u: %s\n",
                bind_address ? bind_address : "0.0.0.0",
                (unsigned)port,
                portableSocketLastError());
        portableSocketClose(&server->server_socket);
        portableSocketCleanup();
        return 0;
    }

    if (!portableSocketListen(server->server_socket, LOGGER_WEB_BACKLOG)) {
        fprintf(stderr, "Failed to listen for logger web viewer: %s\n",
                portableSocketLastError());
        portableSocketClose(&server->server_socket);
        portableSocketCleanup();
        return 0;
    }

#ifdef _WIN32
    uintptr_t thread = _beginthreadex(NULL, 0, serverLoop, server, 0, NULL);
    if (thread == 0) {
        fprintf(stderr, "Failed to start logger web viewer thread\n");
        portableSocketClose(&server->server_socket);
        portableSocketCleanup();
        return 0;
    }

    server->thread = (HANDLE)thread;
#else
    if (pthread_create(&server->thread, NULL, serverLoop, server) != 0) {
        fprintf(stderr, "Failed to start logger web viewer thread\n");
        portableSocketClose(&server->server_socket);
        portableSocketCleanup();
        return 0;
    }
#endif

    server->thread_started = 1;
    return 1;
}

void loggerWebStopServer(LoggerWebServer* server) {
    if (!server) {
        return;
    }

    server->stop_requested = 1;
    portableSocketClose(&server->server_socket);

    if (server->thread_started) {
#ifdef _WIN32
        WaitForSingleObject(server->thread, INFINITE);
        CloseHandle(server->thread);
        server->thread = NULL;
#else
        pthread_join(server->thread, NULL);
#endif
        server->thread_started = 0;
    }

    portableSocketCleanup();
}

#ifdef _WIN32
static unsigned __stdcall serverLoop(void* arg)
#else
static void* serverLoop(void* arg)
#endif
{
    LoggerWebServer* server = (LoggerWebServer*)arg;

    while (server && !server->stop_requested) {
        PortableSocket client_fd = portableSocketAccept(server->server_socket);
        if (!portableSocketIsValid(client_fd)) {
            if (server->stop_requested) {
                break;
            }
            continue;
        }

        loggerWebHandleClient(client_fd, server);
        portableSocketClose(&client_fd);
    }

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}
