#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L

#include "../loggerWeb.h"
#include "../loggerWebInternal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void freeGraph(LoggerWebGraph* graph);
static LoggerWebGraph* findGraphByTitle(LoggerWebServer* server, const char* title);
static int appendGraphVert(LoggerWebGraph* graph,
                           const char* column,
                           size_t column_index,
                           const char* value,
                           const char* color);
static int appendGraphSpan(LoggerWebGraph* graph,
                           const char* column,
                           size_t column_index,
                           const char* start_value,
                           const char* end_value,
                           const char* color);
static const char* graphRangeName(LoggerWebGraphRange range);
static int graphRangeWindow(LoggerWebGraphRange range,
                            time_t now,
                            time_t* range_start,
                            time_t* range_end);
static int graphDayWindow(time_t now, time_t* range_start, time_t* range_end);
static int localDayStart(time_t value, int day_offset, time_t* out);
static int graphStatsWindow(time_t now, time_t* window_start, time_t* window_end);
static void writeGraphJson(int client_fd,
                           const LoggerWebServer* server,
                           const LoggerWebGraph* graph,
                           time_t range_start,
                           time_t range_end);
static void writeGraphPointsJson(int client_fd,
                                 const LoggerWebServer* server,
                                 const LoggerWebGraph* graph,
                                 time_t range_start,
                                 time_t range_end);
static void writeGraphStatsJson(int client_fd,
                                const LoggerWebServer* server,
                                const LoggerWebGraph* graph);
static void writeGraphEventsJson(int client_fd,
                                 const LoggerWebServer* server,
                                 const LoggerWebGraph* graph,
                                 time_t range_start,
                                 time_t range_end);
static void writeGraphSpansJson(int client_fd,
                                const LoggerWebServer* server,
                                const LoggerWebGraph* graph,
                                time_t range_start,
                                time_t range_end);
static void writeTodayJson(int client_fd, const LoggerWebServer* server);

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
    if (!loggerWebResolveColumnIndex(server, x_column, &x_index)) {
        pthread_mutex_unlock(&active_server_mutex);
        return 0;
    }

    LoggerWebGraph graph;
    memset(&graph, 0, sizeof(graph));
    graph.title = loggerWebCopyString(title);
    graph.x_column = loggerWebCopyString(x_column);
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
            !loggerWebResolveColumnIndex(server, y_columns[i], &graph.series[i].index)) {
            freeGraph(&graph);
            pthread_mutex_unlock(&active_server_mutex);
            return 0;
        }

        graph.series[i].name = loggerWebCopyString(y_columns[i]);
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

int loggerWebShowStats(int enabled) {
    pthread_mutex_lock(&active_server_mutex);
    LoggerWebServer* server = active_server;
    if (!server) {
        pthread_mutex_unlock(&active_server_mutex);
        return 0;
    }

    server->show_stats = enabled != 0;
    pthread_mutex_unlock(&active_server_mutex);
    return 1;
}

int loggerWebShowVerts(const char* graph_title,
                       const char* column,
                       const char* value,
                       const char* color) {
    if (!graph_title || !*graph_title || !column || !*column || !value || !*value) {
        return 0;
    }

    pthread_mutex_lock(&active_server_mutex);
    LoggerWebServer* server = active_server;
    if (!server) {
        pthread_mutex_unlock(&active_server_mutex);
        return 0;
    }

    LoggerWebGraph* graph = findGraphByTitle(server, graph_title);
    size_t column_index = 0;
    if (!graph || !loggerWebResolveColumnIndex(server, column, &column_index)) {
        pthread_mutex_unlock(&active_server_mutex);
        return 0;
    }

    int ok = appendGraphVert(graph,
                             column,
                             column_index,
                             value,
                             color && *color ? color : "#ef4444");
    pthread_mutex_unlock(&active_server_mutex);
    return ok;
}

int loggerWebShowSpan(const char* graph_title,
                      const char* column,
                      const char* start_value,
                      const char* end_value,
                      const char* color) {
    if (!graph_title || !*graph_title || !column || !*column ||
        !start_value || !*start_value || !end_value || !*end_value) {
        return 0;
    }

    pthread_mutex_lock(&active_server_mutex);
    LoggerWebServer* server = active_server;
    if (!server) {
        pthread_mutex_unlock(&active_server_mutex);
        return 0;
    }

    LoggerWebGraph* graph = findGraphByTitle(server, graph_title);
    size_t column_index = 0;
    if (!graph || !loggerWebResolveColumnIndex(server, column, &column_index)) {
        pthread_mutex_unlock(&active_server_mutex);
        return 0;
    }

    int ok = appendGraphSpan(graph,
                             column,
                             column_index,
                             start_value,
                             end_value,
                             color && *color ? color : "#f59e0b");
    pthread_mutex_unlock(&active_server_mutex);
    return ok;
}

int loggerWebShowToday(const char* const* columns,
                       size_t column_count) {
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
    pthread_mutex_unlock(&active_server_mutex);
    return 1;
}

void loggerWebFreeGraphs(LoggerWebServer* server) {
    if (!server || !server->graphs) {
        return;
    }

    for (size_t i = 0; i < server->graph_count; i++) {
        freeGraph(&server->graphs[i]);
    }

    free(server->graphs);
    server->graphs = NULL;
    server->graph_count = 0;
    server->graph_capacity = 0;
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

int loggerWebHasGraphs(const LoggerWebServer* server) {
    int has_graphs = 0;

    pthread_mutex_lock(&active_server_mutex);
    has_graphs = server && server->graph_count > 0;
    pthread_mutex_unlock(&active_server_mutex);

    return has_graphs;
}

LoggerWebGraphRange loggerWebParseGraphRange(const char* request) {
    const char* query = request ? strchr(request, '?') : NULL;
    if (!query) {
        return LOGGER_WEB_GRAPH_RANGE_DAY;
    }

    const char* query_end = strchr(query, ' ');
    if (!query_end) {
        query_end = query + strlen(query);
    }

    const char range_prefix[] = "range=";
    const size_t range_prefix_length = sizeof(range_prefix) - 1;
    const char* cursor = query + 1;
    while (cursor < query_end) {
        const char* param_end = cursor;
        while (param_end < query_end && *param_end != '&') {
            param_end++;
        }

        size_t param_length = (size_t)(param_end - cursor);
        if (param_length >= range_prefix_length &&
            strncmp(cursor, range_prefix, range_prefix_length) == 0) {
            const char* value = cursor + range_prefix_length;
            size_t value_length = param_length - range_prefix_length;
            if (value_length == 10 && strncmp(value, "three-days", 10) == 0) {
                return LOGGER_WEB_GRAPH_RANGE_THREE_DAYS;
            }

            if (value_length == 4 && strncmp(value, "week", 4) == 0) {
                return LOGGER_WEB_GRAPH_RANGE_WEEK;
            }

            return LOGGER_WEB_GRAPH_RANGE_DAY;
        }

        cursor = param_end;
        if (cursor < query_end && *cursor == '&') {
            cursor++;
        }
    }

    return LOGGER_WEB_GRAPH_RANGE_DAY;
}

void loggerWebSendGraphData(int client_fd,
                            const LoggerWebServer* server,
                            LoggerWebGraphRange range) {
    time_t now = time(NULL);
    time_t range_start = now - (time_t)24 * 60 * 60;
    time_t range_end = now;
    if (!graphRangeWindow(range, now, &range_start, &range_end)) {
        if (range == LOGGER_WEB_GRAPH_RANGE_WEEK) {
            range_start = now - (time_t)7 * 24 * 60 * 60;
        } else if (range == LOGGER_WEB_GRAPH_RANGE_THREE_DAYS) {
            range_start = now - (time_t)3 * 24 * 60 * 60;
        }
    }
    char range_start_label[32];
    char range_end_label[32];
    char range_start_unix[32];
    char range_end_unix[32];
    loggerWebFormatUnixLabel(range_start, range_start_label, sizeof(range_start_label));
    loggerWebFormatUnixLabel(range_end, range_end_label, sizeof(range_end_label));
    snprintf(range_start_unix, sizeof(range_start_unix), "%lld", (long long)range_start);
    snprintf(range_end_unix, sizeof(range_end_unix), "%lld", (long long)range_end);

    loggerWebSendAll(client_fd,
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: application/json; charset=utf-8\r\n"
                     "Cache-Control: no-store\r\n"
                     "Connection: close\r\n\r\n"
                     "{\"range\":\"");
    loggerWebSendAll(client_fd, graphRangeName(range));
    loggerWebSendAll(client_fd, "\",\"rangeStart\":\"");
    loggerWebSendJsonEscaped(client_fd, range_start_label);
    loggerWebSendAll(client_fd, "\",\"rangeStartUnix\":");
    loggerWebSendAll(client_fd, range_start_unix);
    loggerWebSendAll(client_fd, ",\"rangeEnd\":\"");
    loggerWebSendJsonEscaped(client_fd, range_end_label);
    loggerWebSendAll(client_fd, "\",\"rangeEndUnix\":");
    loggerWebSendAll(client_fd, range_end_unix);
    loggerWebSendAll(client_fd, ",\"today\":");

    pthread_mutex_lock(&active_server_mutex);
    writeTodayJson(client_fd, server);
    loggerWebSendAll(client_fd, ",\"graphs\":[");
    for (size_t i = 0; i < server->graph_count; i++) {
        if (i > 0) {
            loggerWebSendAll(client_fd, ",");
        }

        writeGraphJson(client_fd, server, &server->graphs[i], range_start, range_end);
    }
    pthread_mutex_unlock(&active_server_mutex);

    loggerWebSendAll(client_fd, "]}");
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

    if (graph->verts) {
        for (size_t i = 0; i < graph->vert_count; i++) {
            free(graph->verts[i].column);
            free(graph->verts[i].value);
            free(graph->verts[i].color);
        }
    }

    free(graph->verts);

    if (graph->spans) {
        for (size_t i = 0; i < graph->span_count; i++) {
            free(graph->spans[i].column);
            free(graph->spans[i].start_value);
            free(graph->spans[i].end_value);
            free(graph->spans[i].color);
        }
    }

    free(graph->spans);
    memset(graph, 0, sizeof(*graph));
}

static LoggerWebGraph* findGraphByTitle(LoggerWebServer* server, const char* title) {
    if (!server || !title) {
        return NULL;
    }

    for (size_t i = 0; i < server->graph_count; i++) {
        if (loggerWebStringEqualsIgnoreCase(server->graphs[i].title, title)) {
            return &server->graphs[i];
        }
    }

    return NULL;
}

static int appendGraphVert(LoggerWebGraph* graph,
                           const char* column,
                           size_t column_index,
                           const char* value,
                           const char* color) {
    if (graph->vert_count == graph->vert_capacity) {
        size_t next_capacity = graph->vert_capacity == 0 ? 2 : graph->vert_capacity * 2;
        LoggerWebVert* next_verts = realloc(graph->verts, next_capacity * sizeof(*graph->verts));
        if (!next_verts) {
            return 0;
        }

        graph->verts = next_verts;
        graph->vert_capacity = next_capacity;
    }

    LoggerWebVert* vert = &graph->verts[graph->vert_count];
    memset(vert, 0, sizeof(*vert));
    vert->column = loggerWebCopyString(column);
    vert->value = loggerWebCopyString(value);
    vert->color = loggerWebCopyString(color);
    vert->column_index = column_index;

    if (!vert->column || !vert->value || !vert->color) {
        free(vert->column);
        free(vert->value);
        free(vert->color);
        memset(vert, 0, sizeof(*vert));
        return 0;
    }

    graph->vert_count++;
    return 1;
}

static int appendGraphSpan(LoggerWebGraph* graph,
                           const char* column,
                           size_t column_index,
                           const char* start_value,
                           const char* end_value,
                           const char* color) {
    if (graph->span_count == graph->span_capacity) {
        size_t next_capacity = graph->span_capacity == 0 ? 2 : graph->span_capacity * 2;
        LoggerWebSpan* next_spans = realloc(graph->spans,
                                            next_capacity * sizeof(*graph->spans));
        if (!next_spans) {
            return 0;
        }

        graph->spans = next_spans;
        graph->span_capacity = next_capacity;
    }

    LoggerWebSpan* span = &graph->spans[graph->span_count];
    memset(span, 0, sizeof(*span));
    span->column = loggerWebCopyString(column);
    span->start_value = loggerWebCopyString(start_value);
    span->end_value = loggerWebCopyString(end_value);
    span->color = loggerWebCopyString(color);
    span->column_index = column_index;

    if (!span->column || !span->start_value || !span->end_value || !span->color) {
        free(span->column);
        free(span->start_value);
        free(span->end_value);
        free(span->color);
        memset(span, 0, sizeof(*span));
        return 0;
    }

    graph->span_count++;
    return 1;
}

static const char* graphRangeName(LoggerWebGraphRange range) {
    if (range == LOGGER_WEB_GRAPH_RANGE_THREE_DAYS) {
        return "three-days";
    }

    return range == LOGGER_WEB_GRAPH_RANGE_WEEK ? "week" : "day";
}

static int graphRangeWindow(LoggerWebGraphRange range,
                            time_t now,
                            time_t* range_start,
                            time_t* range_end) {
    if (!range_start || !range_end) {
        return 0;
    }

    if (range == LOGGER_WEB_GRAPH_RANGE_DAY) {
        return graphDayWindow(now, range_start, range_end);
    }

    if (range == LOGGER_WEB_GRAPH_RANGE_THREE_DAYS) {
        if (!localDayStart(now, -2, range_start) ||
            !localDayStart(now, 1, range_end)) {
            return 0;
        }

        return 1;
    }

    if (!localDayStart(now, -6, range_start) ||
        !localDayStart(now, 1, range_end)) {
        return 0;
    }

    return 1;
}

static int graphDayWindow(time_t now, time_t* range_start, time_t* range_end) {
    if (!range_start || !range_end) {
        return 0;
    }

    struct tm local;
    if (!loggerWebLogLocaltime(&now, &local)) {
        return 0;
    }

    struct tm start_local = local;
    start_local.tm_mday -= 1;
    start_local.tm_hour = (local.tm_hour / 2) * 2;
    start_local.tm_min = 0;
    start_local.tm_sec = 0;
    start_local.tm_isdst = -1;

    struct tm end_local = local;
    int end_hour = (local.tm_hour / 2) * 2;
    if ((local.tm_hour % 2) != 0 || local.tm_min > 0 || local.tm_sec > 0) {
        end_hour += 2;
    }
    end_local.tm_hour = end_hour;
    end_local.tm_min = 0;
    end_local.tm_sec = 0;
    end_local.tm_isdst = -1;

    time_t start = mktime(&start_local);
    time_t end = mktime(&end_local);
    if (start == (time_t)-1 || end == (time_t)-1 || end <= start) {
        return 0;
    }

    *range_start = start;
    *range_end = end;
    return 1;
}

static int localDayStart(time_t value, int day_offset, time_t* out) {
    if (!out) {
        return 0;
    }

    struct tm local;
    if (!loggerWebLogLocaltime(&value, &local)) {
        return 0;
    }

    local.tm_mday += day_offset;
    local.tm_hour = 0;
    local.tm_min = 0;
    local.tm_sec = 0;
    local.tm_isdst = -1;

    time_t start = mktime(&local);
    if (start == (time_t)-1) {
        return 0;
    }

    *out = start;
    return 1;
}

static int graphStatsWindow(time_t now, time_t* window_start, time_t* window_end) {
    if (!window_start || !window_end) {
        return 0;
    }

    struct tm local_now;
    if (!loggerWebLogLocaltime(&now, &local_now)) {
        return 0;
    }

    local_now.tm_hour = 0;
    local_now.tm_min = 0;
    local_now.tm_sec = 0;
    local_now.tm_isdst = -1;

    time_t today_start = mktime(&local_now);
    if (today_start == (time_t)-1) {
        return 0;
    }

    time_t last_24_hours = now - (time_t)24 * 60 * 60;
    *window_start = last_24_hours > today_start ? last_24_hours : today_start;
    *window_end = now;
    return 1;
}

static void writeGraphJson(int client_fd,
                           const LoggerWebServer* server,
                           const LoggerWebGraph* graph,
                           time_t range_start,
                           time_t range_end) {
    loggerWebSendAll(client_fd, "{\"title\":\"");
    loggerWebSendJsonEscaped(client_fd, graph->title);
    loggerWebSendAll(client_fd, "\",\"xColumn\":\"");
    loggerWebSendJsonEscaped(client_fd, graph->x_column);
    loggerWebSendAll(client_fd, "\",\"series\":[");

    for (size_t i = 0; i < graph->series_count; i++) {
        if (i > 0) {
            loggerWebSendAll(client_fd, ",");
        }

        loggerWebSendAll(client_fd, "{\"name\":\"");
        loggerWebSendJsonEscaped(client_fd, graph->series[i].name);
        loggerWebSendAll(client_fd, "\"}");
    }

    loggerWebSendAll(client_fd, "],\"points\":[");
    writeGraphPointsJson(client_fd, server, graph, range_start, range_end);
    loggerWebSendAll(client_fd, "],\"stats\":");
    writeGraphStatsJson(client_fd, server, graph);
    loggerWebSendAll(client_fd, ",\"events\":[");
    writeGraphEventsJson(client_fd, server, graph, range_start, range_end);
    loggerWebSendAll(client_fd, "],\"spans\":[");
    writeGraphSpansJson(client_fd, server, graph, range_start, range_end);
    loggerWebSendAll(client_fd, "]}");
}

static void writeGraphPointsJson(int client_fd,
                                 const LoggerWebServer* server,
                                 const LoggerWebGraph* graph,
                                 time_t range_start,
                                 time_t range_end) {
    size_t column_count = loggerWebTotalColumnCount(server);
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

        loggerWebSplitFields(line, fields, column_count);
        time_t logged_at = 0;
        if (!loggerWebParseUnixTime(fields[0], &logged_at) ||
            logged_at < range_start || logged_at > range_end) {
            continue;
        }

        char x_text[64];
        if (graph->x_index == LOGGER_WEB_UNIX_FIELD ||
            graph->x_index == LOGGER_WEB_DATE_FIELD ||
            graph->x_index == LOGGER_WEB_TIME_FIELD) {
            loggerWebFormatUnixLabel(logged_at, x_text, sizeof(x_text));
        } else {
            const char* field = loggerWebFieldForColumn(fields, graph->x_index);
            if (!field || !*field) {
                continue;
            }
            snprintf(x_text, sizeof(x_text), "%s", field);
        }

        int any_value = 0;
        for (size_t i = 0; i < graph->series_count; i++) {
            has_value[i] = 0;
            values[i] = 0.0;

            const char* y_text = loggerWebFieldForColumn(fields, graph->series[i].index);
            if (loggerWebParseDouble(y_text, &values[i])) {
                has_value[i] = 1;
                any_value = 1;
            }
        }

        if (!any_value) {
            continue;
        }

        if (wrote_point) {
            loggerWebSendAll(client_fd, ",");
        }

        loggerWebSendAll(client_fd, "{\"x\":\"");
        loggerWebSendJsonEscaped(client_fd, x_text);
        loggerWebSendAll(client_fd, "\",\"time\":");
        char time_text[32];
        snprintf(time_text, sizeof(time_text), "%lld", (long long)logged_at);
        loggerWebSendAll(client_fd, time_text);
        loggerWebSendAll(client_fd, ",\"values\":[");
        for (size_t i = 0; i < graph->series_count; i++) {
            if (i > 0) {
                loggerWebSendAll(client_fd, ",");
            }

            if (has_value[i]) {
                char number[64];
                snprintf(number, sizeof(number), "%.17g", values[i]);
                loggerWebSendAll(client_fd, number);
            } else {
                loggerWebSendAll(client_fd, "null");
            }
        }
        loggerWebSendAll(client_fd, "]}");
        wrote_point = 1;
    }

    free(fields);
    free(values);
    free(has_value);
    fclose(file);
}

static void writeGraphStatsJson(int client_fd,
                                const LoggerWebServer* server,
                                const LoggerWebGraph* graph) {
    if (!server->show_stats) {
        loggerWebSendAll(client_fd, "null");
        return;
    }

    time_t window_start = 0;
    time_t window_end = 0;
    if (!graphStatsWindow(time(NULL), &window_start, &window_end)) {
        loggerWebSendAll(client_fd, "null");
        return;
    }

    double* mins = calloc(graph->series_count, sizeof(*mins));
    double* maxes = calloc(graph->series_count, sizeof(*maxes));
    int* has_value = calloc(graph->series_count, sizeof(*has_value));
    if (!mins || !maxes || !has_value) {
        free(mins);
        free(maxes);
        free(has_value);
        loggerWebSendAll(client_fd, "null");
        return;
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
            if (!loggerWebParseUnixTime(fields[0], &logged_at) ||
                logged_at < window_start || logged_at > window_end) {
                continue;
            }

            for (size_t i = 0; i < graph->series_count; i++) {
                double value = 0.0;
                if (!loggerWebParseDouble(
                        loggerWebFieldForColumn(fields, graph->series[i].index),
                        &value)) {
                    continue;
                }

                if (!has_value[i]) {
                    mins[i] = value;
                    maxes[i] = value;
                    has_value[i] = 1;
                } else {
                    if (value < mins[i]) {
                        mins[i] = value;
                    }
                    if (value > maxes[i]) {
                        maxes[i] = value;
                    }
                }
            }
        }

        free(fields);
        fclose(file);
    }

    char start_label[32];
    char end_label[32];
    loggerWebFormatUnixLabel(window_start, start_label, sizeof(start_label));
    loggerWebFormatUnixLabel(window_end, end_label, sizeof(end_label));

    loggerWebSendAll(client_fd, "{\"windowStart\":\"");
    loggerWebSendJsonEscaped(client_fd, start_label);
    loggerWebSendAll(client_fd, "\",\"windowEnd\":\"");
    loggerWebSendJsonEscaped(client_fd, end_label);
    loggerWebSendAll(client_fd, "\",\"series\":[");

    for (size_t i = 0; i < graph->series_count; i++) {
        if (i > 0) {
            loggerWebSendAll(client_fd, ",");
        }

        loggerWebSendAll(client_fd, "{\"name\":\"");
        loggerWebSendJsonEscaped(client_fd, graph->series[i].name);
        loggerWebSendAll(client_fd, "\",\"min\":");
        if (has_value[i]) {
            char number[64];
            snprintf(number, sizeof(number), "%.17g", mins[i]);
            loggerWebSendAll(client_fd, number);
        } else {
            loggerWebSendAll(client_fd, "null");
        }
        loggerWebSendAll(client_fd, ",\"max\":");
        if (has_value[i]) {
            char number[64];
            snprintf(number, sizeof(number), "%.17g", maxes[i]);
            loggerWebSendAll(client_fd, number);
        } else {
            loggerWebSendAll(client_fd, "null");
        }
        loggerWebSendAll(client_fd, "}");
    }

    loggerWebSendAll(client_fd, "]}");
    free(mins);
    free(maxes);
    free(has_value);
}

static void writeGraphEventsJson(int client_fd,
                                 const LoggerWebServer* server,
                                 const LoggerWebGraph* graph,
                                 time_t range_start,
                                 time_t range_end) {
    if (graph->vert_count == 0) {
        return;
    }

    size_t column_count = loggerWebTotalColumnCount(server);
    FILE* file = fopen(server->log_path, "r");
    if (!file) {
        return;
    }

    char** fields = calloc(column_count, sizeof(*fields));
    if (!fields) {
        fclose(file);
        return;
    }

    int wrote_event = 0;
    char line[LOGGER_WEB_MAX_LINE];
    while (fgets(line, sizeof(line), file)) {
        char* newline = strpbrk(line, "\r\n");
        if (newline) {
            *newline = '\0';
        }

        loggerWebSplitFields(line, fields, column_count);
        time_t logged_at = 0;
        if (!loggerWebParseUnixTime(fields[0], &logged_at) ||
            logged_at < range_start || logged_at > range_end) {
            continue;
        }

        char x_text[64];
        loggerWebFormatUnixLabel(logged_at, x_text, sizeof(x_text));

        for (size_t i = 0; i < graph->vert_count; i++) {
            const char* field = loggerWebFieldForColumn(fields, graph->verts[i].column_index);
            if (!field || strcmp(field, graph->verts[i].value) != 0) {
                continue;
            }

            if (wrote_event) {
                loggerWebSendAll(client_fd, ",");
            }

            loggerWebSendAll(client_fd, "{\"x\":\"");
            loggerWebSendJsonEscaped(client_fd, x_text);
            loggerWebSendAll(client_fd, "\",\"time\":");
            char time_text[32];
            snprintf(time_text, sizeof(time_text), "%lld", (long long)logged_at);
            loggerWebSendAll(client_fd, time_text);
            loggerWebSendAll(client_fd, ",\"label\":\"");
            loggerWebSendJsonEscaped(client_fd, graph->verts[i].value);
            loggerWebSendAll(client_fd, "\",\"color\":\"");
            loggerWebSendJsonEscaped(client_fd, graph->verts[i].color);
            loggerWebSendAll(client_fd, "\"}");
            wrote_event = 1;
        }
    }

    free(fields);
    fclose(file);
}

static void writeGraphSpansJson(int client_fd,
                                const LoggerWebServer* server,
                                const LoggerWebGraph* graph,
                                time_t range_start,
                                time_t range_end) {
    if (graph->span_count == 0) {
        return;
    }

    size_t column_count = loggerWebTotalColumnCount(server);
    int wrote_span = 0;

    for (size_t span_index = 0; span_index < graph->span_count; span_index++) {
        const LoggerWebSpan* span = &graph->spans[span_index];
        FILE* file = fopen(server->log_path, "r");
        if (!file) {
            return;
        }

        char** fields = calloc(column_count, sizeof(*fields));
        if (!fields) {
            fclose(file);
            return;
        }

        int has_start = 0;
        time_t start_time = 0;
        char start_x[256] = "";
        char line[LOGGER_WEB_MAX_LINE];
        while (fgets(line, sizeof(line), file)) {
            char* newline = strpbrk(line, "\r\n");
            if (newline) {
                *newline = '\0';
            }

            loggerWebSplitFields(line, fields, column_count);
            time_t logged_at = 0;
            if (!loggerWebParseUnixTime(fields[0], &logged_at) ||
                logged_at < range_start || logged_at > range_end) {
                continue;
            }

            const char* field = loggerWebFieldForColumn(fields, span->column_index);
            if (!field) {
                continue;
            }

            if (strcmp(field, span->start_value) == 0) {
                if (!has_start) {
                    has_start = 1;
                    start_time = logged_at;
                    loggerWebFormatUnixLabel(logged_at, start_x, sizeof(start_x));
                }
                continue;
            }

            if (strcmp(field, span->end_value) == 0 && has_start && logged_at >= start_time) {
                time_t duration_seconds = logged_at - start_time;
                char duration[64];
                char seconds_text[64];
                loggerWebFormatDuration(duration_seconds, duration, sizeof(duration));
                snprintf(seconds_text, sizeof(seconds_text), "%lld", (long long)duration_seconds);

                if (wrote_span) {
                    loggerWebSendAll(client_fd, ",");
                }

                loggerWebSendAll(client_fd, "{\"start\":\"");
                loggerWebSendJsonEscaped(client_fd, start_x);
                loggerWebSendAll(client_fd, "\",\"startTime\":");
                char start_time_text[32];
                snprintf(start_time_text, sizeof(start_time_text), "%lld", (long long)start_time);
                loggerWebSendAll(client_fd, start_time_text);
                loggerWebSendAll(client_fd, ",\"end\":\"");
                char end_x[256];
                loggerWebFormatUnixLabel(logged_at, end_x, sizeof(end_x));
                loggerWebSendJsonEscaped(client_fd, end_x);
                loggerWebSendAll(client_fd, "\",\"endTime\":");
                char end_time_text[32];
                snprintf(end_time_text, sizeof(end_time_text), "%lld", (long long)logged_at);
                loggerWebSendAll(client_fd, end_time_text);
                loggerWebSendAll(client_fd, ",\"label\":\"");
                loggerWebSendJsonEscaped(client_fd, duration);
                loggerWebSendAll(client_fd, "\",\"durationSeconds\":");
                loggerWebSendAll(client_fd, seconds_text);
                loggerWebSendAll(client_fd, ",\"duration\":\"");
                loggerWebSendJsonEscaped(client_fd, duration);
                loggerWebSendAll(client_fd, "\",\"color\":\"");
                loggerWebSendJsonEscaped(client_fd, span->color);
                loggerWebSendAll(client_fd, "\"}");

                wrote_span = 1;
                has_start = 0;
                start_time = 0;
                start_x[0] = '\0';
            }
        }

        free(fields);
        fclose(file);
    }
}

static void writeTodayJson(int client_fd, const LoggerWebServer* server) {
    if (server->today_column_count == 0) {
        loggerWebSendAll(client_fd, "null");
        return;
    }

    if (server->today_column_count > 16) {
        loggerWebSendAll(client_fd, "null");
        return;
    }

    size_t column_count = loggerWebTotalColumnCount(server);
    double values[16] = {0};
    int has_value[16] = {0};
    char latest_time[128] = "";
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
            double row_values[16] = {0};
            int row_has_value[16] = {0};

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
                loggerWebFormatUnixTime(logged_at, latest_time, sizeof(latest_time));
            } else if (loggerWebRowHasSplitDateTime(fields)) {
                snprintf(latest_time, sizeof(latest_time), "%s", fields[LOGGER_WEB_TIME_FIELD]);
            } else {
                snprintf(latest_time,
                         sizeof(latest_time),
                         "%s",
                         fields[LOGGER_WEB_DATE_FIELD] ? fields[LOGGER_WEB_DATE_FIELD] : "");
            }
        }

        free(fields);
        fclose(file);
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
