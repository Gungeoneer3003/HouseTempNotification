#ifndef LOGGER_WEB_H
#define LOGGER_WEB_H

#include <stddef.h>

int loggerWebStart(const char* log_path,
                   unsigned short port,
                   const char* title,
                   const char* const* column_headers,
                   size_t column_header_count);
int loggerWebInsertGraph(const char* title,
                         const char* x_column,
                         const char* y_column);

#endif
