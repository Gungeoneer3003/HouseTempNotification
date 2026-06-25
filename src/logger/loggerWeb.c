#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "loggerWeb.h"
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef LOGGER_WEB_BACKLOG
#define LOGGER_WEB_BACKLOG 8
#endif

#ifndef LOGGER_WEB_MAX_LINE
#define LOGGER_WEB_MAX_LINE 2048
#endif

typedef struct {
    char* name;
    size_t index;
} LoggerWebGraphSeries;

typedef struct {
    char* title;
    char* x_column;
    size_t x_index;
    LoggerWebGraphSeries* series;
    size_t series_count;
} LoggerWebGraph;

typedef struct {
    char log_path[512];
    unsigned short port;
    int server_fd;
    char* title;
    char** column_headers;
    size_t column_header_count;
    LoggerWebGraph* graphs;
    size_t graph_count;
    size_t graph_capacity;
} LoggerWebServer;

static LoggerWebServer* active_server = NULL;
static pthread_mutex_t active_server_mutex = PTHREAD_MUTEX_INITIALIZER;

//Function prototypes
static int initServerDisplay(LoggerWebServer* server,
                             const char* title,
                             const char* const* column_headers,
                             size_t column_header_count);
static void freeServerDisplay(LoggerWebServer* server);
static void freeGraph(LoggerWebGraph* graph);
static char* copyString(const char* value);
static void* serverLoop(void* arg);
static void handleClient(int client_fd, const LoggerWebServer* server);
static void sendIndex(int client_fd, const LoggerWebServer* server);
static void sendGraphs(int client_fd, const LoggerWebServer* server);
static void sendGraphData(int client_fd, const LoggerWebServer* server);
static void sendRawLog(int client_fd, const char* log_path);
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
static int splitFields(char* line, char** fields, size_t column_count);
static void writeLogRows(int client_fd, const LoggerWebServer* server);
static void writeLogRow(int client_fd, char* line, const LoggerWebServer* server);
static void writeGraphJson(int client_fd,
                           const LoggerWebServer* server,
                           const LoggerWebGraph* graph);
static void writeGraphPointsJson(int client_fd,
                                 const LoggerWebServer* server,
                                 const LoggerWebGraph* graph);
static int parseDouble(const char* value, double* out);
static int loggerWebHasGraphs(const LoggerWebServer* server);
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

int loggerWebInsertGraph(const char* title,
                         const char* x_column,
                         const char* y_column) {
    const char* y_columns[] = {y_column};
    return loggerWebInsertGraphSeries(title, x_column, y_columns, 1);
}

int loggerWebInsertGraphSeries(const char* title,
                               const char* x_column,
                               const char* const* y_columns,
                               size_t y_column_count) {
    if (!title || !*title || !x_column || !*x_column ||
        !y_columns || y_column_count == 0) {
        return 0;
    }

    pthread_mutex_lock(&active_server_mutex);
    LoggerWebServer* server = active_server;
    if (!server) {
        pthread_mutex_unlock(&active_server_mutex);
        return 0;
    }

    size_t x_index = 0;
    if (!resolveColumnIndex(server, x_column, &x_index)) {
        pthread_mutex_unlock(&active_server_mutex);
        return 0;
    }

    LoggerWebGraph graph;
    memset(&graph, 0, sizeof(graph));
    graph.title = copyString(title);
    graph.x_column = copyString(x_column);
    graph.x_index = x_index;
    graph.series_count = y_column_count;
    graph.series = calloc(y_column_count, sizeof(*graph.series));

    if (!graph.title || !graph.x_column || !graph.series) {
        freeGraph(&graph);
        pthread_mutex_unlock(&active_server_mutex);
        return 0;
    }

    for (size_t i = 0; i < y_column_count; i++) {
        if (!y_columns[i] || !*y_columns[i] ||
            !resolveColumnIndex(server, y_columns[i], &graph.series[i].index)) {
            freeGraph(&graph);
            pthread_mutex_unlock(&active_server_mutex);
            return 0;
        }

        graph.series[i].name = copyString(y_columns[i]);
        if (!graph.series[i].name) {
            freeGraph(&graph);
            pthread_mutex_unlock(&active_server_mutex);
            return 0;
        }
    }

    if (server->graph_count == server->graph_capacity) {
        size_t next_capacity = server->graph_capacity == 0 ? 4 : server->graph_capacity * 2;
        LoggerWebGraph* next_graphs = realloc(server->graphs,
                                              next_capacity * sizeof(*server->graphs));
        if (!next_graphs) {
            freeGraph(&graph);
            pthread_mutex_unlock(&active_server_mutex);
            return 0;
        }

        server->graphs = next_graphs;
        server->graph_capacity = next_capacity;
    }

    server->graphs[server->graph_count] = graph;
    server->graph_count++;
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

    //Free each graph definition and the graph array
    if (server->graphs) {
        for (size_t i = 0; i < server->graph_count; i++) {
            freeGraph(&server->graphs[i]);
        }
    }

    free(server->graphs);
    server->graphs = NULL;
    server->graph_count = 0;
    server->graph_capacity = 0;
}

static void freeGraph(LoggerWebGraph* graph) {
    if (!graph) {
        return;
    }

    free(graph->title);
    free(graph->x_column);

    if (graph->series) {
        for (size_t i = 0; i < graph->series_count; i++) {
            free(graph->series[i].name);
        }
    }

    free(graph->series);
    memset(graph, 0, sizeof(*graph));
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

    //Check the request path and respond accordingly
    //(Probably want to make this less dense)
    if (strncmp(request, "GET / ", 6) == 0 || strncmp(request, "GET /?", 6) == 0) {
        sendIndex(client_fd, server);
    } else if (strncmp(request, "GET /graphs/data ", 17) == 0 ||
               strncmp(request, "GET /graphs/data?", 17) == 0) {
        sendGraphData(client_fd, server);
    } else if (strncmp(request, "GET /graphs ", 12) == 0 ||
               strncmp(request, "GET /graphs?", 12) == 0) {
        sendGraphs(client_fd, server);
    } else if (strncmp(request, "GET /raw ", 9) == 0 || strncmp(request, "GET /raw?", 9) == 0) {
        sendRawLog(client_fd, server->log_path);
    } else if (strncmp(request, "GET /style.css ", 15) == 0 ||
           strncmp(request, "GET /style.css?", 15) == 0) {
        sendCss(client_fd);
    } else if (strncmp(request, "GET /loggerWebGraph.js ", 23) == 0 ||
           strncmp(request, "GET /loggerWebGraph.js?", 23) == 0) {
        sendGraphScript(client_fd);
    } else {
        sendNotFound(client_fd);
    }
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
    sendNav(client_fd, server);
    sendAll(client_fd,
            "<main id=\"graphs\" class=\"graphs\">"
            "<p class=\"empty\">Loading graphs...</p>"
            "</main>"
            "<script src=\"/loggerWebGraph.js\"></script>"
            "</body>"
            "</html>");
}

static void sendGraphData(int client_fd, const LoggerWebServer* server) {
    sendAll(client_fd,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Cache-Control: no-store\r\n"
            "Connection: close\r\n\r\n"
            "{\"graphs\":[");

    pthread_mutex_lock(&active_server_mutex);
    for (size_t i = 0; i < server->graph_count; i++) {
        if (i > 0) {
            sendAll(client_fd, ",");
        }

        writeGraphJson(client_fd, server, &server->graphs[i]);
    }
    pthread_mutex_unlock(&active_server_mutex);

    sendAll(client_fd, "]}");
}

//Send the raw log file as plain text
static void sendRawLog(int client_fd, const char* log_path) {
    sendAll(client_fd,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain; charset=utf-8\r\n"
            "Cache-Control: no-store\r\n"
            "Connection: close\r\n\r\n");

    //Open the log file and send its contents to the client
    FILE* file = fopen(log_path, "r");
    if (!file) {
        sendAll(client_fd, "");
        return;
    }

    //Read the log file line by line and send it to the client
    char buffer[2048];
    while (fgets(buffer, sizeof(buffer), file)) {
        sendAll(client_fd, buffer);
    }

    //Clean up
    fclose(file);
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
    sendAll(client_fd, "<p class=\"nav\"><a href=\"/raw\">Raw log</a>");
    if (loggerWebHasGraphs(server)) {
        sendAll(client_fd, " <a href=\"/graphs\">Graphs</a>");
    }
    sendAll(client_fd, "</p>");
}

//Send the table header row with the column headers
static void sendTableHeaders(int client_fd, const LoggerWebServer* server) {
    sendAll(client_fd, "<th>Unix</th>\n                <th>Time</th>");

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
    return server->column_header_count + 2;
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

//Read the log file and write each line as a row in the log table, starting with the most recent entries
static void writeLogRows(int client_fd, const LoggerWebServer* server) {
    size_t column_count = totalColumnCount(server);
    
    //Open the log file for reading
    FILE* file = fopen(server->log_path, "r");
    if (!file) {
        sendColspanMessage(client_fd, column_count, "No log file found.");
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
                sendColspanMessage(client_fd, column_count, "Unable to load log rows.");
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
            sendColspanMessage(client_fd, column_count, "Unable to load log rows.");
            return;
        }

        memcpy(rows[row_count], line, line_len);
        row_count++;
    }

    //Clean up the file handle
    fclose(file);

    //If the log file is empty, send a message to the client and clean up
    if (row_count == 0) {
        sendColspanMessage(client_fd, column_count, "Log file is empty.");
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

    //Split the line into fields based on the '|' delimiter, up to the number of columns expected
    char** fields = calloc(column_count, sizeof(*fields));
    if (!fields) {
        sendColspanMessage(client_fd, column_count, "Unable to load log row.");
        return;
    }

    splitFields(line, fields, column_count);

    //Send the fields as a row in the log table, with HTML escaping for special characters
    sendAll(client_fd, "<tr>");
    for (size_t i = 0; i < column_count; i++) {
        sendAll(client_fd, "<td>");
        sendEscaped(client_fd, fields[i] ? fields[i] : "");
        sendAll(client_fd, "</td>");
    }
    sendAll(client_fd, "</tr>");
    
    //Clean up
    free(fields);
}

static void writeGraphJson(int client_fd,
                           const LoggerWebServer* server,
                           const LoggerWebGraph* graph) {
    sendAll(client_fd, "{\"title\":\"");
    sendJsonEscaped(client_fd, graph->title);
    sendAll(client_fd, "\",\"xColumn\":\"");
    sendJsonEscaped(client_fd, graph->x_column);
    sendAll(client_fd, "\",\"series\":[");

    for (size_t i = 0; i < graph->series_count; i++) {
        if (i > 0) {
            sendAll(client_fd, ",");
        }

        sendAll(client_fd, "{\"name\":\"");
        sendJsonEscaped(client_fd, graph->series[i].name);
        sendAll(client_fd, "\"}");
    }

    sendAll(client_fd, "],\"points\":[");
    writeGraphPointsJson(client_fd, server, graph);
    sendAll(client_fd, "]}");
}

static void writeGraphPointsJson(int client_fd,
                                 const LoggerWebServer* server,
                                 const LoggerWebGraph* graph) {
    size_t column_count = totalColumnCount(server);
    FILE* file = fopen(server->log_path, "r");
    if (!file) {
        return;
    }

    char** fields = calloc(column_count, sizeof(*fields));
    double* values = calloc(graph->series_count, sizeof(*values));
    int* has_value = calloc(graph->series_count, sizeof(*has_value));
    if (!fields || !values || !has_value) {
        free(fields);
        free(values);
        free(has_value);
        fclose(file);
        return;
    }

    int wrote_point = 0;
    char line[LOGGER_WEB_MAX_LINE];
    while (fgets(line, sizeof(line), file)) {
        char* newline = strpbrk(line, "\r\n");
        if (newline) {
            *newline = '\0';
        }

        splitFields(line, fields, column_count);
        const char* x_text = fields[graph->x_index];
        if (!x_text || !*x_text) {
            continue;
        }

        int any_value = 0;
        for (size_t i = 0; i < graph->series_count; i++) {
            has_value[i] = 0;
            values[i] = 0.0;

            const char* y_text = fields[graph->series[i].index];
            if (parseDouble(y_text, &values[i])) {
                has_value[i] = 1;
                any_value = 1;
            }
        }

        if (!any_value) {
            continue;
        }

        if (wrote_point) {
            sendAll(client_fd, ",");
        }

        sendAll(client_fd, "{\"x\":\"");
        sendJsonEscaped(client_fd, x_text);
        sendAll(client_fd, "\",\"values\":[");
        for (size_t i = 0; i < graph->series_count; i++) {
            if (i > 0) {
                sendAll(client_fd, ",");
            }

            if (has_value[i]) {
                char number[64];
                snprintf(number, sizeof(number), "%.17g", values[i]);
                sendAll(client_fd, number);
            } else {
                sendAll(client_fd, "null");
            }
        }
        sendAll(client_fd, "]}");
        wrote_point = 1;
    }

    free(fields);
    free(values);
    free(has_value);
    fclose(file);
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

static int loggerWebHasGraphs(const LoggerWebServer* server) {
    int has_graphs = 0;

    pthread_mutex_lock(&active_server_mutex);
    has_graphs = server && server->graph_count > 0;
    pthread_mutex_unlock(&active_server_mutex);

    return has_graphs;
}

static int resolveColumnIndex(const LoggerWebServer* server,
                              const char* column,
                              size_t* index) {
    if (stringEqualsIgnoreCase(column, "Unix")) {
        *index = 0;
        return 1;
    }

    if (stringEqualsIgnoreCase(column, "Time")) {
        *index = 1;
        return 1;
    }

    for (size_t i = 0; i < server->column_header_count; i++) {
        if (stringEqualsIgnoreCase(column, server->column_headers[i])) {
            *index = i + 2;
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

    FILE* file = fopen("src/logger/loggerWebGraph.js", "r");
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
