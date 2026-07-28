//Statement of Purpose:
/*
The purpose of this file is to provide the implementation for
handling web requests for the logger web interface. It includes functions to
parse incoming HTTP requests, determine the requested resource, and send the
appropriate response back to the client.
*/

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "loggerWeb.h"
#include "loggerWebInternal.h"

#include <stdlib.h>
#include <string.h>

static void sendRoot(PortableSocket client_fd,
                     const LoggerWebServer* server,
                     size_t log_limit);
static void pollForPageAccess(LoggerWebServer* server, int is_root);
static int parseRequest(const char* request,
                        char* method,
                        size_t method_size,
                        char* path,
                        size_t path_size);
static int pathEquals(const char* path, const char* expected);
static size_t parseLogLimit(const char* request, const LoggerWebServer* server);
static int requestAuthorized(const char* request, const LoggerWebServer* server);
static int queryParamEquals(const char* request, const char* key, const char* value);
static int headerBearerEquals(const char* request, const char* token);
static int headerTokenEquals(const char* request, const char* token);
static int cookieTokenEquals(const char* request, const char* token);
static int tokenValueEquals(const char* candidate,
                            size_t candidate_length,
                            const char* token);

static int parseRequest(const char* request,
                        char* method,
                        size_t method_size,
                        char* path,
                        size_t path_size) {
    if (!request || !method || method_size == 0 || !path || path_size == 0) {
        return 0;
    }

    const char* method_end = strchr(request, ' ');
    if (!method_end || method_end == request) {
        return 0;
    }

    size_t method_length = (size_t)(method_end - request);
    if (method_length >= method_size) {
        return 0;
    }

    memcpy(method, request, method_length);
    method[method_length] = '\0';

    const char* start = method_end + 1;
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

void loggerWebHandleClient(PortableSocket client_fd, LoggerWebServer* server) {
    char request[1024];
    long bytes = portableSocketRecv(client_fd, request, sizeof(request) - 1);
    if (bytes <= 0) {
        return;
    }

    request[bytes] = '\0';

    char method[8];
    char path[LOGGER_WEB_MAX_PATH];
    if (!parseRequest(request, method, sizeof(method), path, sizeof(path))) {
        loggerWebSendNotFound(client_fd);
        return;
    }

    if (!requestAuthorized(request, server)) {
        loggerWebSendUnauthorized(client_fd);
        return;
    }

    if (strcmp(method, "POST") == 0) {
        if (pathEquals(path, "/today/fan/speed/up")) {
            loggerWebHandleTodayControl(client_fd, server, "speed-up");
        } else if (pathEquals(path, "/today/fan/speed/down")) {
            loggerWebHandleTodayControl(client_fd, server, "speed-down");
        } else if (pathEquals(path, "/today/fan/power/toggle")) {
            loggerWebHandleTodayControl(client_fd, server, "power-toggle");
        } else {
            loggerWebSendNotFound(client_fd);
        }
        return;
    }

    if (strcmp(method, "GET") != 0) {
        loggerWebSendNotFound(client_fd);
        return;
    }

    size_t log_limit = parseLogLimit(request, server);

    if (pathEquals(path, "/")) {
        pollForPageAccess(server, 1);
        sendRoot(client_fd, server, log_limit);
    } else if (pathEquals(path, "/log")) {
        pollForPageAccess(server, 0);
        loggerWebSendIndex(client_fd, server, 0, log_limit);
    } else if (pathEquals(path, "/graphs/data") ||
               (loggerWebRootDirectoryEquals(server, LOGGER_WEB_ROOT_GRAPHS) &&
                pathEquals(path, "/data"))) {
        loggerWebSendGraphData(client_fd,
                               server,
                               loggerWebParseGraphRange(request),
                               request);
    } else if (pathEquals(path, "/graphs")) {
        pollForPageAccess(server, 0);
        loggerWebSendGraphs(client_fd, server, 0);
    } else if (pathEquals(path, "/raw")) {
        pollForPageAccess(server, 0);
        loggerWebSendRawLog(client_fd, server, 0);
    } else if (pathEquals(path, "/css/loggerWeb.css") ||
               pathEquals(path, "/style.css")) {
        loggerWebSendCss(client_fd);
    } else if (pathEquals(path, "/js/loggerWebGraph.js") ||
               pathEquals(path, "/loggerWebGraph.js")) {
        loggerWebSendGraphScript(client_fd);
    } else if (pathEquals(path, "/js/loggerWebToday.js") ||
               pathEquals(path, "/loggerWebToday.js")) {
        loggerWebSendTodayScript(client_fd);
    } else {
        loggerWebSendNotFound(client_fd);
    }
}

static void sendRoot(PortableSocket client_fd,
                     const LoggerWebServer* server,
                     size_t log_limit) {
    if (loggerWebRootDirectoryEquals(server, LOGGER_WEB_ROOT_GRAPHS)) {
        loggerWebSendGraphs(client_fd, server, 1);
        return;
    }

    loggerWebSendIndex(client_fd, server, 1, log_limit);
}

static void pollForPageAccess(LoggerWebServer* server, int is_root) {
    LoggerWebAccessPoller poller = NULL;
    void* user = NULL;
    int mode = 0;

    loggerWebMutexLock(&active_server_mutex);
    if (server) {
        mode = server->access_poll_mode;
        poller = server->access_poller;
        user = server->access_poller_user;
    }
    loggerWebMutexUnlock(&active_server_mutex);

    if (!poller || mode == 0 || (mode == 1 && !is_root)) {
        return;
    }

    // Page-triggered sensor reads and fan commands share the house controller.
    // Keep those operations serialized while allowing unrelated web requests through.
    loggerWebMutexLock(&server->today_control_mutex);
    (void)poller(user);
    loggerWebMutexUnlock(&server->today_control_mutex);
}

static size_t parseLogLimit(const char* request, const LoggerWebServer* server) {
    size_t default_limit = server && server->log_row_limit
        ? server->log_row_limit
        : LOGGER_WEB_DEFAULT_LOG_LIMIT;

    const char* query = request ? strchr(request, '?') : NULL;
    if (!query) {
        return default_limit;
    }

    const char* query_end = strchr(query, ' ');
    if (!query_end) {
        query_end = query + strlen(query);
    }

    const char prefix[] = "limit=";
    const size_t prefix_length = sizeof(prefix) - 1;
    const char* cursor = query + 1;

    while (cursor < query_end) {
        const char* param_end = cursor;
        while (param_end < query_end && *param_end != '&') {
            param_end++;
        }

        size_t param_length = (size_t)(param_end - cursor);
        if (param_length > prefix_length &&
            strncmp(cursor, prefix, prefix_length) == 0) {
            char value[32];
            size_t value_length = param_length - prefix_length;
            if (value_length >= sizeof(value)) {
                return default_limit;
            }
            memcpy(value, cursor + prefix_length, value_length);
            value[value_length] = '\0';

            unsigned long parsed = strtoul(value, NULL, 10);
            if (parsed == 0) {
                return default_limit;
            }
            if (parsed > LOGGER_WEB_MAX_LOG_LIMIT) {
                return LOGGER_WEB_MAX_LOG_LIMIT;
            }
            return (size_t)parsed;
        }

        cursor = param_end;
        if (cursor < query_end && *cursor == '&') {
            cursor++;
        }
    }

    return default_limit;
}

static int requestAuthorized(const char* request, const LoggerWebServer* server) {
    if (!server || !server->auth_token[0]) {
        return 1;
    }

    return queryParamEquals(request, "token", server->auth_token) ||
           headerBearerEquals(request, server->auth_token) ||
           headerTokenEquals(request, server->auth_token) ||
           cookieTokenEquals(request, server->auth_token);
}

static int queryParamEquals(const char* request, const char* key, const char* value) {
    const char* query = request ? strchr(request, '?') : NULL;
    if (!query || !key || !value) {
        return 0;
    }

    const char* query_end = strchr(query, ' ');
    if (!query_end) {
        query_end = query + strlen(query);
    }

    size_t key_length = strlen(key);
    const char* cursor = query + 1;
    while (cursor < query_end) {
        const char* param_end = cursor;
        while (param_end < query_end && *param_end != '&') {
            param_end++;
        }

        if ((size_t)(param_end - cursor) > key_length + 1 &&
            strncmp(cursor, key, key_length) == 0 &&
            cursor[key_length] == '=') {
            const char* token = cursor + key_length + 1;
            return tokenValueEquals(token, (size_t)(param_end - token), value);
        }

        cursor = param_end;
        if (cursor < query_end && *cursor == '&') {
            cursor++;
        }
    }

    return 0;
}

static int headerBearerEquals(const char* request, const char* token) {
    const char* header = strstr(request ? request : "", "\r\nAuthorization: Bearer ");
    if (!header) {
        return 0;
    }

    const char* value = header + strlen("\r\nAuthorization: Bearer ");
    const char* end = strstr(value, "\r\n");
    if (!end) {
        return 0;
    }

    return tokenValueEquals(value, (size_t)(end - value), token);
}

static int headerTokenEquals(const char* request, const char* token) {
    const char* header = strstr(request ? request : "", "\r\nX-Logger-Token: ");
    if (!header) {
        return 0;
    }

    const char* value = header + strlen("\r\nX-Logger-Token: ");
    const char* end = strstr(value, "\r\n");
    if (!end) {
        return 0;
    }

    return tokenValueEquals(value, (size_t)(end - value), token);
}

static int cookieTokenEquals(const char* request, const char* token) {
    const char* header = strstr(request ? request : "", "\r\nCookie: ");
    if (!header) {
        return 0;
    }

    const char* cookie = strstr(header, "logger_web_token=");
    if (!cookie) {
        return 0;
    }

    const char* value = cookie + strlen("logger_web_token=");
    const char* end = value;
    while (*end && *end != ';' && *end != '\r' && *end != '\n') {
        end++;
    }

    return tokenValueEquals(value, (size_t)(end - value), token);
}

static int tokenValueEquals(const char* candidate,
                            size_t candidate_length,
                            const char* token) {
    return token &&
           strlen(token) == candidate_length &&
           strncmp(candidate, token, candidate_length) == 0;
}
