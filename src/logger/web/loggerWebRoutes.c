//Statement of Purpose:
/*
The purpose of this file is to provide the implementation for
handling web requests for the logger web interface. It includes functions to 
parse incoming HTTP requests, determine the requested resource, and send the 
appropriate response back to the client.
*/

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L

#include "loggerWeb.h"
#include "loggerWebInternal.h"
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

static void sendRoot(int client_fd, const LoggerWebServer* server);

//Parse the request path from the HTTP request string
static int parseRequestPath(const char* request, char* path, size_t path_size) {
    if (!request || !path || path_size == 0 || strncmp(request, "GET ", 4) != 0) {
        return 0;
    }

    const char* start = request + 4;
    if (*start != '/') {
        return 0;
    }

    const char* end = start;
    while (*end && *end != ' ' && *end != '?' && *end != '\r' && *end != '\n') {
        end++;
    }

    size_t length = (size_t)(end - start);
    if (length == 0 || length >= path_size) {
        return 0;
    }

    memcpy(path, start, length);
    path[length] = '\0';
    return 1;
}

//Check if the given path matches the expected path, allowing for an optional trailing slash
static int pathEquals(const char* path, const char* expected) {
    if (!path || !expected) {
        return 0;
    }

    if (strcmp(path, expected) == 0) {
        return 1;
    }

    size_t expected_length = strlen(expected);
    if (expected_length == 0 || strcmp(expected, "/") == 0) {
        return 0;
    }

    return strncmp(path, expected, expected_length) == 0 &&
           path[expected_length] == '/' &&
           path[expected_length + 1] == '\0';
}

//Handle an incoming client connection and respond to the HTTP request
void loggerWebHandleClient(int client_fd, const LoggerWebServer* server) {
    char request[1024];
    ssize_t bytes = recv(client_fd, request, sizeof(request) - 1, 0);
    if (bytes <= 0) {
        return;
    }

    //Null-terminate the request string so we can safely use string functions on it
    request[bytes] = '\0';

    char path[LOGGER_WEB_MAX_PATH];
    if (!parseRequestPath(request, path, sizeof(path))) {
        loggerWebSendNotFound(client_fd);
        return;
    }

    //Check the request path and respond accordingly
    if (pathEquals(path, "/")) {
        sendRoot(client_fd, server);
    } else if (pathEquals(path, "/log")) {
        loggerWebSendIndex(client_fd, server);
    } else if (pathEquals(path, "/graphs/data") ||
               (loggerWebRootDirectoryEquals(server, LOGGER_WEB_ROOT_GRAPHS) &&
                pathEquals(path, "/data"))) {
        loggerWebSendGraphData(client_fd, server, loggerWebParseGraphRange(request));
    } else if (pathEquals(path, "/graphs")) {
        loggerWebSendGraphs(client_fd, server);
    } else if (pathEquals(path, "/raw")) {
        loggerWebSendRawLog(client_fd, server);
    } else if (pathEquals(path, "/css/loggerWeb.css") ||
               pathEquals(path, "/style.css")) {
        loggerWebSendCss(client_fd);
    } else if (pathEquals(path, "/js/loggerWebGraph.js") ||
               pathEquals(path, "/loggerWebGraph.js")) {
        loggerWebSendGraphScript(client_fd);
    } else {
        loggerWebSendNotFound(client_fd);
    }
}

//Send the root page to the client, which is either the index or graphs page based on the server's root directory
static void sendRoot(int client_fd, const LoggerWebServer* server) {
    if (loggerWebRootDirectoryEquals(server, LOGGER_WEB_ROOT_GRAPHS)) {
        loggerWebSendGraphs(client_fd, server);
        return;
    }

    loggerWebSendIndex(client_fd, server);
}


#endif
