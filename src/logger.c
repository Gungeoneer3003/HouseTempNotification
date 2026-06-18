#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "portable.h"
#include "settings.h"

static void write_log_field(FILE* file, const char* value);
static void replace_file(const char* temp_path, const char* target_path);

int log_append(const char* log_path,
               const char* event,
               int house,
               int outside_air,
               int power,
               Recommendation rec,
               const char* detail) {
    FILE* file = fopen(log_path, "a");
    if (!file) {
        fprintf(stderr, "Failed to open log file %s\n", log_path);
        return 0;
    }

    time_t now = time(NULL);
    struct tm local;
    char timestamp[32];

    if (portable_localtime(&now, &local)) {
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local);
    } else {
        snprintf(timestamp, sizeof(timestamp), "unknown");
    }

    fprintf(file, "%lld|%s|%d|%d|%d|%s|",
            (long long)now, timestamp, house, outside_air, power, recommendation_name(rec));
    write_log_field(file, event ? event : "");
    fputc('|', file);
    write_log_field(file, detail ? detail : "");
    fputc('\n', file);

    fclose(file);
    return 1;
}

void log_trim(const char* log_path) {
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
    replace_file(temp_path, log_path);
}

static void write_log_field(FILE* file, const char* value) {
    for (const char* p = value; *p; p++) {
        if (*p == '\n' || *p == '\r' || *p == '|') {
            fputc(' ', file);
        } else {
            fputc(*p, file);
        }
    }
}

static void replace_file(const char* temp_path, const char* target_path) {
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
