//Statement of Purpose:
/*
The purpose of this file is to provide the implementation for sending log rows
over the web interface. It reads structured LogRecord lines, caps memory use
with a ring buffer, and sends the newest rows first.
*/

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "loggerWeb.h"
#include "loggerWebInternal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void sendColspanMessage(PortableSocket client_fd,
                               size_t column_count,
                               const char* message);
static size_t displayedColumnCount(const LoggerWebServer* server);
static void writeLogRow(PortableSocket client_fd,
                        char* line,
                        const LoggerWebServer* server);

void loggerWebSendRawLogContent(PortableSocket client_fd, const LoggerWebServer* server) {
    FILE* file = fopen(server->log_path, "r");
    if (!file) {
        return;
    }

    char buffer[2048];
    while (fgets(buffer, sizeof(buffer), file)) {
        loggerWebSendEscaped(client_fd, buffer);
    }

    fclose(file);
}

static void sendColspanMessage(PortableSocket client_fd,
                               size_t column_count,
                               const char* message) {
    char colspan[32];
    snprintf(colspan, sizeof(colspan), "%zu", column_count);

    loggerWebSendAll(client_fd, "<tr><td colspan=\"");
    loggerWebSendAll(client_fd, colspan);
    loggerWebSendAll(client_fd, "\">");
    loggerWebSendEscaped(client_fd, message);
    loggerWebSendAll(client_fd, "</td></tr>");
}

size_t loggerWebTotalColumnCount(const LoggerWebServer* server) {
    return server->column_header_count + LOGGER_WEB_DATA_FIELD;
}

static size_t displayedColumnCount(const LoggerWebServer* server) {
    return loggerWebTotalColumnCount(server) - 1;
}

const char* loggerWebFieldForColumn(const LogRecord* record, size_t column_index) {
    if (!record) {
        return NULL;
    }

    if (column_index == LOGGER_WEB_UNIX_FIELD ||
        column_index == LOGGER_WEB_DATE_FIELD ||
        column_index == LOGGER_WEB_TIME_FIELD) {
        return NULL;
    }

    size_t field_index = column_index - LOGGER_WEB_DATA_FIELD;
    if (field_index >= record->field_count) {
        return NULL;
    }

    return record->fields[field_index];
}

void loggerWebSendLogRows(PortableSocket client_fd,
                          const LoggerWebServer* server,
                          size_t limit) {
    size_t display_column_count = displayedColumnCount(server);
    if (limit == 0) {
        limit = server && server->log_row_limit
            ? server->log_row_limit
            : LOGGER_WEB_DEFAULT_LOG_LIMIT;
    }

    FILE* file = fopen(server->log_path, "r");
    if (!file) {
        sendColspanMessage(client_fd, display_column_count, "No log file found.");
        return;
    }

    char** rows = calloc(limit, sizeof(*rows));
    if (!rows) {
        fclose(file);
        sendColspanMessage(client_fd, display_column_count, "Unable to load log rows.");
        return;
    }

    size_t total_rows = 0;
    char line[LOGGER_WEB_MAX_LINE];

    while (fgets(line, sizeof(line), file)) {
        char* newline = strpbrk(line, "\r\n");
        if (newline) {
            *newline = '\0';
        }

        size_t slot = total_rows % limit;
        free(rows[slot]);
        rows[slot] = loggerWebCopyString(line);
        if (!rows[slot]) {
            for (size_t i = 0; i < limit; i++) {
                free(rows[i]);
            }
            free(rows);
            fclose(file);
            sendColspanMessage(client_fd, display_column_count, "Unable to load log rows.");
            return;
        }

        total_rows++;
    }

    fclose(file);

    if (total_rows == 0) {
        sendColspanMessage(client_fd, display_column_count, "Log file is empty.");
        free(rows);
        return;
    }

    size_t row_count = total_rows < limit ? total_rows : limit;
    for (size_t i = 0; i < row_count; i++) {
        size_t slot = (total_rows - 1 - i) % limit;
        writeLogRow(client_fd, rows[slot], server);
    }

    for (size_t i = 0; i < limit; i++) {
        free(rows[i]);
    }
    free(rows);
}

static void writeLogRow(PortableSocket client_fd,
                        char* line,
                        const LoggerWebServer* server) {
    size_t display_column_count = displayedColumnCount(server);

    LogRecord record;
    if (!logger_record_parse_line(line, &record)) {
        sendColspanMessage(client_fd, display_column_count, "Unable to parse log row.");
        return;
    }

    char date_text[32] = "";
    char time_text[32] = "";
    if (record.has_logged_at) {
        loggerWebFormatUnixDate(record.logged_at, date_text, sizeof(date_text));
        loggerWebFormatUnixTime(record.logged_at, time_text, sizeof(time_text));
    }

    loggerWebSendAll(client_fd, "<tr>");
    loggerWebSendAll(client_fd, "<td>");
    loggerWebSendEscaped(client_fd, date_text);
    loggerWebSendAll(client_fd, "</td><td>");
    loggerWebSendEscaped(client_fd, time_text);
    loggerWebSendAll(client_fd, "</td>");

    for (size_t i = 0; i < server->column_header_count; i++) {
        const char* field = loggerWebFieldForColumn(&record, i + LOGGER_WEB_DATA_FIELD);
        loggerWebSendAll(client_fd, "<td>");
        loggerWebSendEscaped(client_fd, field ? field : "");
        loggerWebSendAll(client_fd, "</td>");
    }
    loggerWebSendAll(client_fd, "</tr>");
}
