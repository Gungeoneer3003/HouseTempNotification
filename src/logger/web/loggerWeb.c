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
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

//For the time being, there's no support for Windows.
#ifdef _WIN32
int loggerWebStart(const char* log_path,
                   unsigned short port,
                   const char* title,
                   const char* const* column_headers,
                   size_t column_header_count) {
    
    //Signify that the parameters are unused to avoid compiler warnings
    (void)log_path;
    (void)port;
    (void)title;
    (void)column_headers;
    (void)column_header_count;

    fprintf(stderr, "Logger web viewer is not supported on Windows\n");
    return 0;
}

int loggerWebSetRootDirectory(const char* subdirectory) {
    (void)subdirectory;

    fprintf(stderr, "Logger web viewer is not supported on Windows\n");
    return 0;
}

int loggerWebAddNavLink(const char* label, const char* href, int root_relative) {
    (void)label;
    (void)href;
    (void)root_relative;

    fprintf(stderr, "Logger web viewer is not supported on Windows\n");
    return 0;
}

int loggerWebInsertGraph(const char* title,
                         const char* x_column,
                         const char* y_column) {
    (void)title;
    (void)x_column;
    (void)y_column;

    fprintf(stderr, "Logger web graphs are not supported on Windows\n");
    return 0;
}

int loggerWebInsertGraphSeries(const char* title,
                               const char* x_column,
                               const char* const* y_columns,
                               size_t y_column_count) {
    (void)title;
    (void)x_column;
    (void)y_columns;
    (void)y_column_count;

    fprintf(stderr, "Logger web graphs are not supported on Windows\n");
    return 0;
}
int loggerWebShowStats(int enabled) {
    (void)enabled;

    fprintf(stderr, "Logger web graphs are not supported on Windows\n");
    return 0;
}

int loggerWebShowRefreshButton(int enabled) {
    (void)enabled;

    fprintf(stderr, "Logger web graphs are not supported on Windows\n");
    return 0;
}

int loggerWebShowVerts(const char* graph_title,
                       const char* column,
                       const char* value,
                       const char* color) {
    (void)graph_title;
    (void)column;
    (void)value;
    (void)color;

    fprintf(stderr, "Logger web graphs are not supported on Windows\n");
    return 0;
}

int loggerWebShowSpan(const char* graph_title,
                      const char* column,
                      const char* start_value,
                      const char* end_value,
                      const char* color) {
    (void)graph_title;
    (void)column;
    (void)start_value;
    (void)end_value;
    (void)color;

    fprintf(stderr, "Logger web graphs are not supported on Windows\n");
    return 0;
}

int loggerWebShowToday(const char* const* columns,
                       size_t column_count,
                       int show_on_other_pages,
                       int show_controls) {
    (void)columns;
    (void)column_count;
    (void)show_on_other_pages;
    (void)show_controls;

    fprintf(stderr, "Logger web graphs are not supported on Windows\n");
    return 0;
}

int loggerWebSetTodayControls(const LoggerWebTodayControls* controls) {
    (void)controls;

    fprintf(stderr, "Logger web graphs are not supported on Windows\n");
    return 0;
}
#else
//Get started using Linux and POSIX APIs
#include <ctype.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//Logger web server state
LoggerWebServer* active_server = NULL;
pthread_mutex_t active_server_mutex = PTHREAD_MUTEX_INITIALIZER;

//Static function prototypes
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

//Start the logger web server on the specified port
int loggerWebStart(const char* log_path,
                   unsigned short port,
                   const char* title,
                   const char* const* column_headers,
                   size_t column_header_count) {
    //Check for invalid parameters
    if (!log_path || !*log_path || port == 0 || !title || !*title ||
        (column_header_count > 0 && !column_headers) || column_header_count > SIZE_MAX - 2) {
        return 0;
    }

    //Allocate and initialize the server structure
    LoggerWebServer* server = calloc(1, sizeof(*server));
    if (!server) {
        return 0;
    }

    //Copy the log path into the server structure
    int n = snprintf(server->log_path, sizeof(server->log_path), "%s", log_path);
    if (n < 0 || (size_t)n >= sizeof(server->log_path)) {
        free(server);
        fprintf(stderr, "Log path is too long for web viewer\n");
        return 0;
    }

    snprintf(server->root_directory,
             sizeof(server->root_directory),
             "%s",
             LOGGER_WEB_ROOT_LOG);

    //Initialize the server display settings (title and column headers)
    if (!initServerDisplay(server, title, column_headers, column_header_count)) {
        free(server);
        return 0;
    }

    //Start the server thread to listen for incoming connections
    if (!loggerWebStartServer(server, port)) {
        freeServerDisplay(server);
        free(server);
        return 0;
    }

    pthread_mutex_lock(&active_server_mutex);
    active_server = server;
    pthread_mutex_unlock(&active_server_mutex);

    printf("Logger web viewer listening on port %u\n", (unsigned)port);
    return 1;
}

int loggerWebSetRootDirectory(const char* subdirectory) {
    char normalized[LOGGER_WEB_ROOT_DIRECTORY_SIZE];

    //Check for null or empty subdirectory
    if (!normalizeRootDirectory(subdirectory, normalized, sizeof(normalized)) ||
        !supportedRootDirectory(normalized)) {
        return 0;
    }

    //Set the root directory in the server structure
    pthread_mutex_lock(&active_server_mutex);
    LoggerWebServer* server = active_server;
    if (!server) {
        pthread_mutex_unlock(&active_server_mutex);
        return 0;
    }

    //Copy the normalized root directory into the server structure
    snprintf(server->root_directory,
             sizeof(server->root_directory),
             "%s",
             normalized);
    
    pthread_mutex_unlock(&active_server_mutex);
    return 1;
}

int loggerWebAddNavLink(const char* label, const char* href, int root_relative) {
    if (!label || !*label || !href || !*href) {
        return 0;
    }

    pthread_mutex_lock(&active_server_mutex);
    LoggerWebServer* server = active_server;
    if (!server) {
        pthread_mutex_unlock(&active_server_mutex);
        return 0;
    }

    // Grow the optional custom-link list only when callers actually add links.
    if (server->nav_link_count == server->nav_link_capacity) {
        size_t new_capacity = server->nav_link_capacity ? server->nav_link_capacity * 2 : 4;
        LoggerWebNavLink* new_links = realloc(server->nav_links,
                                              new_capacity * sizeof(*new_links));
        if (!new_links) {
            pthread_mutex_unlock(&active_server_mutex);
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
        pthread_mutex_unlock(&active_server_mutex);
        return 0;
    }

    server->nav_links[server->nav_link_count].label = label_copy;
    server->nav_links[server->nav_link_count].href = href_copy;
    server->nav_links[server->nav_link_count].root_relative = root_relative != 0;
    server->nav_link_count++;

    pthread_mutex_unlock(&active_server_mutex);
    return 1;
}

//Enable or disable the refresh button on the web interface
int loggerWebShowRefreshButton(int enabled) {
    pthread_mutex_lock(&active_server_mutex);
    LoggerWebServer* server = active_server;
    if (!server) {
        pthread_mutex_unlock(&active_server_mutex);
        return 0;
    }

    server->show_refresh_button = enabled != 0;
    pthread_mutex_unlock(&active_server_mutex);
    return 1;
}

//Initialize the server display settings (title and column headers)
static int initServerDisplay(LoggerWebServer* server,
                             const char* title,
                             const char* const* column_headers,
                             size_t column_header_count) {
    //Copy the title into the server structure
    server->title = loggerWebCopyString(title);
    if (!server->title) {
        return 0;
    }

    //Copy the column headers into the server structure
    server->column_header_count = column_header_count;
    if (column_header_count == 0) {
        return 1;
    }

    //Allocate the column header array
    server->column_headers = calloc(column_header_count, sizeof(*server->column_headers));
    if (!server->column_headers) {
        freeServerDisplay(server);
        return 0;
    }

    //Copy each column header string into the server structure
    for (size_t i = 0; i < column_header_count; i++) {
        //Check for null or empty column header strings
        //Those are bad
        if (!column_headers[i]) {
            freeServerDisplay(server);
            return 0;
        }

        //Copy the column header string into the server structure
        server->column_headers[i] = loggerWebCopyString(column_headers[i]);
        if (!server->column_headers[i]) {
            freeServerDisplay(server);
            return 0;
        }
    }

    return 1;
}

//Free the memory used by the server display settings
static void freeServerDisplay(LoggerWebServer* server) {
    //Check if there is a server to free
    if (!server) {
        return;
    }

    //Free the title string
    free(server->title);
    server->title = NULL;

    //Free each column header string and the column header array
    if (server->column_headers) {
        for (size_t i = 0; i < server->column_header_count; i++) {
            free(server->column_headers[i]);
        }
    }
    //Free the column header array
    free(server->column_headers);
    server->column_headers = NULL;
    server->column_header_count = 0;

    loggerWebFreeGraphs(server);
    loggerWebFreeTodayColumns(server);
    freeServerNavLinks(server);
    server->show_stats = 0;
    server->show_today_on_other_pages = 0;
    server->show_today_controls = 0;
    memset(&server->today_controls, 0, sizeof(server->today_controls));
}

static void freeServerNavLinks(LoggerWebServer* server) {
    if (!server) {
        return;
    }

    // Custom nav links are copied at configuration time and released with display state.
    for (size_t i = 0; i < server->nav_link_count; i++) {
        free(server->nav_links[i].label);
        free(server->nav_links[i].href);
    }
    free(server->nav_links);
    server->nav_links = NULL;
    server->nav_link_count = 0;
    server->nav_link_capacity = 0;
}

//Normalize the root directory string by removing leading and trailing slashes
//and converting to lowercase. Returns 1 on success, 0 on failure.
static int normalizeRootDirectory(const char* subdirectory,
                                  char* output,
                                  size_t output_size) {
    //Check for null or empty input and output parameters
    if (!subdirectory || !output || output_size == 0) {
        return 0;
    }

    //Skip leading slashes
    while (*subdirectory == '/') {
        subdirectory++;
    }

    //Find the end of the string and remove trailing slashes
    const char* end = subdirectory + strlen(subdirectory);
    while (end > subdirectory && end[-1] == '/') {
        end--;
    }

    //Check if the normalized string will fit
    size_t length = (size_t)(end - subdirectory);
    if (length == 0 || length >= output_size) {
        return 0;
    }

    //Copy the normalized string to the output buffer and convert to lowercase
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

//Check if the subdirectory is a supported root directory
static int supportedRootDirectory(const char* subdirectory) {
    return loggerWebStringEqualsIgnoreCase(subdirectory, LOGGER_WEB_ROOT_LOG) ||
           loggerWebStringEqualsIgnoreCase(subdirectory, LOGGER_WEB_ROOT_GRAPHS);
}

//Copy the root directory from the server structure to the output buffer
static void copyRootDirectory(const LoggerWebServer* server,
                              char* output,
                              size_t output_size) {
    if (!output || output_size == 0) {
        return;
    }

    snprintf(output, output_size, "%s", LOGGER_WEB_ROOT_LOG);

    //Check if the server is active and has a root directory set
    pthread_mutex_lock(&active_server_mutex);
    if (server && server->root_directory[0]) {
        snprintf(output, output_size, "%s", server->root_directory);
    }
    pthread_mutex_unlock(&active_server_mutex);
}

//Check if the root directory of the server matches the given subdirectory
int loggerWebRootDirectoryEquals(const LoggerWebServer* server, const char* subdirectory) {
    char root_directory[LOGGER_WEB_ROOT_DIRECTORY_SIZE];
    copyRootDirectory(server, root_directory, sizeof(root_directory));
    return loggerWebStringEqualsIgnoreCase(root_directory, subdirectory);
}

//Create a copy of a string using dynamic memory allocation
char* loggerWebCopyString(const char* value) {
    size_t length = strlen(value) + 1;
    char* copy = malloc(length);
    if (!copy) {
        return NULL;
    }

    memcpy(copy, value, length);
    return copy;
}

//Resolve the index of a column in the server's column headers
int loggerWebResolveColumnIndex(const LoggerWebServer* server,
                                const char* column,
                                size_t* index) {
    //Check for null parameters
    if (!server || !column || !index) {
        return 0;
    }

    //Check for special columns: Unix, Date, Time
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

    //Search for the column in the server's column headers
    for (size_t i = 0; i < server->column_header_count; i++) {
        if (loggerWebStringEqualsIgnoreCase(column, server->column_headers[i])) {
            *index = i + LOGGER_WEB_DATA_FIELD;
            return 1;
        }
    }

    return 0;
}

//Compare two strings for equality, ignoring case-sensitivity
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

#endif
