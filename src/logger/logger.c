#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifndef LOG_RETENTION_DAYS
#define LOG_RETENTION_DAYS 30
#endif

static int logLocaltime(const time_t* value, struct tm* out);
static void replaceFile(const char* temp_path, const char* target_path);

static int logLocaltime(const time_t* value, struct tm* out) {
    if (!value || !out) {
        return 0;
    }

#ifdef _WIN32
    return localtime_s(out, value) == 0;
#else
    return localtime_r(value, out) != NULL;
#endif
}

int logWrite(const char* log_path, const char* message) {
    if (!log_path || !message) {
        return 0;
    }

    FILE* file = fopen(log_path, "a");
    if (!file) {
        fprintf(stderr, "Failed to open log file %s\n", log_path);
        return 0;
    }

    time_t now = time(NULL);
    struct tm local;
    char timestamp[32];

    if (logLocaltime(&now, &local)) {
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local);
    } else {
        snprintf(timestamp, sizeof(timestamp), "unknown");
    }

    fprintf(file, "%lld|%s|%s\n", (long long)now, timestamp, message);
    fclose(file);
    return 1;
}

int logFormat(const char* log_path, const char* format, ...) {
    if (!log_path || !format) {
        return 0;
    }

    char message[512];
    va_list args;
    va_start(args, format);
    int written = vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    if (written < 0 || (size_t)written >= sizeof(message)) {
        return 0;
    }

    return logWrite(log_path, message);
}

void logTrim(const char* log_path) {
    if (!log_path) {
        return;
    }

    FILE* input = fopen(log_path, "r");
    if (!input) {
        return;
    }

    char temp_path[512];
    int n = snprintf(temp_path, sizeof(temp_path), "%s.tmp", log_path);
    if (n < 0 || (size_t)n >= sizeof(temp_path)) {
        fclose(input);
        return;
    }

    FILE* output = fopen(temp_path, "w");
    if (!output) {
        fclose(input);
        return;
    }

    time_t cutoff = time(NULL) - (time_t)LOG_RETENTION_DAYS * 24 * 60 * 60;
    char line[1024];

    while (fgets(line, sizeof(line), input)) {
        char* end = NULL;
        long long logged_at = strtoll(line, &end, 10);

        if (end == line || logged_at >= (long long)cutoff) {
            fputs(line, output);
        }
    }

    fclose(input);
    fclose(output);
    replaceFile(temp_path, log_path);
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
