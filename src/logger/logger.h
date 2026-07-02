#ifndef LOGGER_H
#define LOGGER_H

#include <stddef.h>
#include <time.h>

#ifndef LOGGER_RECORD_MAX_FIELDS
#define LOGGER_RECORD_MAX_FIELDS 32
#endif

typedef enum {
    LOGGER_BACKEND_FILE,
    LOGGER_BACKEND_STDOUT,
    LOGGER_BACKEND_DISABLED
} LoggerBackend;

typedef struct {
    LoggerBackend backend;
    const char* path;
    int retention_days;
} LoggerConfig;

typedef struct Logger {
    LoggerBackend backend;
    char path[512];
    int retention_days;
} Logger;

typedef struct {
    time_t logged_at;
    int has_logged_at;
    const char* fields[LOGGER_RECORD_MAX_FIELDS];
    size_t field_count;
} LogRecord;

int logger_init(Logger* logger, const LoggerConfig* config);
int logger_init_file(Logger* logger, const char* path);
void logger_destroy(Logger* logger);
const char* logger_path(const Logger* logger);

int logger_record_init(LogRecord* record,
                       const char* const* fields,
                       size_t field_count);
int logger_log(Logger* logger, const LogRecord* record);
int logger_log_fields(Logger* logger,
                      const char* const* fields,
                      size_t field_count);
void logger_trim(Logger* logger);

int logger_record_parse_line(char* line, LogRecord* record);

#endif
