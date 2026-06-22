#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "loggerWeb.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//Check if this is the right platform
#ifdef _WIN32
int loggerWebStart(const char* log_path, unsigned short port) {
    (void)log_path;
    (void)port;
    fprintf(stderr, "Logger web viewer is not supported on Windows\n");
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
    char log_path[512];
    unsigned short port;
    int server_fd;
} LoggerWebServer;

//Function prototypes
static void* serverLoop(void* arg);
static void handleClient(int client_fd, const char* log_path);
static void sendIndex(int client_fd, const char* log_path);
static void sendRawLog(int client_fd, const char* log_path);
static void sendNotFound(int client_fd);
static void sendAll(int fd, const char* data);
static void sendEscaped(int fd, const char* value);
static void writeLogRows(int client_fd, const char* log_path);
static void writeLogRow(int client_fd, char* line);
static void sendCss(int client_fd);
static void sendFile(int client_fd, const char* path);

//Start the logger web server on the specified port
int loggerWebStart(const char* log_path, unsigned short port) {
    if (!log_path || !*log_path || port == 0) {
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

    //Set the port and create the server socket
    server->port = port;
    server->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->server_fd < 0) {
        fprintf(stderr, "Failed to create logger web socket: %s\n", strerror(errno));
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
        free(server);
        return 0;
    }

    //Start listening for incoming connections
    if (listen(server->server_fd, LOGGER_WEB_BACKLOG) != 0) {
        fprintf(stderr, "Failed to listen for logger web viewer: %s\n", strerror(errno));
        close(server->server_fd);
        free(server);
        return 0;
    }

    //Start the server loop in a detached thread
    pthread_t thread;
    if (pthread_create(&thread, NULL, serverLoop, server) != 0) {
        fprintf(stderr, "Failed to start logger web viewer thread\n");
        close(server->server_fd);
        free(server);
        return 0;
    }

    //Detach the thread so it cleans up after itself when it exits
    pthread_detach(thread);
    printf("Logger web viewer listening on port %u\n", (unsigned)port);
    return 1;
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

        handleClient(client_fd, server->log_path);
        close(client_fd);
    }

    return NULL;
}

//Handle a single client connection
static void handleClient(int client_fd, const char* log_path) {
    char request[1024];
    ssize_t bytes = recv(client_fd, request, sizeof(request) - 1, 0);
    if (bytes <= 0) {
        return;
    }

    request[bytes] = '\0';

    if (strncmp(request, "GET / ", 6) == 0 || strncmp(request, "GET /?", 6) == 0) {
        sendIndex(client_fd, log_path);
    } else if (strncmp(request, "GET /raw ", 9) == 0 || strncmp(request, "GET /raw?", 9) == 0) {
        sendRawLog(client_fd, log_path);
    } else if (strncmp(request, "GET /style.css ", 15) == 0 ||
           strncmp(request, "GET /style.css?", 15) == 0) {
        sendCss(client_fd);
    } else {
        sendNotFound(client_fd);
    }
}

static void sendIndex(int client_fd, const char* log_path) {
    sendAll(client_fd,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Cache-Control: no-store\r\n"
            "Connection: close\r\n\r\n");

    sendFile(client_fd, "loggerWeb.html");

    writeLogRows(client_fd, log_path);

    sendAll(client_fd,
            "</tbody>"
            "</table>"
            "</body>"
            "</html>");
}

static void sendRawLog(int client_fd, const char* log_path) {
    sendAll(client_fd,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain; charset=utf-8\r\n"
            "Cache-Control: no-store\r\n"
            "Connection: close\r\n\r\n");

    FILE* file = fopen(log_path, "r");
    if (!file) {
        sendAll(client_fd, "");
        return;
    }

    char buffer[2048];
    while (fgets(buffer, sizeof(buffer), file)) {
        sendAll(client_fd, buffer);
    }

    fclose(file);
}

static void sendNotFound(int client_fd) {
    sendAll(client_fd,
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/plain; charset=utf-8\r\n"
            "Connection: close\r\n\r\n"
            "Not found\n");
}

static void sendAll(int fd, const char* data) {
    size_t remaining = strlen(data);
    const char* cursor = data;

    while (remaining > 0) {
        ssize_t sent = send(fd, cursor, remaining, 0);
        if (sent <= 0) {
            return;
        }

        cursor += sent;
        remaining -= (size_t)sent;
    }
}

static void sendEscaped(int fd, const char* value) {
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

static void writeLogRows(int client_fd, const char* log_path) {
    FILE* file = fopen(log_path, "r");
    if (!file) {
        sendAll(client_fd, "<tr><td colspan=\"8\">No log file found.</td></tr>");
        return;
    }

    char** rows = NULL;
    size_t row_count = 0;
    size_t row_capacity = 0;
    char line[LOGGER_WEB_MAX_LINE];

    while (fgets(line, sizeof(line), file)) {
        char* newline = strpbrk(line, "\r\n");
        if (newline) {
            *newline = '\0';
        }

        if (row_count == row_capacity) {
            size_t next_capacity = row_capacity == 0 ? 32 : row_capacity * 2;
            char** next_rows = realloc(rows, next_capacity * sizeof(*next_rows));
            if (!next_rows) {
                for (size_t i = 0; i < row_count; i++) {
                    free(rows[i]);
                }
                free(rows);
                fclose(file);
                sendAll(client_fd, "<tr><td colspan=\"8\">Unable to load log rows.</td></tr>");
                return;
            }
            rows = next_rows;
            row_capacity = next_capacity;
        }

        size_t line_len = strlen(line) + 1;
        rows[row_count] = malloc(line_len);
        if (!rows[row_count]) {
            for (size_t i = 0; i < row_count; i++) {
                free(rows[i]);
            }
            free(rows);
            fclose(file);
            sendAll(client_fd, "<tr><td colspan=\"8\">Unable to load log rows.</td></tr>");
            return;
        }
        memcpy(rows[row_count], line, line_len);
        row_count++;
    }

    fclose(file);

    if (row_count == 0) {
        sendAll(client_fd, "<tr><td colspan=\"8\">Log file is empty.</td></tr>");
        free(rows);
        return;
    }

    for (size_t i = row_count; i > 0; i--) {
        writeLogRow(client_fd, rows[i - 1]);
        free(rows[i - 1]);
    }
    free(rows);
}

static void writeLogRow(int client_fd, char* line) {
    char* fields[8] = {0};
    char* cursor = line;
    for (int i = 0; i < 8; i++) {
        fields[i] = cursor;
        char* next = strchr(cursor, '|');
        if (!next) {
            break;
        }
        *next = '\0';
        cursor = next + 1;
    }

    sendAll(client_fd, "<tr>");
    for (int i = 0; i < 8; i++) {
        sendAll(client_fd, "<td>");
        sendEscaped(client_fd, fields[i] ? fields[i] : "");
        sendAll(client_fd, "</td>");
    }
    sendAll(client_fd, "</tr>");
}

static void sendCss(int client_fd) {
    sendAll(client_fd,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/css; charset=utf-8\r\n"
            "Cache-Control: no-store\r\n"
            "Connection: close\r\n\r\n");

    FILE* file = fopen("loggerWeb.css", "r");
    if (!file) {
        return;
    }

    char buffer[2048];
    while (fgets(buffer, sizeof(buffer), file)) {
        sendAll(client_fd, buffer);
    }

    fclose(file);
}

static void sendFile(int client_fd, const char* path) {
    FILE* file = fopen(path, "r");
    if (!file) {
        sendAll(client_fd, "<p>Missing page template.</p>");
        return;
    }

    char buffer[2048];
    while (fgets(buffer, sizeof(buffer), file)) {
        sendAll(client_fd, buffer);
    }

    fclose(file);
}

#endif
