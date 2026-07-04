#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "logger.h"
#include "loggerSettings.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

#ifndef LOGGER_LINE_SIZE
#define LOGGER_LINE_SIZE 4096
#endif

#ifdef _WIN32
static SRWLOCK logger_mutex = SRWLOCK_INIT;
static void loggerLock(void);
static void loggerUnlock(void);
#else
static pthread_mutex_t logger_mutex = PTHREAD_MUTEX_INITIALIZER;
static void loggerLock(void);
static void loggerUnlock(void);
#endif

static int writeRecordJson(FILE* file, time_t logged_at, const LogRecord* record);
static void writeJsonString(FILE* file, const char* value);
static void replaceFile(const char* temp_path, const char* target_path);
static int parseJsonRecord(char* line, LogRecord* record);
static int parseLegacyPipeRecord(char* line, LogRecord* record);
static char* skipWhitespace(char* cursor);
static int parseJsonString(char** cursor, char** value);
static int parseJsonFields(char** cursor, LogRecord* record);
static int parseJsonInteger(char** cursor, time_t* value);
static int parseLegacyUnixTime(const char* value, time_t* out);
static int looksLikeDateField(const char* value);
static int looksLikeTimeField(const char* value);
static int hexValue(char c);

int logger_init(Logger* logger, const LoggerConfig* config) {
    if (!logger || !config) {
        return 0;
    }

    memset(logger, 0, sizeof(*logger));
    logger->backend = config->backend;
    logger->retention_days = config->retention_days > 0
        ? config->retention_days
        : LOG_RETENTION_DAYS;

    if (logger->backend == LOGGER_BACKEND_FILE) {
        if (!config->path || !*config->path) {
            return 0;
        }

        int n = snprintf(logger->path, sizeof(logger->path), "%s", config->path);
        if (n < 0 || (size_t)n >= sizeof(logger->path)) {
            fprintf(stderr, "Log path is too long\n");
            memset(logger, 0, sizeof(*logger));
            return 0;
        }
    }

    return 1;
}

int logger_init_file(Logger* logger, const char* path) {
    LoggerConfig config;
    memset(&config, 0, sizeof(config));
    config.backend = LOGGER_BACKEND_FILE;
    config.path = path;
    config.retention_days = LOG_RETENTION_DAYS;
    return logger_init(logger, &config);
}

void logger_destroy(Logger* logger) {
    if (logger) {
        memset(logger, 0, sizeof(*logger));
    }
}

const char* logger_path(const Logger* logger) {
    if (!logger || logger->backend != LOGGER_BACKEND_FILE || !logger->path[0]) {
        return NULL;
    }

    return logger->path;
}

int logger_record_init(LogRecord* record,
                       const char* const* fields,
                       size_t field_count) {
    if (!record || (field_count > 0 && !fields) ||
        field_count > LOGGER_RECORD_MAX_FIELDS) {
        return 0;
    }

    memset(record, 0, sizeof(*record));
    record->field_count = field_count;

    for (size_t i = 0; i < field_count; i++) {
        record->fields[i] = fields[i] ? fields[i] : "";
    }

    return 1;
}

int logger_log_fields(Logger* logger,
                      const char* const* fields,
                      size_t field_count) {
    LogRecord record;
    if (!logger_record_init(&record, fields, field_count)) {
        return 0;
    }

    return logger_log(logger, &record);
}

int logger_log(Logger* logger, const LogRecord* record) {
    if (!logger || !record || record->field_count > LOGGER_RECORD_MAX_FIELDS) {
        return 0;
    }

    if (logger->backend == LOGGER_BACKEND_DISABLED) {
        return 1;
    }

    time_t logged_at = record->has_logged_at ? record->logged_at : time(NULL);

    loggerLock();

    FILE* file = NULL;
    int close_file = 0;
    if (logger->backend == LOGGER_BACKEND_STDOUT) {
        file = stdout;
    } else if (logger->backend == LOGGER_BACKEND_FILE) {
        file = fopen(logger->path, "a");
        close_file = 1;
    }

    if (!file) {
        if (logger->backend == LOGGER_BACKEND_FILE) {
            fprintf(stderr, "Failed to open log file %s\n", logger->path);
        }
        loggerUnlock();
        return 0;
    }

    int ok = writeRecordJson(file, logged_at, record);
    fflush(file);
    if (close_file) {
        fclose(file);
    }

    loggerUnlock();
    return ok;
}

void logger_trim(Logger* logger) {
    if (!logger || logger->backend != LOGGER_BACKEND_FILE || !logger->path[0]) {
        return;
    }

    loggerLock();

    FILE* input = fopen(logger->path, "r");
    if (!input) {
        loggerUnlock();
        return;
    }

    char temp_path[512];
    int n = snprintf(temp_path, sizeof(temp_path), "%s.tmp", logger->path);
    if (n < 0 || (size_t)n >= sizeof(temp_path)) {
        fclose(input);
        loggerUnlock();
        return;
    }

    FILE* output = fopen(temp_path, "w");
    if (!output) {
        fclose(input);
        loggerUnlock();
        return;
    }

    time_t cutoff = time(NULL) - (time_t)logger->retention_days * 24 * 60 * 60;
    char line[LOGGER_LINE_SIZE];
    char parse_line[LOGGER_LINE_SIZE];

    while (fgets(line, sizeof(line), input)) {
        snprintf(parse_line, sizeof(parse_line), "%s", line);

        LogRecord record;
        if (!logger_record_parse_line(parse_line, &record) ||
            !record.has_logged_at ||
            record.logged_at >= cutoff) {
            fputs(line, output);
        }
    }

    fclose(input);
    fclose(output);
    replaceFile(temp_path, logger->path);

    loggerUnlock();
}

int logger_record_parse_line(char* line, LogRecord* record) {
    if (!line || !record) {
        return 0;
    }

    char* newline = strpbrk(line, "\r\n");
    if (newline) {
        *newline = '\0';
    }

    char* cursor = skipWhitespace(line);
    if (*cursor == '{') {
        return parseJsonRecord(cursor, record);
    }

    return parseLegacyPipeRecord(cursor, record);
}

static int writeRecordJson(FILE* file, time_t logged_at, const LogRecord* record) {
    if (!file || !record) {
        return 0;
    }

    if (fprintf(file, "{\"ts\":%lld,\"fields\":[", (long long)logged_at) < 0) {
        return 0;
    }

    for (size_t i = 0; i < record->field_count; i++) {
        if (i > 0 && fputc(',', file) == EOF) {
            return 0;
        }
        writeJsonString(file, record->fields[i] ? record->fields[i] : "");
    }

    return fputs("]}\n", file) >= 0;
}

static void writeJsonString(FILE* file, const char* value) {
    fputc('"', file);

    for (const unsigned char* p = (const unsigned char*)value; p && *p; p++) {
        switch (*p) {
            case '"':
                fputs("\\\"", file);
                break;
            case '\\':
                fputs("\\\\", file);
                break;
            case '\b':
                fputs("\\b", file);
                break;
            case '\f':
                fputs("\\f", file);
                break;
            case '\n':
                fputs("\\n", file);
                break;
            case '\r':
                fputs("\\r", file);
                break;
            case '\t':
                fputs("\\t", file);
                break;
            default:
                if (*p < 0x20) {
                    fprintf(file, "\\u%04x", (unsigned)*p);
                } else {
                    fputc((int)*p, file);
                }
                break;
        }
    }

    fputc('"', file);
}

static int parseJsonRecord(char* line, LogRecord* record) {
    memset(record, 0, sizeof(*record));

    char* cursor = skipWhitespace(line);
    if (*cursor != '{') {
        return 0;
    }
    cursor++;

    int saw_fields = 0;
    for (;;) {
        cursor = skipWhitespace(cursor);
        if (*cursor == '}') {
            cursor++;
            break;
        }

        char* key = NULL;
        if (!parseJsonString(&cursor, &key)) {
            return 0;
        }

        cursor = skipWhitespace(cursor);
        if (*cursor != ':') {
            return 0;
        }
        cursor++;
        cursor = skipWhitespace(cursor);

        if (strcmp(key, "ts") == 0) {
            if (!parseJsonInteger(&cursor, &record->logged_at)) {
                return 0;
            }
            record->has_logged_at = 1;
        } else if (strcmp(key, "fields") == 0) {
            if (!parseJsonFields(&cursor, record)) {
                return 0;
            }
            saw_fields = 1;
        } else {
            return 0;
        }

        cursor = skipWhitespace(cursor);
        if (*cursor == ',') {
            cursor++;
            continue;
        }
        if (*cursor == '}') {
            cursor++;
            break;
        }

        return 0;
    }

    cursor = skipWhitespace(cursor);
    return *cursor == '\0' && saw_fields;
}

static int parseJsonFields(char** cursor, LogRecord* record) {
    if (!cursor || !record || **cursor != '[') {
        return 0;
    }

    (*cursor)++;
    record->field_count = 0;

    for (;;) {
        *cursor = skipWhitespace(*cursor);
        if (**cursor == ']') {
            (*cursor)++;
            return 1;
        }

        if (record->field_count == LOGGER_RECORD_MAX_FIELDS) {
            return 0;
        }

        char* value = NULL;
        if (!parseJsonString(cursor, &value)) {
            return 0;
        }
        record->fields[record->field_count++] = value;

        *cursor = skipWhitespace(*cursor);
        if (**cursor == ',') {
            (*cursor)++;
            continue;
        }
        if (**cursor == ']') {
            (*cursor)++;
            return 1;
        }

        return 0;
    }
}

static int parseJsonString(char** cursor, char** value) {
    if (!cursor || !*cursor || !value || **cursor != '"') {
        return 0;
    }

    char* src = *cursor + 1;
    char* dst = src;
    char* start = dst;

    while (*src) {
        if (*src == '"') {
            *dst = '\0';
            *cursor = src + 1;
            *value = start;
            return 1;
        }

        if (*src != '\\') {
            *dst++ = *src++;
            continue;
        }

        src++;
        switch (*src) {
            case '"':
            case '\\':
            case '/':
                *dst++ = *src++;
                break;
            case 'b':
                *dst++ = '\b';
                src++;
                break;
            case 'f':
                *dst++ = '\f';
                src++;
                break;
            case 'n':
                *dst++ = '\n';
                src++;
                break;
            case 'r':
                *dst++ = '\r';
                src++;
                break;
            case 't':
                *dst++ = '\t';
                src++;
                break;
            case 'u': {
                int value_code = 0;
                for (int i = 1; i <= 4; i++) {
                    int digit = hexValue(src[i]);
                    if (digit < 0) {
                        return 0;
                    }
                    value_code = (value_code << 4) | digit;
                }
                if (value_code > 0x7f) {
                    return 0;
                }
                *dst++ = (char)value_code;
                src += 5;
                break;
            }
            default:
                return 0;
        }
    }

    return 0;
}

static int parseJsonInteger(char** cursor, time_t* value) {
    if (!cursor || !*cursor || !value) {
        return 0;
    }

    errno = 0;
    char* end = NULL;
    long long parsed = strtoll(*cursor, &end, 10);
    if (end == *cursor || errno == ERANGE) {
        return 0;
    }

    *value = (time_t)parsed;
    *cursor = end;
    return 1;
}

static int parseLegacyPipeRecord(char* line, LogRecord* record) {
    memset(record, 0, sizeof(*record));

    if (!line || !*line) {
        return 0;
    }

    char* tokens[LOGGER_RECORD_MAX_FIELDS + 3];
    size_t token_count = 0;
    char* cursor = line;

    while (token_count < sizeof(tokens) / sizeof(tokens[0])) {
        tokens[token_count++] = cursor;
        char* next = strchr(cursor, '|');
        if (!next) {
            break;
        }
        *next = '\0';
        cursor = next + 1;
    }

    if (token_count == 0) {
        return 0;
    }

    size_t data_start = 0;
    if (parseLegacyUnixTime(tokens[0], &record->logged_at)) {
        record->has_logged_at = 1;
        data_start = 1;
        if (token_count >= 3 &&
            looksLikeDateField(tokens[1]) &&
            looksLikeTimeField(tokens[2])) {
            data_start = 3;
        }
    }

    for (size_t i = data_start; i < token_count; i++) {
        if (record->field_count == LOGGER_RECORD_MAX_FIELDS) {
            return 0;
        }
        record->fields[record->field_count++] = tokens[i];
    }

    return record->field_count > 0 || record->has_logged_at;
}

static char* skipWhitespace(char* cursor) {
    while (cursor && isspace((unsigned char)*cursor)) {
        cursor++;
    }

    return cursor;
}

static int parseLegacyUnixTime(const char* value, time_t* out) {
    if (!value || !*value || !out) {
        return 0;
    }

    errno = 0;
    char* end = NULL;
    long long parsed = strtoll(value, &end, 10);
    if (end == value || errno == ERANGE || (end && *end)) {
        return 0;
    }

    *out = (time_t)parsed;
    return 1;
}

static int looksLikeDateField(const char* value) {
    if (!value || strlen(value) != 10) {
        return 0;
    }

    return isdigit((unsigned char)value[0]) &&
           isdigit((unsigned char)value[1]) &&
           isdigit((unsigned char)value[2]) &&
           isdigit((unsigned char)value[3]) &&
           value[4] == '-' &&
           isdigit((unsigned char)value[5]) &&
           isdigit((unsigned char)value[6]) &&
           value[7] == '-' &&
           isdigit((unsigned char)value[8]) &&
           isdigit((unsigned char)value[9]);
}

static int looksLikeTimeField(const char* value) {
    if (!value || !strchr(value, ':')) {
        return 0;
    }

    return strstr(value, " AM") != NULL || strstr(value, " PM") != NULL;
}

static int hexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }

    return -1;
}

static void replaceFile(const char* temp_path, const char* target_path) {
#ifdef _WIN32
    if (!MoveFileExA(temp_path, target_path, MOVEFILE_REPLACE_EXISTING)) {
        remove(temp_path);
    }
#else
    if (rename(temp_path, target_path) != 0) {
        remove(temp_path);
    }
#endif
}

static void loggerLock(void) {
#ifdef _WIN32
    AcquireSRWLockExclusive(&logger_mutex);
#else
    pthread_mutex_lock(&logger_mutex);
#endif
}

static void loggerUnlock(void) {
#ifdef _WIN32
    ReleaseSRWLockExclusive(&logger_mutex);
#else
    pthread_mutex_unlock(&logger_mutex);
#endif
}
