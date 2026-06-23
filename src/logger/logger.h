#ifndef LOGGER_H
#define LOGGER_H

#include <stdarg.h>

int lprint(const char* log_path, const char* message);
int lprintf(const char* log_path, const char* format, ...);
void logTrim(const char* log_path);

#endif
