//Statement of Purpose:
/*
* This file contains the implementation for sending graph data in JSON format
* to the client for the logger web server. It includes functions to read the 
* log file, extract relevant data points, and format them as JSON for graphing.
*/
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L

#include "../loggerWeb.h"
#include "../loggerWebInternal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

//Function prototypes for internal functions used in the logger web server
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

//Send the graph data in JSON format to the client for the specified range
void loggerWebSendGraphData(int client_fd,
                            const LoggerWebServer* server,
                            LoggerWebGraphRange range) {
    
    //Get the current time and calculate the start and end times
    time_t now = time(NULL);
    time_t range_start = now - (time_t)24 * 60 * 60;
    time_t range_end = now;

    //If the range is not valid, default to the last 24 hours
    if (!loggerWebGraphRangeWindow(range, now, &range_start, &range_end)) {
        if (range == LOGGER_WEB_GRAPH_RANGE_WEEK) {
            range_start = now - (time_t)7 * 24 * 60 * 60;
        } else if (range == LOGGER_WEB_GRAPH_RANGE_THREE_DAYS) {
            range_start = now - (time_t)3 * 24 * 60 * 60;
        }
    }
    
    //Format the range start and end times into human-readable labels and Unix timestamps
    char range_start_label[32];
    char range_end_label[32];
    char range_start_unix[32];
    char range_end_unix[32];

    loggerWebFormatUnixLabel(range_start, range_start_label, sizeof(range_start_label));
    loggerWebFormatUnixLabel(range_end, range_end_label, sizeof(range_end_label));
    snprintf(range_start_unix, sizeof(range_start_unix), "%lld", (long long)range_start);
    snprintf(range_end_unix, sizeof(range_end_unix), "%lld", (long long)range_end);

    //Send the HTTP response header and the JSON data to the client
    loggerWebSendAll(client_fd,
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: application/json; charset=utf-8\r\n"
                     "Cache-Control: no-store\r\n"
                     "Connection: close\r\n\r\n"
                     "{\"range\":\"");
    loggerWebSendAll(client_fd, loggerWebGraphRangeName(range));
    loggerWebSendAll(client_fd, "\",\"rangeStart\":\"");
    loggerWebSendJsonEscaped(client_fd, range_start_label);
    loggerWebSendAll(client_fd, "\",\"rangeStartUnix\":");
    loggerWebSendAll(client_fd, range_start_unix);
    loggerWebSendAll(client_fd, ",\"rangeEnd\":\"");
    loggerWebSendJsonEscaped(client_fd, range_end_label);
    loggerWebSendAll(client_fd, "\",\"rangeEndUnix\":");
    loggerWebSendAll(client_fd, range_end_unix);
    loggerWebSendAll(client_fd, ",\"today\":");

    //Lock the active server mutex to ensure thread safety
    pthread_mutex_lock(&active_server_mutex);

    //Send the "today" data in JSON format to the client
    loggerWebWriteTodayJson(client_fd, server);
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

//Write the graph data in JSON format for the specified graph and range
//Send the graph title, x-axis column, series data, 
//points, stats, events, and spans in JSON format to the client
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

//Write the graph points in JSON format for the specified graph and range
static void writeGraphPointsJson(int client_fd,
                                 const LoggerWebServer* server,
                                 const LoggerWebGraph* graph,
                                 time_t range_start,
                                 time_t range_end) {
    //Check the file and column count
    size_t column_count = loggerWebTotalColumnCount(server);
    FILE* file = fopen(server->log_path, "r");
    if (!file) {
        return;
    }

    //Allocate memory for the fields, values, and has_value arrays
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

    //Read the log file, extract the relevant data points, and format them as JSON
    int wrote_point = 0;
    char line[LOGGER_WEB_MAX_LINE];

    while (fgets(line, sizeof(line), file)) {
        //Remove any trailing newline characters from the line
        char* newline = strpbrk(line, "\r\n");
        if (newline) {
            *newline = '\0';
        }

        //Split the line into fields and check it's within the specified range
        loggerWebSplitFields(line, fields, column_count);
        time_t logged_at = 0;
        if (!loggerWebParseUnixTime(fields[0], &logged_at) ||
            logged_at < range_start || logged_at > range_end) {
            continue;
        }

        //Determine the x-axis value based on the graph's x_index and format it
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

        //Extract the y-axis values for each series
        int any_value = 0;

        //Check if any of the series have a valid value for this data point
        for (size_t i = 0; i < graph->series_count; i++) {
            has_value[i] = 0;
            values[i] = 0.0;

            const char* y_text = loggerWebFieldForColumn(fields, graph->series[i].index);
            if (loggerWebParseDouble(y_text, &values[i])) {
                has_value[i] = 1;
                any_value = 1;
            }
        }

        //If none of the series have a valid value, skip this data point
        if (!any_value) {
            continue;
        }

        //Write the data point in JSON format to the client
        if (wrote_point) {
            loggerWebSendAll(client_fd, ",");
        }

        //Send the x-axis value, Unix timestamp, and y-axis values for each series
        loggerWebSendAll(client_fd, "{\"x\":\"");
        loggerWebSendJsonEscaped(client_fd, x_text);
        loggerWebSendAll(client_fd, "\",\"time\":");
        
        char time_text[32];
        snprintf(time_text, sizeof(time_text), "%lld", (long long)logged_at);
        loggerWebSendAll(client_fd, time_text);
        loggerWebSendAll(client_fd, ",\"values\":[");

        //Send the y-axis values for each series, using "null" for 
        //missing values
        for (size_t i = 0; i < graph->series_count; i++) {
            if (i > 0) {
                loggerWebSendAll(client_fd, ",");
            }

            //Send the value for the series
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

    //Clean up allocated memory and close the file
    free(fields);
    free(values);
    free(has_value);
    fclose(file);
}

//Write the graph statistics in JSON format for the specified graph
static void writeGraphStatsJson(int client_fd,
                                const LoggerWebServer* server,
                                const LoggerWebGraph* graph) {
    //If the server is not configured to show statistics, send "null" and return                                
    if (!server->show_stats) {
        loggerWebSendAll(client_fd, "null");
        return;
    }

    //Get the current time and calculate the start and end of the statistics window
    time_t window_start = 0;
    time_t window_end = 0;
    if (!loggerWebGraphStatsWindow(time(NULL), &window_start, &window_end)) {
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

    //Read the log file and calculate the minimum and maximum values 
    //for each series within the statistics window
    size_t column_count = loggerWebTotalColumnCount(server);
    FILE* file = fopen(server->log_path, "r");

    //See if the file was opened successfully
    //Then use the contents
    if (file) {
        char** fields = calloc(column_count, sizeof(*fields));
        char line[LOGGER_WEB_MAX_LINE];
        
        //Read each line of the log file, split it into fields
        //Check if it's within the statistics window
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

            //For each series, parse the value
            for (size_t i = 0; i < graph->series_count; i++) {
                double value = 0.0;
                if (!loggerWebParseDouble(
                        loggerWebFieldForColumn(fields, graph->series[i].index),
                        &value)) {
                    continue;
                }

                //Update the minimum and maximum values for the series
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

    //Format the statistics window into human-readable labels
    char start_label[32];
    char end_label[32];
    loggerWebFormatUnixLabel(window_start, start_label, sizeof(start_label));
    loggerWebFormatUnixLabel(window_end, end_label, sizeof(end_label));

    //Send the statistics data in JSON format to the client
    loggerWebSendAll(client_fd, "{\"windowStart\":\"");
    loggerWebSendJsonEscaped(client_fd, start_label);
    loggerWebSendAll(client_fd, "\",\"windowEnd\":\"");
    loggerWebSendJsonEscaped(client_fd, end_label);
    loggerWebSendAll(client_fd, "\",\"series\":[");

    //For each series, send the name, minimum, and maximum values in JSON format
    for (size_t i = 0; i < graph->series_count; i++) {
        if (i > 0) {
            loggerWebSendAll(client_fd, ",");
        }

        //Send the series name, minimum value, and maximum value in JSON format
        loggerWebSendAll(client_fd, "{\"name\":\"");
        loggerWebSendJsonEscaped(client_fd, graph->series[i].name);

        //Send the minimum value for the series, using "null" otherwise
        loggerWebSendAll(client_fd, "\",\"min\":");
        if (has_value[i]) {
            char number[64];
            snprintf(number, sizeof(number), "%.17g", mins[i]);
            loggerWebSendAll(client_fd, number);
        } else {
            loggerWebSendAll(client_fd, "null");
        }

        //Send the maximum value for the series, using "null" otherwise
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

    //Conclude the JSON object for the statistics data and free memory
    loggerWebSendAll(client_fd, "]}");
    free(mins);
    free(maxes);
    free(has_value);
}

//Write the graph events in JSON format for the specified graph and range
static void writeGraphEventsJson(int client_fd,
                                 const LoggerWebServer* server,
                                 const LoggerWebGraph* graph,
                                 time_t range_start,
                                 time_t range_end) {
    //If there are no events to write, return early
    if (graph->vert_count == 0) {
        return;
    }

    //Check the file and column count
    size_t column_count = loggerWebTotalColumnCount(server);
    FILE* file = fopen(server->log_path, "r");
    if (!file) {
        return;
    }

    //Allocate memory for the fields array to hold the split fields
    char** fields = calloc(column_count, sizeof(*fields));
    if (!fields) {
        fclose(file);
        return;
    }

    //Read the log file, extract the relevant events, and format them as JSON
    int wrote_event = 0;
    char line[LOGGER_WEB_MAX_LINE];
    while (fgets(line, sizeof(line), file)) {
        char* newline = strpbrk(line, "\r\n");
        if (newline) {
            *newline = '\0';
        }

        //Split the line into fields and check if it's within the range
        loggerWebSplitFields(line, fields, column_count);
        time_t logged_at = 0;
        if (!loggerWebParseUnixTime(fields[0], &logged_at) ||
            logged_at < range_start || logged_at > range_end) {
            continue;
        }

        //Format the x-axis value based on the logged time
        char x_text[64];
        loggerWebFormatUnixLabel(logged_at, x_text, sizeof(x_text));

        //For each event, check if the field matches and send it in JSON format
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

//Write the graph spans in JSON format for the specified graph and range
static void writeGraphSpansJson(int client_fd,
                                const LoggerWebServer* server,
                                const LoggerWebGraph* graph,
                                time_t range_start,
                                time_t range_end) {
    //If there are no spans to write, return early
    if (graph->span_count == 0) {
        return;
    }

    //Check the file and column count
    size_t column_count = loggerWebTotalColumnCount(server);
    int wrote_span = 0;

    //Iterate through each span and extract the data points from the log file
    for (size_t span_index = 0; span_index < graph->span_count; span_index++) {
        //Get the current span and open the log file for reading
        const LoggerWebSpan* span = &graph->spans[span_index];
        FILE* file = fopen(server->log_path, "r");
        if (!file) {
            return;
        }

        //Allocate memory for the fields array to hold the split fields
        char** fields = calloc(column_count, sizeof(*fields));
        if (!fields) {
            fclose(file);
            return;
        }

        //Initialize variables to track the start of the span and its duration
        int has_start = 0;
        time_t start_time = 0;
        char start_x[256] = "";
        char line[LOGGER_WEB_MAX_LINE];

        //Read the log file, extract relevant spans, and format them as JSON
        while (fgets(line, sizeof(line), file)) {
            char* newline = strpbrk(line, "\r\n");
            if (newline) {
                *newline = '\0';
            }

            //Split the line into fields and check if it's within the range
            loggerWebSplitFields(line, fields, column_count);
            time_t logged_at = 0;
            if (!loggerWebParseUnixTime(fields[0], &logged_at) ||
                logged_at < range_start || logged_at > range_end) {
                continue;
            }

            //Get the field value for the current span's column index
            const char* field = loggerWebFieldForColumn(fields, span->column_index);
            if (!field) {
                continue;
            }

            //Check if the field matches the start value of the span
            if (strcmp(field, span->start_value) == 0) {
                if (!has_start) {
                    has_start = 1;
                    start_time = logged_at;
                    loggerWebFormatUnixLabel(logged_at, start_x, sizeof(start_x));
                }
                continue;
            }

            //Check if the field matches the end value of the span 
            //and if a start has been logged
            if (strcmp(field, span->end_value) == 0 && has_start && logged_at >= start_time) {
                //Calculate the duration of the span in seconds and format it
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


#endif
