#ifndef LOGGER_H
#define LOGGER_H

#include <stdarg.h>

//Log a simple message with timestamp to the log file
int logWrite(const char* log_path, const char* message);

//Log a formatted message (like sprintf) with timestamp to the log file
int logFormat(const char* log_path, const char* format, ...);

//Remove log entries older than LOG_RETENTION_DAYS
void logTrim(const char* log_path);

#endif
