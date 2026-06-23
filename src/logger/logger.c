#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "logger.h"
#include <pthread.h>
#include <stdarg.h>
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

#ifndef MESSAGE_SIZE
#define MESSAGE_SIZE 512
#endif

//Static function prototypes
static int lprintUnlocked(const char* log_path, const char* message);
static int logLocaltime(const time_t* value, struct tm* out);
static void replaceFile(const char* temp_path, const char* target_path);

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

//Write a simple message with timestamp to the log file
int lprint(const char* log_path, const char* message) {
    int result;

    pthread_mutex_lock(&log_mutex);
    result = lprintUnlocked(log_path, message);
    pthread_mutex_unlock(&log_mutex);

    return result;
}

//Log a formatted message (like sprintf) with timestamp to the log file
int lprintf(const char* log_path, const char* format, ...) {
    if (!log_path || !format) {
        return 0;
    }

    //Format the message using a fixed-size buffer
    char message[MESSAGE_SIZE];
    va_list args;
    va_start(args, format);
    
    int written = vsnprintf(message, sizeof(message), format, args);
    
    va_end(args);

    if (written < 0 || (size_t)written >= sizeof(message)) {
        return 0;
    }

    pthread_mutex_lock(&log_mutex);
    int result = lprintUnlocked(log_path, message);
    pthread_mutex_unlock(&log_mutex);

    return result;
}

//Remove log entries older than LOG_RETENTION_DAYS
void logTrim(const char* log_path) {
    pthread_mutex_lock(&log_mutex);

    if (!log_path) {
        pthread_mutex_unlock(&log_mutex);
        return;
    }

    //Open the existing log file for reading
    FILE* input = fopen(log_path, "r");
    if (!input) {
        pthread_mutex_unlock(&log_mutex);
        return;
    }

    //Create a temporary file for writing the trimmed logs
    char temp_path[512]; //512 is an arbitrary size for the temp file path
    int n = snprintf(temp_path, sizeof(temp_path), "%s.tmp", log_path);
    if (n < 0 || (size_t)n >= sizeof(temp_path)) {
        fclose(input);
        pthread_mutex_unlock(&log_mutex);
        return;
    }

    //Write entries newer than the cutoff time to the temp file
    FILE* output = fopen(temp_path, "w");
    if (!output) {
        fclose(input);
        pthread_mutex_unlock(&log_mutex);
        return;
    }

    //Calculate the cutoff time for log retention
    time_t cutoff = time(NULL) - (time_t)LOG_RETENTION_DAYS * 24 * 60 * 60;
    char line[MESSAGE_SIZE + 64]; //64 bytes for timestamp and separators

    //Read each line from the input log file
    while (fgets(line, sizeof(line), input)) {
        char* end = NULL;
        long long logged_at = strtoll(line, &end, 10);

        if (end == line || logged_at >= (long long)cutoff) {
            fputs(line, output);
        }
    }

    //Clean up
    fclose(input);
    fclose(output);
    replaceFile(temp_path, log_path);

    pthread_mutex_unlock(&log_mutex);
}

static int lprintUnlocked(const char* log_path, const char* message) {
    if (!log_path || !message) {
        return 0;
    }

    //Ensure the log directory exists
    FILE* file = fopen(log_path, "a");
    if (!file) {
        fprintf(stderr, "Failed to open log file %s\n", log_path);
        return 0;
    }

    //Write log entry with timestamp
    time_t now = time(NULL);
    struct tm local;
    char timestamp[32];

    //Use localtime_r for thread safety
    if (logLocaltime(&now, &local)) {
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local);
    } else {
        snprintf(timestamp, sizeof(timestamp), "unknown");
    }

    //Log format: timestamp|message
    fprintf(file, "%lld|%s|%s\n", (long long)now, timestamp, message);
    fclose(file);
    return 1;
}

//Replace the target file with the temp file
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

//Get local time in a thread-safe way
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
