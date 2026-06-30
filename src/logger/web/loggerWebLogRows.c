//Statement of Purpose:
/*
The purpose of this file is to provide the implementation for 
sending log rows over the web interface. It includes functions to read the 
log file, split log entries into fields, and send the log rows to the client 
in an HTML table format.
*/

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L

#include "loggerWeb.h"
#include "loggerWebInternal.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void sendColspanMessage(int client_fd, size_t column_count, const char* message);
static size_t displayedColumnCount(const LoggerWebServer* server);
static int looksLikeDateField(const char* value);
static int looksLikeTimeField(const char* value);
static void writeLogRow(int client_fd, char* line, const LoggerWebServer* server);

//Send the contents of the log file to the client, escaping HTML special characters
void loggerWebSendRawLogContent(int client_fd, const LoggerWebServer* server) {
    //Open the log file and send its contents to the client
    FILE* file = fopen(server->log_path, "r");
    if (!file) {
        return;
    }

    //Read the log file line by line and send it to the client
    char buffer[2048];
    while (fgets(buffer, sizeof(buffer), file)) {
        loggerWebSendEscaped(client_fd, buffer);
    }

    //Clean up
    fclose(file);
}

//Send the log rows to the client in an HTML table format
//Colspan means that the message will span across all columns in the table
static void sendColspanMessage(int client_fd, size_t column_count, const char* message) {
    char colspan[32];
    snprintf(colspan, sizeof(colspan), "%zu", column_count);

    loggerWebSendAll(client_fd, "<tr><td colspan=\"");
    loggerWebSendAll(client_fd, colspan);
    loggerWebSendAll(client_fd, "\">");
    loggerWebSendEscaped(client_fd, message);
    loggerWebSendAll(client_fd, "</td></tr>");
}

//Check if the given string looks like a date field in the format YYYY-MM-DD
size_t loggerWebTotalColumnCount(const LoggerWebServer* server) {
    return server->column_header_count + LOGGER_WEB_DATA_FIELD;
}

//Get the number of displayed columns in the log table, excluding the hidden Unix timestamp column
static size_t displayedColumnCount(const LoggerWebServer* server) {
    return loggerWebTotalColumnCount(server) - 1;
}

//Split a log line into fields based on the '|' delimiter, up to the specified column count.
int loggerWebSplitFields(char* line, char** fields, size_t column_count) {
    if (!line || !fields || column_count == 0) {
        return 0;
    }

    memset(fields, 0, column_count * sizeof(*fields));

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

//Get the field value for a given column index
//Taking into account the special handling of the 
//Unix timestamp and split date/time fields
const char* loggerWebFieldForColumn(char** fields, size_t column_index) {
    if (!fields) {
        return NULL;
    }

    if (column_index < LOGGER_WEB_DATA_FIELD || loggerWebRowHasSplitDateTime(fields)) {
        return fields[column_index];
    }

    return fields[column_index - 1];
}

//Check if the log row has split date and time fields
int loggerWebRowHasSplitDateTime(char** fields) {
    return fields &&
           looksLikeDateField(fields[LOGGER_WEB_DATE_FIELD]) &&
           looksLikeTimeField(fields[LOGGER_WEB_TIME_FIELD]);
}

//Check whether a field is a split date in YYYY-MM-DD format.
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

//Check whether a field is a split local-time value.
static int looksLikeTimeField(const char* value) {
    if (!value || !strchr(value, ':')) {
        return 0;
    }

    return strstr(value, " AM") != NULL || strstr(value, " PM") != NULL;
}

//Send the log rows to the client in an HTML table format
void loggerWebSendLogRows(int client_fd, const LoggerWebServer* server) {
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

    //Show newest entries first.
    for (size_t i = row_count; i > 0; i--) {
        writeLogRow(client_fd, rows[i - 1], server);
        free(rows[i - 1]);
    }
    free(rows);
}

//Write a single log row to the client in an HTML table format, escaping special characters
static void writeLogRow(int client_fd, char* line, const LoggerWebServer* server) {
    size_t column_count = loggerWebTotalColumnCount(server);
    size_t display_column_count = displayedColumnCount(server);

    //Split the line into fields based on the '|' delimiter, up to the number of columns expected
    char** fields = calloc(column_count, sizeof(*fields));
    if (!fields) {
        sendColspanMessage(client_fd, display_column_count, "Unable to load log row.");
        return;
    }

    loggerWebSplitFields(line, fields, column_count);

    //Send the fields as a row in the log table, with HTML escaping for special characters
    time_t logged_at = 0;
    int has_logged_at = loggerWebParseUnixTime(fields[LOGGER_WEB_UNIX_FIELD], &logged_at);
    char date_text[32] = "";
    char time_text[32] = "";

    //Determine the date and time text to display based on the available fields
    if (has_logged_at) {
        loggerWebFormatUnixDate(logged_at, date_text, sizeof(date_text));
        loggerWebFormatUnixTime(logged_at, time_text, sizeof(time_text));
    } else if (loggerWebRowHasSplitDateTime(fields)) {
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

    //Send the log row as an HTML table row, with each field in its own cell
    loggerWebSendAll(client_fd, "<tr>");
    loggerWebSendAll(client_fd, "<td>");
    loggerWebSendEscaped(client_fd, date_text);
    loggerWebSendAll(client_fd, "</td><td>");
    loggerWebSendEscaped(client_fd, time_text);
    loggerWebSendAll(client_fd, "</td>");

    //Send the remaining fields as table cells, skipping the Unix timestamp field
    for (size_t i = 0; i < server->column_header_count; i++) {
        const char* field = loggerWebFieldForColumn(fields, i + LOGGER_WEB_DATA_FIELD);
        loggerWebSendAll(client_fd, "<td>");
        loggerWebSendEscaped(client_fd, field ? field : "");
        loggerWebSendAll(client_fd, "</td>");
    }
    loggerWebSendAll(client_fd, "</tr>");
    
    //Clean up
    free(fields);
}


#endif
