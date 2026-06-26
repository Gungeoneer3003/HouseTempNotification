#ifndef LOGGER_WEB_H
#define LOGGER_WEB_H

#include <stddef.h>

int loggerWebStart(const char* log_path,
                   unsigned short port,
                   const char* title,
                   const char* const* column_headers,
                   size_t column_header_count);
int loggerWebSetRootDirectory(const char* subdirectory);
int loggerWebInsertGraph(const char* title,
                         const char* x_column,
                         const char* y_column);
int loggerWebInsertGraphSeries(const char* title,
                               const char* x_column,
                               const char* const* y_columns,
                               size_t y_column_count);
int loggerWebShowStats(int enabled);
int loggerWebShowVerts(const char* graph_title,
                       const char* column,
                       const char* value,
                       const char* color);
int loggerWebShowToday(const char* const* columns,
                       size_t column_count);

#endif
