#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L

#include "../loggerWeb.h"
#include "../loggerWebInternal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOGGER_WEB_TODAY_MAX_COLUMNS 16

static int readTodayValues(const LoggerWebServer* server,
                           double* values,
                           int* has_value,
                           char* latest_time,
                           size_t latest_time_size);

int loggerWebShowToday(const char* const* columns,
                       size_t column_count,
                       int show_on_other_pages) {
    if (column_count > 0 && !columns) {
        return 0;
    }

    pthread_mutex_lock(&active_server_mutex);
    LoggerWebServer* server = active_server;
    if (!server) {
        pthread_mutex_unlock(&active_server_mutex);
        return 0;
    }

    LoggerWebTodayColumn* next_columns = NULL;
    if (column_count > 0) {
        next_columns = calloc(column_count, sizeof(*next_columns));
        if (!next_columns) {
            pthread_mutex_unlock(&active_server_mutex);
            return 0;
        }

        for (size_t i = 0; i < column_count; i++) {
            if (!columns[i] || !*columns[i] ||
                !loggerWebResolveColumnIndex(server, columns[i], &next_columns[i].index)) {
                for (size_t j = 0; j < i; j++) {
                    free(next_columns[j].name);
                }
                free(next_columns);
                pthread_mutex_unlock(&active_server_mutex);
                return 0;
            }

            next_columns[i].name = loggerWebCopyString(columns[i]);
            if (!next_columns[i].name) {
                for (size_t j = 0; j < i; j++) {
                    free(next_columns[j].name);
                }
                free(next_columns);
                pthread_mutex_unlock(&active_server_mutex);
                return 0;
            }
        }
    }

    loggerWebFreeTodayColumns(server);
    server->today_columns = next_columns;
    server->today_column_count = column_count;
    server->show_today_on_other_pages = show_on_other_pages != 0;
    pthread_mutex_unlock(&active_server_mutex);
    return 1;
}

void loggerWebFreeTodayColumns(LoggerWebServer* server) {
    if (!server || !server->today_columns) {
        return;
    }

    for (size_t i = 0; i < server->today_column_count; i++) {
        free(server->today_columns[i].name);
    }

    free(server->today_columns);
    server->today_columns = NULL;
    server->today_column_count = 0;
}

void loggerWebSendTodayPanel(int client_fd, const LoggerWebServer* server) {
    if (!server) {
        return;
    }

    pthread_mutex_lock(&active_server_mutex);
    double values[LOGGER_WEB_TODAY_MAX_COLUMNS] = {0};
    int has_value[LOGGER_WEB_TODAY_MAX_COLUMNS] = {0};
    char latest_time[128] = "";
    if (!readTodayValues(server,
                         values,
                         has_value,
                         latest_time,
                         sizeof(latest_time))) {
        pthread_mutex_unlock(&active_server_mutex);
        return;
    }

    loggerWebSendAll(client_fd, "<section class=\"today-panel\">");
    loggerWebSendAll(client_fd, "<div class=\"today-heading\">Current readings</div>");
    loggerWebSendAll(client_fd, "<div class=\"today-readings\">");
    for (size_t i = 0; i < server->today_column_count; i++) {
        loggerWebSendAll(client_fd, "<div class=\"today-reading\"><span class=\"today-label\">");
        loggerWebSendEscaped(client_fd, server->today_columns[i].name);
        loggerWebSendAll(client_fd, "</span><span class=\"today-value\">");
        if (has_value[i]) {
            char number[64];
            snprintf(number, sizeof(number), "%.17g", values[i]);
            loggerWebSendEscaped(client_fd, number);
        } else {
            loggerWebSendAll(client_fd, "--");
        }
        loggerWebSendAll(client_fd, "</span></div>");
    }
    loggerWebSendAll(client_fd, "</div>");
    if (latest_time[0]) {
        loggerWebSendAll(client_fd, "<div class=\"today-updated\">Updated ");
        loggerWebSendEscaped(client_fd, latest_time);
        loggerWebSendAll(client_fd, "</div>");
    }
    loggerWebSendAll(client_fd, "</section>");
    pthread_mutex_unlock(&active_server_mutex);
}

static int readTodayValues(const LoggerWebServer* server,
                           double* values,
                           int* has_value,
                           char* latest_time,
                           size_t latest_time_size) {
    if (!server || !values || !has_value || !latest_time || latest_time_size == 0 ||
        server->today_column_count == 0 ||
        server->today_column_count > LOGGER_WEB_TODAY_MAX_COLUMNS) {
        return 0;
    }

    latest_time[0] = '\0';
    for (size_t i = 0; i < LOGGER_WEB_TODAY_MAX_COLUMNS; i++) {
        values[i] = 0.0;
        has_value[i] = 0;
    }

    size_t column_count = loggerWebTotalColumnCount(server);
    FILE* file = fopen(server->log_path, "r");
    if (file) {
        char** fields = calloc(column_count, sizeof(*fields));
        char line[LOGGER_WEB_MAX_LINE];
        while (fields && fgets(line, sizeof(line), file)) {
            char* newline = strpbrk(line, "\r\n");
            if (newline) {
                *newline = '\0';
            }

            loggerWebSplitFields(line, fields, column_count);
            time_t logged_at = 0;
            int has_logged_at = loggerWebParseUnixTime(fields[LOGGER_WEB_UNIX_FIELD], &logged_at);
            int any_value = 0;
            double row_values[LOGGER_WEB_TODAY_MAX_COLUMNS] = {0};
            int row_has_value[LOGGER_WEB_TODAY_MAX_COLUMNS] = {0};

            for (size_t i = 0; i < server->today_column_count; i++) {
                const char* field = loggerWebFieldForColumn(
                    fields,
                    server->today_columns[i].index);
                if (loggerWebParseDouble(field, &row_values[i])) {
                    row_has_value[i] = 1;
                    any_value = 1;
                }
            }

            if (!any_value) {
                continue;
            }

            for (size_t i = 0; i < server->today_column_count; i++) {
                values[i] = row_values[i];
                has_value[i] = row_has_value[i];
            }
            if (has_logged_at) {
                loggerWebFormatUnixTime(logged_at, latest_time, latest_time_size);
            } else if (loggerWebRowHasSplitDateTime(fields)) {
                snprintf(latest_time,
                         latest_time_size,
                         "%s",
                         fields[LOGGER_WEB_TIME_FIELD]);
            } else {
                snprintf(latest_time,
                         latest_time_size,
                         "%s",
                         fields[LOGGER_WEB_DATE_FIELD] ? fields[LOGGER_WEB_DATE_FIELD] : "");
            }
        }

        free(fields);
        fclose(file);
    }

    return 1;
}

void loggerWebWriteTodayJson(int client_fd, const LoggerWebServer* server) {
    double values[LOGGER_WEB_TODAY_MAX_COLUMNS] = {0};
    int has_value[LOGGER_WEB_TODAY_MAX_COLUMNS] = {0};
    char latest_time[128] = "";
    if (!readTodayValues(server,
                         values,
                         has_value,
                         latest_time,
                         sizeof(latest_time))) {
        loggerWebSendAll(client_fd, "null");
        return;
    }

    loggerWebSendAll(client_fd, "{\"time\":\"");
    loggerWebSendJsonEscaped(client_fd, latest_time);
    loggerWebSendAll(client_fd, "\",\"columns\":[");
    for (size_t i = 0; i < server->today_column_count; i++) {
        if (i > 0) {
            loggerWebSendAll(client_fd, ",");
        }

        loggerWebSendAll(client_fd, "{\"name\":\"");
        loggerWebSendJsonEscaped(client_fd, server->today_columns[i].name);
        loggerWebSendAll(client_fd, "\",\"value\":");
        if (has_value[i]) {
            char number[64];
            snprintf(number, sizeof(number), "%.17g", values[i]);
            loggerWebSendAll(client_fd, number);
        } else {
            loggerWebSendAll(client_fd, "null");
        }
        loggerWebSendAll(client_fd, "}");
    }
    loggerWebSendAll(client_fd, "]}");
}


#endif
