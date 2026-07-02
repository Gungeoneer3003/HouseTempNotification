//Statement of Purpose
/*
The purpose of this file is to provide the public logger web facade.
It owns server initialization and high-level display settings; routing,
responses, assets, log rows, time helpers, and graph behavior live in
the neighboring web modules.
*/

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "loggerWeb.h"
#include "loggerWebInternal.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings.h"

//Logger web server state
LoggerWebServer* active_server = NULL;
LoggerWebMutex active_server_mutex = LOGGER_WEB_MUTEX_INITIALIZER;
static int stop_registered = 0;

static int initServerDisplay(LoggerWebServer* server,
                             const char* title,
                             const char* const* column_headers,
                             size_t column_header_count);
static void freeServerDisplay(LoggerWebServer* server);
static void freeServerNavLinks(LoggerWebServer* server);
static int normalizeRootDirectory(const char* subdirectory,
                                  char* output,
                                  size_t output_size);
static int supportedRootDirectory(const char* subdirectory);
static void copyRootDirectory(const LoggerWebServer* server,
                              char* output,
                              size_t output_size);
static const char* configLogPath(const LoggerWebConfig* config);
static size_t normalizeLogLimit(size_t limit);

//Start the logger web server on the specified port.
int loggerWebStart(const char* log_path,
                   unsigned short port,
                   const char* title,
                   const char* const* column_headers,
                   size_t column_header_count) {
    LoggerWebConfig config;
    memset(&config, 0, sizeof(config));
    config.log_path = log_path;
    config.port = port;
    config.bind_address = LOGGER_WEB_BIND_ADDRESS;
    config.auth_token = LOGGER_WEB_AUTH_TOKEN;
    config.title = title;
    config.column_headers = column_headers;
    config.column_header_count = column_header_count;
    config.log_row_limit = LOGGER_WEB_DEFAULT_LOG_LIMIT;
    return loggerWebStartWithConfig(&config);
}

int loggerWebStartWithConfig(const LoggerWebConfig* config) {
    const char* log_path = configLogPath(config);
    if (!config || !log_path || !*log_path || config->port == 0 ||
        !config->title || !*config->title ||
        (config->column_header_count > 0 && !config->column_headers) ||
        config->column_header_count > SIZE_MAX - 2) {
        return 0;
    }

    loggerWebMutexLock(&active_server_mutex);
    int already_started = active_server != NULL;
    loggerWebMutexUnlock(&active_server_mutex);
    if (already_started) {
        return 0;
    }

    LoggerWebServer* server = calloc(1, sizeof(*server));
    if (!server) {
        return 0;
    }
    server->server_socket = PORTABLE_SOCKET_INVALID;

    int n = snprintf(server->log_path, sizeof(server->log_path), "%s", log_path);
    if (n < 0 || (size_t)n >= sizeof(server->log_path)) {
        free(server);
        fprintf(stderr, "Log path is too long for web viewer\n");
        return 0;
    }

    const char* bind_address = config->bind_address && *config->bind_address
        ? config->bind_address
        : LOGGER_WEB_BIND_ADDRESS;
    n = snprintf(server->bind_address,
                 sizeof(server->bind_address),
                 "%s",
                 bind_address);
    if (n < 0 || (size_t)n >= sizeof(server->bind_address)) {
        free(server);
        fprintf(stderr, "Logger web bind address is too long\n");
        return 0;
    }

    const char* auth_token = config->auth_token ? config->auth_token : "";
    n = snprintf(server->auth_token,
                 sizeof(server->auth_token),
                 "%s",
                 auth_token);
    if (n < 0 || (size_t)n >= sizeof(server->auth_token)) {
        free(server);
        fprintf(stderr, "Logger web auth token is too long\n");
        return 0;
    }

    server->log_row_limit = normalizeLogLimit(config->log_row_limit);
    snprintf(server->root_directory,
             sizeof(server->root_directory),
             "%s",
             LOGGER_WEB_ROOT_LOG);

    if (!initServerDisplay(server,
                           config->title,
                           config->column_headers,
                           config->column_header_count)) {
        free(server);
        return 0;
    }

    if (!loggerWebStartServer(server, server->bind_address, config->port)) {
        freeServerDisplay(server);
        free(server);
        return 0;
    }

    loggerWebMutexLock(&active_server_mutex);
    active_server = server;
    loggerWebMutexUnlock(&active_server_mutex);

    if (!stop_registered) {
        atexit(loggerWebStop);
        stop_registered = 1;
    }

    printf("Logger web viewer listening on %s:%u\n",
           server->bind_address,
           (unsigned)config->port);
    return 1;
}

void loggerWebStop(void) {
    loggerWebMutexLock(&active_server_mutex);
    LoggerWebServer* server = active_server;
    active_server = NULL;
    if (server) {
        server->stop_requested = 1;
    }
    loggerWebMutexUnlock(&active_server_mutex);

    if (!server) {
        return;
    }

    loggerWebStopServer(server);
    freeServerDisplay(server);
    free(server);
}

int loggerWebSetRootDirectory(const char* subdirectory) {
    char normalized[LOGGER_WEB_ROOT_DIRECTORY_SIZE];

    if (!normalizeRootDirectory(subdirectory, normalized, sizeof(normalized)) ||
        !supportedRootDirectory(normalized)) {
        return 0;
    }

    loggerWebMutexLock(&active_server_mutex);
    LoggerWebServer* server = active_server;
    if (!server) {
        loggerWebMutexUnlock(&active_server_mutex);
        return 0;
    }

    snprintf(server->root_directory,
             sizeof(server->root_directory),
             "%s",
             normalized);

    loggerWebMutexUnlock(&active_server_mutex);
    return 1;
}

int loggerWebAddNavLink(const char* label, const char* href, int root_relative) {
    if (!label || !*label || !href || !*href) {
        return 0;
    }

    loggerWebMutexLock(&active_server_mutex);
    LoggerWebServer* server = active_server;
    if (!server) {
        loggerWebMutexUnlock(&active_server_mutex);
        return 0;
    }

    if (server->nav_link_count == server->nav_link_capacity) {
        size_t new_capacity = server->nav_link_capacity ? server->nav_link_capacity * 2 : 4;
        LoggerWebNavLink* new_links = realloc(server->nav_links,
                                              new_capacity * sizeof(*new_links));
        if (!new_links) {
            loggerWebMutexUnlock(&active_server_mutex);
            return 0;
        }

        server->nav_links = new_links;
        server->nav_link_capacity = new_capacity;
    }

    char* label_copy = loggerWebCopyString(label);
    char* href_copy = loggerWebCopyString(href);
    if (!label_copy || !href_copy) {
        free(label_copy);
        free(href_copy);
        loggerWebMutexUnlock(&active_server_mutex);
        return 0;
    }

    server->nav_links[server->nav_link_count].label = label_copy;
    server->nav_links[server->nav_link_count].href = href_copy;
    server->nav_links[server->nav_link_count].root_relative = root_relative != 0;
    server->nav_link_count++;

    loggerWebMutexUnlock(&active_server_mutex);
    return 1;
}

int loggerWebSetAccessPoller(int mode, LoggerWebAccessPoller poller, void* user) {
    if (mode < 0 || mode > 2) {
        return 0;
    }

    loggerWebMutexLock(&active_server_mutex);
    LoggerWebServer* server = active_server;
    if (!server) {
        loggerWebMutexUnlock(&active_server_mutex);
        return 0;
    }

    server->access_poll_mode = mode;
    server->access_poller = mode == 0 ? NULL : poller;
    server->access_poller_user = mode == 0 ? NULL : user;
    loggerWebMutexUnlock(&active_server_mutex);
    return 1;
}

int loggerWebSetAuthToken(const char* token) {
    loggerWebMutexLock(&active_server_mutex);
    LoggerWebServer* server = active_server;
    if (!server) {
        loggerWebMutexUnlock(&active_server_mutex);
        return 0;
    }

    const char* next_token = token ? token : "";
    int n = snprintf(server->auth_token,
                     sizeof(server->auth_token),
                     "%s",
                     next_token);
    loggerWebMutexUnlock(&active_server_mutex);
    return n >= 0 && (size_t)n < LOGGER_WEB_MAX_AUTH_TOKEN;
}

//Enable or disable the refresh button on the web interface.
int loggerWebShowRefreshButton(int enabled) {
    loggerWebMutexLock(&active_server_mutex);
    LoggerWebServer* server = active_server;
    if (!server) {
        loggerWebMutexUnlock(&active_server_mutex);
        return 0;
    }

    server->show_refresh_button = enabled != 0;
    loggerWebMutexUnlock(&active_server_mutex);
    return 1;
}

static int initServerDisplay(LoggerWebServer* server,
                             const char* title,
                             const char* const* column_headers,
                             size_t column_header_count) {
    server->title = loggerWebCopyString(title);
    if (!server->title) {
        return 0;
    }

    server->column_header_count = column_header_count;
    if (column_header_count == 0) {
        return 1;
    }

    server->column_headers = calloc(column_header_count, sizeof(*server->column_headers));
    if (!server->column_headers) {
        freeServerDisplay(server);
        return 0;
    }

    for (size_t i = 0; i < column_header_count; i++) {
        if (!column_headers[i]) {
            freeServerDisplay(server);
            return 0;
        }

        server->column_headers[i] = loggerWebCopyString(column_headers[i]);
        if (!server->column_headers[i]) {
            freeServerDisplay(server);
            return 0;
        }
    }

    return 1;
}

static void freeServerDisplay(LoggerWebServer* server) {
    if (!server) {
        return;
    }

    free(server->title);
    server->title = NULL;

    if (server->column_headers) {
        for (size_t i = 0; i < server->column_header_count; i++) {
            free(server->column_headers[i]);
        }
    }
    free(server->column_headers);
    server->column_headers = NULL;
    server->column_header_count = 0;

    loggerWebFreeGraphs(server);
    loggerWebFreeTodayColumns(server);
    freeServerNavLinks(server);
    server->show_stats = 0;
    server->show_refresh_button = 0;
    server->show_today_on_other_pages = 0;
    server->show_today_controls = 0;
    server->access_poll_mode = 0;
    server->access_poller = NULL;
    server->access_poller_user = NULL;
    memset(&server->today_controls, 0, sizeof(server->today_controls));
}

static void freeServerNavLinks(LoggerWebServer* server) {
    if (!server) {
        return;
    }

    for (size_t i = 0; i < server->nav_link_count; i++) {
        free(server->nav_links[i].label);
        free(server->nav_links[i].href);
    }
    free(server->nav_links);
    server->nav_links = NULL;
    server->nav_link_count = 0;
    server->nav_link_capacity = 0;
}

static int normalizeRootDirectory(const char* subdirectory,
                                  char* output,
                                  size_t output_size) {
    if (!subdirectory || !output || output_size == 0) {
        return 0;
    }

    while (*subdirectory == '/') {
        subdirectory++;
    }

    const char* end = subdirectory + strlen(subdirectory);
    while (end > subdirectory && end[-1] == '/') {
        end--;
    }

    size_t length = (size_t)(end - subdirectory);
    if (length == 0 || length >= output_size) {
        return 0;
    }

    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)subdirectory[i];
        if (!isalnum(c) && c != '-' && c != '_') {
            return 0;
        }

        output[i] = (char)tolower(c);
    }
    output[length] = '\0';

    return 1;
}

static int supportedRootDirectory(const char* subdirectory) {
    return loggerWebStringEqualsIgnoreCase(subdirectory, LOGGER_WEB_ROOT_LOG) ||
           loggerWebStringEqualsIgnoreCase(subdirectory, LOGGER_WEB_ROOT_GRAPHS);
}

static void copyRootDirectory(const LoggerWebServer* server,
                              char* output,
                              size_t output_size) {
    if (!output || output_size == 0) {
        return;
    }

    snprintf(output, output_size, "%s", LOGGER_WEB_ROOT_LOG);

    loggerWebMutexLock(&active_server_mutex);
    if (server && server->root_directory[0]) {
        snprintf(output, output_size, "%s", server->root_directory);
    }
    loggerWebMutexUnlock(&active_server_mutex);
}

int loggerWebRootDirectoryEquals(const LoggerWebServer* server, const char* subdirectory) {
    char root_directory[LOGGER_WEB_ROOT_DIRECTORY_SIZE];
    copyRootDirectory(server, root_directory, sizeof(root_directory));
    return loggerWebStringEqualsIgnoreCase(root_directory, subdirectory);
}

char* loggerWebCopyString(const char* value) {
    if (!value) {
        return NULL;
    }

    size_t length = strlen(value) + 1;
    char* copy = malloc(length);
    if (!copy) {
        return NULL;
    }

    memcpy(copy, value, length);
    return copy;
}

int loggerWebResolveColumnIndex(const LoggerWebServer* server,
                                const char* column,
                                size_t* index) {
    if (!server || !column || !index) {
        return 0;
    }

    if (loggerWebStringEqualsIgnoreCase(column, "Unix")) {
        *index = LOGGER_WEB_UNIX_FIELD;
        return 1;
    }

    if (loggerWebStringEqualsIgnoreCase(column, "Date")) {
        *index = LOGGER_WEB_DATE_FIELD;
        return 1;
    }

    if (loggerWebStringEqualsIgnoreCase(column, "Time")) {
        *index = LOGGER_WEB_TIME_FIELD;
        return 1;
    }

    for (size_t i = 0; i < server->column_header_count; i++) {
        if (loggerWebStringEqualsIgnoreCase(column, server->column_headers[i])) {
            *index = i + LOGGER_WEB_DATA_FIELD;
            return 1;
        }
    }

    return 0;
}

int loggerWebStringEqualsIgnoreCase(const char* left, const char* right) {
    if (!left || !right) {
        return 0;
    }

    while (*left && *right) {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) {
            return 0;
        }

        left++;
        right++;
    }

    return *left == '\0' && *right == '\0';
}

static const char* configLogPath(const LoggerWebConfig* config) {
    if (!config) {
        return NULL;
    }

    if (config->logger) {
        return logger_path(config->logger);
    }

    return config->log_path;
}

static size_t normalizeLogLimit(size_t limit) {
    if (limit == 0) {
        return LOGGER_WEB_DEFAULT_LOG_LIMIT;
    }

    return limit > LOGGER_WEB_MAX_LOG_LIMIT ? LOGGER_WEB_MAX_LOG_LIMIT : limit;
}
