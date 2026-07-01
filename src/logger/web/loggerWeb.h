#ifndef LOGGER_WEB_H
#define LOGGER_WEB_H

#include <stddef.h>

// Optional callbacks used by the custom Today panel fan buttons.
typedef struct {
    int (*speed_up)(void* user);
    int (*speed_down)(void* user);
    int (*power_toggle)(void* user);
    void* user;
} LoggerWebTodayControls;

typedef int (*LoggerWebAccessPoller)(void* user);

int loggerWebStart(const char* log_path,
                   unsigned short port,
                   const char* title,
                   const char* const* column_headers,
                   size_t column_header_count);
int loggerWebSetRootDirectory(const char* subdirectory);
int loggerWebAddNavLink(const char* label, const char* href, int root_relative);
int loggerWebSetAccessPoller(int mode, LoggerWebAccessPoller poller, void* user);
int loggerWebInsertGraph(const char* title,
                         const char* x_column,
                         const char* y_column);
int loggerWebInsertGraphSeries(const char* title,
                               const char* x_column,
                               const char* const* y_columns,
                               size_t y_column_count);
int loggerWebShowStats(int enabled);
int loggerWebShowRefreshButton(int enabled);
int loggerWebShowVerts(const char* graph_title,
                       const char* column,
                       const char* value,
                       const char* color);
int loggerWebShowSpan(const char* graph_title,
                      const char* column,
                      const char* start_value,
                      const char* end_value,
                      const char* color);
int loggerWebShowToday(const char* const* columns,
                       size_t column_count,
                       int show_on_other_pages,
                       int show_controls);
// Pass NULL to clear any Today panel control callbacks.
int loggerWebSetTodayControls(const LoggerWebTodayControls* controls);

#endif
