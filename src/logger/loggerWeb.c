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

//Check if this is the right platform
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
                       size_t column_count) {
    (void)columns;
    (void)column_count;

    fprintf(stderr, "Logger web graphs are not supported on Windows\n");
    return 0;
}
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

LoggerWebServer* active_server = NULL;
pthread_mutex_t active_server_mutex = PTHREAD_MUTEX_INITIALIZER;

//Function prototypes
static int initServerDisplay(LoggerWebServer* server,
                             const char* title,
                             const char* const* column_headers,
                             size_t column_header_count);
static void freeServerDisplay(LoggerWebServer* server);
static int normalizeRootDirectory(const char* subdirectory,
                                  char* output,
                                  size_t output_size);
static int supportedRootDirectory(const char* subdirectory);
static void copyRootDirectory(const LoggerWebServer* server,
                              char* output,
                              size_t output_size);
static int rootDirectoryEquals(const LoggerWebServer* server, const char* subdirectory);
static int parseRequestPath(const char* request, char* path, size_t path_size);
static int pathEquals(const char* path, const char* expected);
static char* copyString(const char* value);
static void* serverLoop(void* arg);
static void handleClient(int client_fd, const LoggerWebServer* server);
static void sendRoot(int client_fd, const LoggerWebServer* server);
static void sendIndex(int client_fd, const LoggerWebServer* server);
static void sendGraphs(int client_fd, const LoggerWebServer* server);
static void sendRawLog(int client_fd, const LoggerWebServer* server);
static void sendNotFound(int client_fd);
static void sendBytes(int fd, const char* data, size_t length);
static void sendAll(int fd, const char* data);
static void sendEscaped(int fd, const char* value);
static void sendJsonEscaped(int fd, const char* value);
static void sendTemplate(int client_fd, const char* path, const LoggerWebServer* server);
static void sendTemplateLine(int client_fd, const char* line, const LoggerWebServer* server);
static void sendNav(int client_fd, const LoggerWebServer* server);
static void sendTableHeaders(int client_fd, const LoggerWebServer* server);
static void sendColspanMessage(int client_fd, size_t column_count, const char* message);
static size_t totalColumnCount(const LoggerWebServer* server);
static size_t displayedColumnCount(const LoggerWebServer* server);
static int splitFields(char* line, char** fields, size_t column_count);
static const char* fieldForColumn(char** fields, size_t column_index);
static int rowHasSplitDateTime(char** fields);
static int looksLikeDateField(const char* value);
static int looksLikeTimeField(const char* value);
static void writeLogRows(int client_fd, const LoggerWebServer* server);
static void writeLogRow(int client_fd, char* line, const LoggerWebServer* server);
static int parseDouble(const char* value, double* out);
static int parseUnixTime(const char* value, time_t* out);
static int logLocaltime(const time_t* value, struct tm* out);
static void formatUnixLabel(time_t value, char* buffer, size_t buffer_size);
static void formatUnixDate(time_t value, char* buffer, size_t buffer_size);
static void formatUnixTime(time_t value, char* buffer, size_t buffer_size);
static void formatDuration(time_t seconds, char* buffer, size_t buffer_size);
static int resolveColumnIndex(const LoggerWebServer* server,
                              const char* column,
                              size_t* index);
static int stringEqualsIgnoreCase(const char* left, const char* right);
static void sendCss(int client_fd);
static void sendGraphScript(int client_fd);

//Start the logger web server on the specified port
int loggerWebStart(const char* log_path,
                   unsigned short port,
                   const char* title,
                   const char* const* column_headers,
                   size_t column_header_count) {
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

    if (!initServerDisplay(server, title, column_headers, column_header_count)) {
        free(server);
        return 0;
    }

    //Set the port and create the server socket
    server->port = port;
    server->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->server_fd < 0) {
        fprintf(stderr, "Failed to create logger web socket: %s\n", strerror(errno));
        freeServerDisplay(server);
        free(server);
        return 0;
    }

    //Allow the socket to be reused
    //This is for if the server is restarted quickly, to avoid "address already in use" errors
    int reuse = 1;
    setsockopt(server->server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    //Bind the socket to the specified port on all interfaces
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    //Bind the socket and check for errors
    if (bind(server->server_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "Failed to bind logger web viewer on port %u: %s\n",
                (unsigned)port, strerror(errno));
        close(server->server_fd);
        freeServerDisplay(server);
        free(server);
        return 0;
    }

    //Start listening for incoming connections
    if (listen(server->server_fd, LOGGER_WEB_BACKLOG) != 0) {
        fprintf(stderr, "Failed to listen for logger web viewer: %s\n", strerror(errno));
        close(server->server_fd);
        freeServerDisplay(server);
        free(server);
        return 0;
    }

    //Start the server loop in a detached thread
    pthread_t thread;
    if (pthread_create(&thread, NULL, serverLoop, server) != 0) {
        fprintf(stderr, "Failed to start logger web viewer thread\n");
        close(server->server_fd);
        freeServerDisplay(server);
        free(server);
        return 0;
    }

    //Detach the thread so it cleans up after itself when it exits
    pthread_detach(thread);

    pthread_mutex_lock(&active_server_mutex);
    active_server = server;
    pthread_mutex_unlock(&active_server_mutex);

    printf("Logger web viewer listening on port %u\n", (unsigned)port);
    return 1;
}

int loggerWebSetRootDirectory(const char* subdirectory) {
    char normalized[LOGGER_WEB_ROOT_DIRECTORY_SIZE];
    if (!normalizeRootDirectory(subdirectory, normalized, sizeof(normalized)) ||
        !supportedRootDirectory(normalized)) {
        return 0;
    }

    pthread_mutex_lock(&active_server_mutex);
    LoggerWebServer* server = active_server;
    if (!server) {
        pthread_mutex_unlock(&active_server_mutex);
        return 0;
    }

    snprintf(server->root_directory,
             sizeof(server->root_directory),
             "%s",
             normalized);
    pthread_mutex_unlock(&active_server_mutex);
    return 1;
}

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
    server->title = copyString(title);
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
        server->column_headers[i] = copyString(column_headers[i]);
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
    server->show_stats = 0;
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
    return stringEqualsIgnoreCase(subdirectory, LOGGER_WEB_ROOT_LOG) ||
           stringEqualsIgnoreCase(subdirectory, LOGGER_WEB_ROOT_GRAPHS);
}

static void copyRootDirectory(const LoggerWebServer* server,
                              char* output,
                              size_t output_size) {
    if (!output || output_size == 0) {
        return;
    }

    snprintf(output, output_size, "%s", LOGGER_WEB_ROOT_LOG);

    pthread_mutex_lock(&active_server_mutex);
    if (server && server->root_directory[0]) {
        snprintf(output, output_size, "%s", server->root_directory);
    }
    pthread_mutex_unlock(&active_server_mutex);
}

static int rootDirectoryEquals(const LoggerWebServer* server, const char* subdirectory) {
    char root_directory[LOGGER_WEB_ROOT_DIRECTORY_SIZE];
    copyRootDirectory(server, root_directory, sizeof(root_directory));
    return stringEqualsIgnoreCase(root_directory, subdirectory);
}

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

//Create a copy of a string using dynamic memory allocation
static char* copyString(const char* value) {
    size_t length = strlen(value) + 1;
    char* copy = malloc(length);
    if (!copy) {
        return NULL;
    }

    memcpy(copy, value, length);
    return copy;
}

//Server loop that accepts incoming connections and handles them
static void* serverLoop(void* arg) {
    LoggerWebServer* server = (LoggerWebServer*)arg;

    //Accept incoming connections in an endless loop
    for (;;) {
        int client_fd = accept(server->server_fd, NULL, NULL);
        if (client_fd < 0) {
            continue;
        }

        handleClient(client_fd, server);
        close(client_fd);
    }

    return NULL;
}

//Handle a single client connection
static void handleClient(int client_fd, const LoggerWebServer* server) {
    char request[1024];
    ssize_t bytes = recv(client_fd, request, sizeof(request) - 1, 0);
    if (bytes <= 0) {
        return;
    }

    //Null-terminate the request string so we can safely use string functions on it
    request[bytes] = '\0';

    char path[LOGGER_WEB_MAX_PATH];
    if (!parseRequestPath(request, path, sizeof(path))) {
        sendNotFound(client_fd);
        return;
    }

    //Check the request path and respond accordingly
    if (pathEquals(path, "/")) {
        sendRoot(client_fd, server);
    } else if (pathEquals(path, "/log")) {
        sendIndex(client_fd, server);
    } else if (pathEquals(path, "/graphs/data") ||
               (rootDirectoryEquals(server, LOGGER_WEB_ROOT_GRAPHS) &&
                pathEquals(path, "/data"))) {
        loggerWebSendGraphData(client_fd, server, loggerWebParseGraphRange(request));
    } else if (pathEquals(path, "/graphs")) {
        sendGraphs(client_fd, server);
    } else if (pathEquals(path, "/raw")) {
        sendRawLog(client_fd, server);
    } else if (pathEquals(path, "/style.css")) {
        sendCss(client_fd);
    } else if (pathEquals(path, "/loggerWebGraph.js")) {
        sendGraphScript(client_fd);
    } else {
        sendNotFound(client_fd);
    }
}

static void sendRoot(int client_fd, const LoggerWebServer* server) {
    if (rootDirectoryEquals(server, LOGGER_WEB_ROOT_GRAPHS)) {
        sendGraphs(client_fd, server);
        return;
    }

    sendIndex(client_fd, server);
}

//Send the main index page with the log table
static void sendIndex(int client_fd, const LoggerWebServer* server) {
    sendAll(client_fd,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Cache-Control: no-store\r\n"
            "Connection: close\r\n\r\n");

    sendTemplate(client_fd, "src/logger/loggerWeb.html", server);

    //Write the log rows into the table
    writeLogRows(client_fd, server);

    sendAll(client_fd,
            "</tbody>"
            "</table>"
            "</body>"
            "</html>");
}

static void sendGraphs(int client_fd, const LoggerWebServer* server) {
    sendAll(client_fd,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Cache-Control: no-store\r\n"
            "Connection: close\r\n\r\n"
            "<!doctype html>"
            "<html>"
            "<head>"
            "<meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<title>");
    sendEscaped(client_fd, server->title);
    sendAll(client_fd,
            " - Graphs</title>"
            "<link rel=\"stylesheet\" href=\"/style.css\">"
            "</head>"
            "<body>"
            "<h1>");
    sendEscaped(client_fd, server->title);
    sendAll(client_fd, " Graphs</h1>");
    sendAll(client_fd, "<section id=\"today\" class=\"today-panel is-hidden\"></section>");
    sendNav(client_fd, server);
    const char* graph_data_path = rootDirectoryEquals(server, LOGGER_WEB_ROOT_GRAPHS)
        ? "/data"
        : "/graphs/data";
    sendAll(client_fd,
            "<main id=\"graphs\" class=\"graphs\" data-graph-data-url=\"");
    sendAll(client_fd, graph_data_path);
    sendAll(client_fd,
            "\" data-refresh-ms=\"150000\" data-show-refresh-button=\"");
    sendAll(client_fd, server->show_refresh_button ? "1" : "0");
    sendAll(client_fd,
            "\">"
            "<p class=\"empty\">Loading graphs...</p>"
            "</main>"
            "<script src=\"https://cdn.jsdelivr.net/npm/chart.js\"></script>"
            "<script src=\"/loggerWebGraph.js\"></script>"
            "</body>"
            "</html>");
}

//Send the raw log file inside a small HTML shell so the normal navigation is available.
static void sendRawLog(int client_fd, const LoggerWebServer* server) {
    sendAll(client_fd,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Cache-Control: no-store\r\n"
            "Connection: close\r\n\r\n"
            "<!doctype html>"
            "<html>"
            "<head>"
            "<meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<title>");
    sendEscaped(client_fd, server->title);
    sendAll(client_fd,
            " - Raw Log</title>"
            "<link rel=\"stylesheet\" href=\"/style.css\">"
            "</head>"
            "<body>"
            "<h1>");
    sendEscaped(client_fd, server->title);
    sendAll(client_fd, " Raw Log</h1>");
    sendNav(client_fd, server);
    sendAll(client_fd, "<pre class=\"raw-log\">");

    //Open the log file and send its contents to the client
    FILE* file = fopen(server->log_path, "r");
    if (!file) {
        sendAll(client_fd, "</pre></body></html>");
        return;
    }

    //Read the log file line by line and send it to the client
    char buffer[2048];
    while (fgets(buffer, sizeof(buffer), file)) {
        sendEscaped(client_fd, buffer);
    }

    //Clean up
    fclose(file);
    sendAll(client_fd, "</pre></body></html>");
}

//Send a 404 Not Found response to the client
static void sendNotFound(int client_fd) {
    sendAll(client_fd,
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/plain; charset=utf-8\r\n"
            "Connection: close\r\n\r\n"
            "Not found\n");
}

//Send a specified number of bytes from a buffer to a socket, handling partial sends
static void sendBytes(int fd, const char* data, size_t length) {
    size_t remaining = length;
    const char* cursor = data;

    //Loop until all bytes have been sent
    while (remaining > 0) {
        ssize_t sent = send(fd, cursor, remaining, 0);
        if (sent <= 0) {
            return;
        }

        cursor += sent;
        remaining -= (size_t)sent;
    }
}

//Send a null-terminated string to a socket
static void sendAll(int fd, const char* data) {
    sendBytes(fd, data, strlen(data));
}

//Send a string to a socket with HTML escaping for special characters
static void sendEscaped(int fd, const char* value) {
    //Loop through each character in the string and send it, escaping special characters as needed
    for (const char* p = value; p && *p; p++) {
        switch (*p) {
            case '&':
                sendAll(fd, "&amp;");
                break;
            case '<':
                sendAll(fd, "&lt;");
                break;
            case '>':
                sendAll(fd, "&gt;");
                break;
            case '"':
                sendAll(fd, "&quot;");
                break;
            default: {
                char c[2] = {*p, '\0'};
                sendAll(fd, c);
                break;
            }
        }
    }
}

static void sendJsonEscaped(int fd, const char* value) {
    for (const unsigned char* p = (const unsigned char*)value; p && *p; p++) {
        switch (*p) {
            case '"':
                sendAll(fd, "\\\"");
                break;
            case '\\':
                sendAll(fd, "\\\\");
                break;
            case '\b':
                sendAll(fd, "\\b");
                break;
            case '\f':
                sendAll(fd, "\\f");
                break;
            case '\n':
                sendAll(fd, "\\n");
                break;
            case '\r':
                sendAll(fd, "\\r");
                break;
            case '\t':
                sendAll(fd, "\\t");
                break;
            default:
                if (*p < 0x20) {
                    char escaped[8];
                    snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned)*p);
                    sendAll(fd, escaped);
                } else {
                    char c[2] = {(char)*p, '\0'};
                    sendAll(fd, c);
                }
                break;
        }
    }
}

//Send an HTML template file to the client, replacing placeholders with dynamic content
static void sendTemplate(int client_fd, const char* path, const LoggerWebServer* server) {
    //Open the template file
    FILE* file = fopen(path, "r");
    if (!file) {
        sendAll(client_fd, "<p>Missing page template.</p>");
        return;
    }

    //Read the template file line by line and send it to the client
    char buffer[2048];
    while (fgets(buffer, sizeof(buffer), file)) {
        sendTemplateLine(client_fd, buffer, server);
    }

    //Clean up
    fclose(file);
}

//Send a single line of the template to the client, replacing placeholders with dynamic content
static void sendTemplateLine(int client_fd, const char* line, const LoggerWebServer* server) {
    static const char title_placeholder[] = "{{LOGGER_WEB_TITLE}}";
    static const char headers_placeholder[] = "{{LOGGER_WEB_HEADERS}}";
    static const char nav_placeholder[] = "{{LOGGER_WEB_NAV}}";
    const char* cursor = line;

    //Loop through the line and replace placeholders with dynamic content
    for (;;) {
        const char* title_at = strstr(cursor, title_placeholder);
        const char* headers_at = strstr(cursor, headers_placeholder);
        const char* nav_at = strstr(cursor, nav_placeholder);
        const char* next = NULL;
        int placeholder = 0;

        //Determine which placeholder comes next in the line and set the next pointer accordingly
        if (title_at && (!headers_at || title_at < headers_at)) {
            next = title_at;
            placeholder = 1;
        }
        if (headers_at && (!next || headers_at < next)) {
            next = headers_at;
            placeholder = 2;
        }
        if (nav_at && (!next || nav_at < next)) {
            next = nav_at;
            placeholder = 3;
        }
        if (!next) {
            sendAll(client_fd, cursor);
            return;
        }

        //Send the part of the line before the placeholder
        sendBytes(client_fd, cursor, (size_t)(next - cursor));

        //Replace the placeholder with the appropriate dynamic content
        if (placeholder == 1) {
            sendEscaped(client_fd, server->title);
            cursor = next + strlen(title_placeholder);
        } else if (placeholder == 2) {
            sendTableHeaders(client_fd, server);
            cursor = next + strlen(headers_placeholder);
        } else {
            sendNav(client_fd, server);
            cursor = next + strlen(nav_placeholder);
        }
    }
}

static void sendNav(int client_fd, const LoggerWebServer* server) {
    const char* log_path = rootDirectoryEquals(server, LOGGER_WEB_ROOT_LOG) ? "/" : "/log";
    const char* graphs_path = rootDirectoryEquals(server, LOGGER_WEB_ROOT_GRAPHS)
        ? "/"
        : "/graphs";

    sendAll(client_fd, "<p class=\"nav\"><a href=\"");
    sendAll(client_fd, log_path);
    sendAll(client_fd, "\">Log</a> <a href=\"/raw\">Raw log</a>");
    if (loggerWebHasGraphs(server)) {
        sendAll(client_fd, " <a href=\"");
        sendAll(client_fd, graphs_path);
        sendAll(client_fd, "\">Graphs</a>");
    }
    sendAll(client_fd, "</p>");
}

//Send the table header row with the column headers
static void sendTableHeaders(int client_fd, const LoggerWebServer* server) {
    sendAll(client_fd, "<th>Date</th>\n                <th>Time</th>");

    for (size_t i = 0; i < server->column_header_count; i++) {
        sendAll(client_fd, "\n                <th>");
        sendEscaped(client_fd, server->column_headers[i]);
        sendAll(client_fd, "</th>");
    }
}

//Send a message in a table row that spans all columns, for displaying errors or other important information
static void sendColspanMessage(int client_fd, size_t column_count, const char* message) {
    char colspan[32];
    snprintf(colspan, sizeof(colspan), "%zu", column_count);

    sendAll(client_fd, "<tr><td colspan=\"");
    sendAll(client_fd, colspan);
    sendAll(client_fd, "\">");
    sendEscaped(client_fd, message);
    sendAll(client_fd, "</td></tr>");
}

static size_t totalColumnCount(const LoggerWebServer* server) {
    return server->column_header_count + LOGGER_WEB_DATA_FIELD;
}

static size_t displayedColumnCount(const LoggerWebServer* server) {
    return totalColumnCount(server) - 1;
}

static int splitFields(char* line, char** fields, size_t column_count) {
    if (!line || !fields || column_count == 0) {
        return 0;
    }

    memset(fields, 0, column_count * sizeof(*fields));

    //The first field starts at the beginning of the line, and each subsequent field starts after the next '|' character
    char* cursor = line;
    for (size_t i = 0; i < column_count; i++) {
        fields[i] = cursor;

        if (i + 1 == column_count) {
            break;
        }

        char* next = strchr(cursor, '|');
        if (!next) {
            break;
        }

        *next = '\0';
        cursor = next + 1;
    }

    return 1;
}

static const char* fieldForColumn(char** fields, size_t column_index) {
    if (!fields) {
        return NULL;
    }

    if (column_index < LOGGER_WEB_DATA_FIELD || rowHasSplitDateTime(fields)) {
        return fields[column_index];
    }

    return fields[column_index - 1];
}

static int rowHasSplitDateTime(char** fields) {
    return fields &&
           looksLikeDateField(fields[LOGGER_WEB_DATE_FIELD]) &&
           looksLikeTimeField(fields[LOGGER_WEB_TIME_FIELD]);
}

static int looksLikeDateField(const char* value) {
    if (!value || strlen(value) != 10) {
        return 0;
    }

    return value &&
           isdigit((unsigned char)value[0]) &&
           isdigit((unsigned char)value[1]) &&
           isdigit((unsigned char)value[2]) &&
           isdigit((unsigned char)value[3]) &&
           value[4] == '-' &&
           isdigit((unsigned char)value[5]) &&
           isdigit((unsigned char)value[6]) &&
           value[7] == '-' &&
           isdigit((unsigned char)value[8]) &&
           isdigit((unsigned char)value[9]) &&
           value[10] == '\0';
}

static int looksLikeTimeField(const char* value) {
    if (!value || !strchr(value, ':')) {
        return 0;
    }

    return strstr(value, " AM") != NULL || strstr(value, " PM") != NULL;
}

//Read the log file and write each line as a row in the log table, starting with the most recent entries
static void writeLogRows(int client_fd, const LoggerWebServer* server) {
    size_t display_column_count = displayedColumnCount(server);
    
    //Open the log file for reading
    FILE* file = fopen(server->log_path, "r");
    if (!file) {
        sendColspanMessage(client_fd, display_column_count, "No log file found.");
        return;
    }

    char** rows = NULL;
    size_t row_count = 0;
    size_t row_capacity = 0;
    char line[LOGGER_WEB_MAX_LINE];

    //Read the log file line by line and store each line in a dynamically growing array
    while (fgets(line, sizeof(line), file)) {
        char* newline = strpbrk(line, "\r\n");
        if (newline) {
            *newline = '\0';
        }

        //Grow the row array if needed
        if (row_count == row_capacity) {
            size_t next_capacity = row_capacity == 0 ? 32 : row_capacity * 2;
            char** next_rows = realloc(rows, next_capacity * sizeof(*next_rows));
            
            //If realloc fails, free any rows that were already allocated and send an error message to the client
            if (!next_rows) {
                for (size_t i = 0; i < row_count; i++) {
                    free(rows[i]);
                }
                free(rows);
                fclose(file);
                sendColspanMessage(client_fd, display_column_count, "Unable to load log rows.");
                return;
            }

            rows = next_rows;
            row_capacity = next_capacity;
        }

        //Copy the line into the row array
        size_t line_len = strlen(line) + 1;
        rows[row_count] = malloc(line_len);
        
        //If malloc fails, free any rows that were already allocated and send an error message to the client
        if (!rows[row_count]) {
            for (size_t i = 0; i < row_count; i++) {
                free(rows[i]);
            }

            //Clean up and send an error message to the client
            free(rows);
            fclose(file);
            sendColspanMessage(client_fd, display_column_count, "Unable to load log rows.");
            return;
        }

        memcpy(rows[row_count], line, line_len);
        row_count++;
    }

    //Clean up the file handle
    fclose(file);

    //If the log file is empty, send a message to the client and clean up
    if (row_count == 0) {
        sendColspanMessage(client_fd, display_column_count, "Log file is empty.");
        free(rows);
        return;
    }

    //Write the log rows to the client in reverse order (most recent first), freeing each row after it is sent
    for (size_t i = row_count; i > 0; i--) {
        writeLogRow(client_fd, rows[i - 1], server);
        free(rows[i - 1]);
    }
    free(rows);
}

//Parse a log line into its fields and write it as a row in the log table, with HTML escaping for special characters
static void writeLogRow(int client_fd, char* line, const LoggerWebServer* server) {
    size_t column_count = totalColumnCount(server);
    size_t display_column_count = displayedColumnCount(server);

    //Split the line into fields based on the '|' delimiter, up to the number of columns expected
    char** fields = calloc(column_count, sizeof(*fields));
    if (!fields) {
        sendColspanMessage(client_fd, display_column_count, "Unable to load log row.");
        return;
    }

    splitFields(line, fields, column_count);

    //Send the fields as a row in the log table, with HTML escaping for special characters
    time_t logged_at = 0;
    int has_logged_at = parseUnixTime(fields[LOGGER_WEB_UNIX_FIELD], &logged_at);
    char date_text[32] = "";
    char time_text[32] = "";
    if (has_logged_at) {
        formatUnixDate(logged_at, date_text, sizeof(date_text));
        formatUnixTime(logged_at, time_text, sizeof(time_text));
    } else if (rowHasSplitDateTime(fields)) {
        snprintf(date_text, sizeof(date_text), "%s", fields[LOGGER_WEB_DATE_FIELD]);
        snprintf(time_text, sizeof(time_text), "%s", fields[LOGGER_WEB_TIME_FIELD]);
    } else if (fields[LOGGER_WEB_DATE_FIELD]) {
        const char* separator = strchr(fields[LOGGER_WEB_DATE_FIELD], ' ');
        if (separator) {
            size_t date_length = (size_t)(separator - fields[LOGGER_WEB_DATE_FIELD]);
            if (date_length >= sizeof(date_text)) {
                date_length = sizeof(date_text) - 1;
            }
            memcpy(date_text, fields[LOGGER_WEB_DATE_FIELD], date_length);
            date_text[date_length] = '\0';
            snprintf(time_text, sizeof(time_text), "%s", separator + 1);
        } else {
            snprintf(date_text, sizeof(date_text), "%s", fields[LOGGER_WEB_DATE_FIELD]);
        }
    }

    sendAll(client_fd, "<tr>");
    sendAll(client_fd, "<td>");
    sendEscaped(client_fd, date_text);
    sendAll(client_fd, "</td><td>");
    sendEscaped(client_fd, time_text);
    sendAll(client_fd, "</td>");

    for (size_t i = 0; i < server->column_header_count; i++) {
        const char* field = fieldForColumn(fields, i + LOGGER_WEB_DATA_FIELD);
        sendAll(client_fd, "<td>");
        sendEscaped(client_fd, field ? field : "");
        sendAll(client_fd, "</td>");
    }
    sendAll(client_fd, "</tr>");
    
    //Clean up
    free(fields);
}

static int parseDouble(const char* value, double* out) {
    if (!value || !*value || !out) {
        return 0;
    }

    errno = 0;
    char* end = NULL;
    double parsed = strtod(value, &end);
    if (end == value || errno == ERANGE) {
        return 0;
    }

    while (end && *end && isspace((unsigned char)*end)) {
        end++;
    }
    if (end && *end) {
        return 0;
    }

    *out = parsed;
    return 1;
}


static int parseUnixTime(const char* value, time_t* out) {
    if (!value || !*value || !out) {
        return 0;
    }

    errno = 0;
    char* end = NULL;
    long long parsed = strtoll(value, &end, 10);
    if (end == value || errno == ERANGE) {
        return 0;
    }

    while (end && *end && isspace((unsigned char)*end)) {
        end++;
    }
    if (end && *end) {
        return 0;
    }

    *out = (time_t)parsed;
    return 1;
}

static int logLocaltime(const time_t* value, struct tm* out) {
    if (!value || !out) {
        return 0;
    }

    return localtime_r(value, out) != NULL;
}

static void formatUnixLabel(time_t value, char* buffer, size_t buffer_size) {
    struct tm local;
    if (logLocaltime(&value, &local)) {
        strftime(buffer, buffer_size, "%Y-%m-%d %I:%M:%S %p", &local);
    } else if (buffer_size > 0) {
        buffer[0] = '\0';
    }
}

static void formatUnixDate(time_t value, char* buffer, size_t buffer_size) {
    struct tm local;
    if (logLocaltime(&value, &local)) {
        strftime(buffer, buffer_size, "%Y-%m-%d", &local);
    } else if (buffer_size > 0) {
        buffer[0] = '\0';
    }
}

static void formatUnixTime(time_t value, char* buffer, size_t buffer_size) {
    struct tm local;
    if (logLocaltime(&value, &local)) {
        strftime(buffer, buffer_size, "%I:%M:%S %p", &local);
    } else if (buffer_size > 0) {
        buffer[0] = '\0';
    }
}

static void formatDuration(time_t seconds, char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return;
    }

    if (seconds < 0) {
        seconds = 0;
    }

    long long total_seconds = (long long)seconds;
    long long days = total_seconds / (24LL * 60LL * 60LL);
    long long hours = (total_seconds / (60LL * 60LL)) % 24LL;
    long long minutes = (total_seconds / 60LL) % 60LL;
    long long remaining_seconds = total_seconds % 60LL;

    if (days > 0) {
        snprintf(buffer, buffer_size, "%lldd %lldh %lldm", days, hours, minutes);
    } else if (hours > 0) {
        snprintf(buffer, buffer_size, "%lldh %lldm", hours, minutes);
    } else if (minutes > 0) {
        snprintf(buffer, buffer_size, "%lldm", minutes);
    } else {
        snprintf(buffer, buffer_size, "%llds", remaining_seconds);
    }
}

static int resolveColumnIndex(const LoggerWebServer* server,
                              const char* column,
                              size_t* index) {
    if (stringEqualsIgnoreCase(column, "Unix")) {
        *index = LOGGER_WEB_UNIX_FIELD;
        return 1;
    }

    if (stringEqualsIgnoreCase(column, "Date")) {
        *index = LOGGER_WEB_DATE_FIELD;
        return 1;
    }

    if (stringEqualsIgnoreCase(column, "Time")) {
        *index = LOGGER_WEB_TIME_FIELD;
        return 1;
    }

    for (size_t i = 0; i < server->column_header_count; i++) {
        if (stringEqualsIgnoreCase(column, server->column_headers[i])) {
            *index = i + LOGGER_WEB_DATA_FIELD;
            return 1;
        }
    }

    return 0;
}

static int stringEqualsIgnoreCase(const char* left, const char* right) {
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

char* loggerWebCopyString(const char* value) {
    return copyString(value);
}

void loggerWebSendAll(int fd, const char* data) {
    sendAll(fd, data);
}

void loggerWebSendJsonEscaped(int fd, const char* value) {
    sendJsonEscaped(fd, value);
}

size_t loggerWebTotalColumnCount(const LoggerWebServer* server) {
    return totalColumnCount(server);
}

int loggerWebSplitFields(char* line, char** fields, size_t column_count) {
    return splitFields(line, fields, column_count);
}

const char* loggerWebFieldForColumn(char** fields, size_t column_index) {
    return fieldForColumn(fields, column_index);
}

int loggerWebRowHasSplitDateTime(char** fields) {
    return rowHasSplitDateTime(fields);
}

int loggerWebParseDouble(const char* value, double* out) {
    return parseDouble(value, out);
}

int loggerWebParseUnixTime(const char* value, time_t* out) {
    return parseUnixTime(value, out);
}

int loggerWebLogLocaltime(const time_t* value, struct tm* out) {
    return logLocaltime(value, out);
}

void loggerWebFormatUnixLabel(time_t value, char* buffer, size_t buffer_size) {
    formatUnixLabel(value, buffer, buffer_size);
}

void loggerWebFormatUnixTime(time_t value, char* buffer, size_t buffer_size) {
    formatUnixTime(value, buffer, buffer_size);
}

void loggerWebFormatDuration(time_t seconds, char* buffer, size_t buffer_size) {
    formatDuration(seconds, buffer, buffer_size);
}

int loggerWebResolveColumnIndex(const LoggerWebServer* server,
                                const char* column,
                                size_t* index) {
    return resolveColumnIndex(server, column, index);
}

int loggerWebStringEqualsIgnoreCase(const char* left, const char* right) {
    return stringEqualsIgnoreCase(left, right);
}

//Send the CSS stylesheet to the client
static void sendCss(int client_fd) {
    //Send the HTTP response headers for a CSS file
    sendAll(client_fd,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/css; charset=utf-8\r\n"
            "Cache-Control: no-store\r\n"
            "Connection: close\r\n\r\n");

    //Open the CSS file and send its contents to the client
    FILE* file = fopen("src/logger/loggerWeb.css", "r");
    if (!file) {
        return;
    }

    //Read the CSS file line by line and send it to the client
    char buffer[2048];
    while (fgets(buffer, sizeof(buffer), file)) {
        sendAll(client_fd, buffer);
    }

    //Clean up
    fclose(file);
}

static void sendGraphScript(int client_fd) {
    sendAll(client_fd,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/javascript; charset=utf-8\r\n"
            "Cache-Control: no-store\r\n"
            "Connection: close\r\n\r\n");

    FILE* file = fopen("src/logger/graph/loggerWebGraph.js", "r");
    if (!file) {
        return;
    }

    char buffer[2048];
    while (fgets(buffer, sizeof(buffer), file)) {
        sendAll(client_fd, buffer);
    }

    fclose(file);
}

#endif
