//Statement of Purpose:
/*
The purpose of this file is to provide the implementation for
sending HTTP response headers, static files, and escaped response bodies.
*/

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L

#include "loggerWeb.h"
#include "loggerWebInternal.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

//HTML header for templated pages.
void loggerWebSendHtmlHeader(int client_fd) {
    loggerWebSendAll(client_fd,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Cache-Control: no-store\r\n"
            "Connection: close\r\n\r\n");
}

//Send a 404 Not Found response to the client.
void loggerWebSendNotFound(int client_fd) {
    loggerWebSendAll(client_fd,
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/plain; charset=utf-8\r\n"
            "Connection: close\r\n\r\n"
            "Not found\n");
}

//Send an empty success response for state-changing control requests.
void loggerWebSendNoContent(int client_fd) {
    loggerWebSendAll(client_fd,
            "HTTP/1.1 204 No Content\r\n"
            "Cache-Control: no-store\r\n"
            "Connection: close\r\n\r\n");
}

//Send a short plain-text status response for control failures.
void loggerWebSendPlainStatus(int client_fd,
                              int status_code,
                              const char* reason,
                              const char* body) {
    char header[256];
    int n = snprintf(header,
                     sizeof(header),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: text/plain; charset=utf-8\r\n"
                     "Cache-Control: no-store\r\n"
                     "Connection: close\r\n\r\n",
                     status_code,
                     reason ? reason : "Error");
    if (n > 0 && (size_t)n < sizeof(header)) {
        loggerWebSendAll(client_fd, header);
    }
    loggerWebSendAll(client_fd, body ? body : "");
}

//Send a fixed number of bytes, handling partial socket writes.
void loggerWebSendBytes(int fd, const char* data, size_t length) {
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

//Send a null-terminated string.
void loggerWebSendAll(int fd, const char* data) {
    loggerWebSendBytes(fd, data, strlen(data));
}

//Send text with HTML escaping.
void loggerWebSendEscaped(int fd, const char* value) {
    for (const char* p = value; p && *p; p++) {
        switch (*p) {
            case '&':
                loggerWebSendAll(fd, "&amp;");
                break;
            case '<':
                loggerWebSendAll(fd, "&lt;");
                break;
            case '>':
                loggerWebSendAll(fd, "&gt;");
                break;
            case '"':
                loggerWebSendAll(fd, "&quot;");
                break;
            default: {
                char c[2] = {*p, '\0'};
                loggerWebSendAll(fd, c);
                break;
            }
        }
    }
}

//Send text with JSON escaping.
void loggerWebSendJsonEscaped(int fd, const char* value) {
    for (const unsigned char* p = (const unsigned char*)value; p && *p; p++) {
        switch (*p) {
            case '"':
                loggerWebSendAll(fd, "\\\"");
                break;
            case '\\':
                loggerWebSendAll(fd, "\\\\");
                break;
            case '\b':
                loggerWebSendAll(fd, "\\b");
                break;
            case '\f':
                loggerWebSendAll(fd, "\\f");
                break;
            case '\n':
                loggerWebSendAll(fd, "\\n");
                break;
            case '\r':
                loggerWebSendAll(fd, "\\r");
                break;
            case '\t':
                loggerWebSendAll(fd, "\\t");
                break;
            default:
                if (*p < 0x20) {
                    char escaped[8];
                    snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned)*p);
                    loggerWebSendAll(fd, escaped);
                } else {
                    char c[2] = {(char)*p, '\0'};
                    loggerWebSendAll(fd, c);
                }
                break;
        }
    }
}

//Send a static file with the specified content type.
void loggerWebSendStaticFile(int client_fd, const char* content_type, const char* path) {
    loggerWebSendAll(client_fd,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: ");
    loggerWebSendAll(client_fd, content_type);
    loggerWebSendAll(client_fd,
            "\r\n"
            "Cache-Control: no-store\r\n"
            "Connection: close\r\n\r\n");

    FILE* file = fopen(path, "r");
    if (!file) {
        return;
    }

    char buffer[2048];
    while (fgets(buffer, sizeof(buffer), file)) {
        loggerWebSendAll(client_fd, buffer);
    }

    fclose(file);
}


#endif
