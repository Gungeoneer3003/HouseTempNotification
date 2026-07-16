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
#include <stdlib.h>

#ifdef _WIN32
#include <process.h>
static unsigned __stdcall serverLoop(void* arg);
static unsigned __stdcall clientLoop(void* arg);
#else
static void* serverLoop(void* arg);
static void* clientLoop(void* arg);
#endif

typedef struct {
    LoggerWebServer* server;
    PortableSocket client_fd;
} LoggerWebClientJob;

static int initRequestSync(LoggerWebServer* server);
static void destroyRequestSync(LoggerWebServer* server);
static int startClient(LoggerWebServer* server, PortableSocket client_fd);
static void finishClient(LoggerWebServer* server);

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

    if (!initRequestSync(server)) {
        fprintf(stderr, "Failed to initialize logger web request synchronization\n");
        portableSocketCleanup();
        return 0;
    }

    server->port = port;
    server->server_socket = portableSocketCreateTcp();
    if (!portableSocketIsValid(server->server_socket)) {
        fprintf(stderr, "Failed to create logger web socket: %s\n",
                portableSocketLastError());
        destroyRequestSync(server);
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
        destroyRequestSync(server);
        portableSocketCleanup();
        return 0;
    }

    if (!portableSocketListen(server->server_socket, LOGGER_WEB_BACKLOG)) {
        fprintf(stderr, "Failed to listen for logger web viewer: %s\n",
                portableSocketLastError());
        portableSocketClose(&server->server_socket);
        destroyRequestSync(server);
        portableSocketCleanup();
        return 0;
    }

#ifdef _WIN32
    uintptr_t thread = _beginthreadex(NULL, 0, serverLoop, server, 0, NULL);
    if (thread == 0) {
        fprintf(stderr, "Failed to start logger web viewer thread\n");
        portableSocketClose(&server->server_socket);
        destroyRequestSync(server);
        portableSocketCleanup();
        return 0;
    }

    server->thread = (HANDLE)thread;
#else
    if (pthread_create(&server->thread, NULL, serverLoop, server) != 0) {
        fprintf(stderr, "Failed to start logger web viewer thread\n");
        portableSocketClose(&server->server_socket);
        destroyRequestSync(server);
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

    loggerWebMutexLock(&server->client_mutex);
    while (server->active_client_count > 0) {
        loggerWebConditionWait(&server->clients_done, &server->client_mutex);
    }
    loggerWebMutexUnlock(&server->client_mutex);
    destroyRequestSync(server);

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

        if (!startClient(server, client_fd)) {
            // Fall back to inline handling if a worker cannot be allocated or started.
            loggerWebHandleClient(client_fd, server);
            portableSocketClose(&client_fd);
        }
    }

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static int initRequestSync(LoggerWebServer* server) {
    if (!server || !loggerWebMutexInit(&server->client_mutex)) {
        return 0;
    }

    if (!loggerWebConditionInit(&server->clients_done)) {
        loggerWebMutexDestroy(&server->client_mutex);
        return 0;
    }

    if (!loggerWebMutexInit(&server->today_control_mutex)) {
        loggerWebConditionDestroy(&server->clients_done);
        loggerWebMutexDestroy(&server->client_mutex);
        return 0;
    }

    server->request_sync_initialized = 1;
    return 1;
}

static void destroyRequestSync(LoggerWebServer* server) {
    if (!server || !server->request_sync_initialized) {
        return;
    }

    loggerWebMutexDestroy(&server->today_control_mutex);
    loggerWebConditionDestroy(&server->clients_done);
    loggerWebMutexDestroy(&server->client_mutex);
    server->request_sync_initialized = 0;
}

static int startClient(LoggerWebServer* server, PortableSocket client_fd) {
    LoggerWebClientJob* job = malloc(sizeof(*job));
    if (!job) {
        return 0;
    }

    job->server = server;
    job->client_fd = client_fd;

    loggerWebMutexLock(&server->client_mutex);
    server->active_client_count++;
    loggerWebMutexUnlock(&server->client_mutex);

#ifdef _WIN32
    uintptr_t thread = _beginthreadex(NULL, 0, clientLoop, job, 0, NULL);
    if (thread != 0) {
        CloseHandle((HANDLE)thread);
        return 1;
    }
#else
    pthread_t thread;
    if (pthread_create(&thread, NULL, clientLoop, job) == 0) {
        pthread_detach(thread);
        return 1;
    }
#endif

    finishClient(server);
    free(job);
    return 0;
}

#ifdef _WIN32
static unsigned __stdcall clientLoop(void* arg)
#else
static void* clientLoop(void* arg)
#endif
{
    LoggerWebClientJob* job = (LoggerWebClientJob*)arg;
    LoggerWebServer* server = job->server;
    PortableSocket client_fd = job->client_fd;
    free(job);

    loggerWebHandleClient(client_fd, server);
    portableSocketClose(&client_fd);
    finishClient(server);

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static void finishClient(LoggerWebServer* server) {
    loggerWebMutexLock(&server->client_mutex);
    if (server->active_client_count > 0) {
        server->active_client_count--;
    }
    if (server->active_client_count == 0) {
        loggerWebConditionWakeAll(&server->clients_done);
    }
    loggerWebMutexUnlock(&server->client_mutex);
}
