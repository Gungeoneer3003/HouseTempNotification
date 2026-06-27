#ifndef LOGGER_WEB_INTERNAL_H
#define LOGGER_WEB_INTERNAL_H

#ifndef _WIN32

#include <pthread.h>
#include <stddef.h>
#include <time.h>

#ifndef LOGGER_WEB_BACKLOG
#define LOGGER_WEB_BACKLOG 8
#endif

#ifndef LOGGER_WEB_MAX_LINE
#define LOGGER_WEB_MAX_LINE 2048
#endif

#define LOGGER_WEB_ROOT_LOG "log"
#define LOGGER_WEB_ROOT_GRAPHS "graphs"
#define LOGGER_WEB_ROOT_DIRECTORY_SIZE 32
#define LOGGER_WEB_MAX_PATH 256
#define LOGGER_WEB_UNIX_FIELD 0
#define LOGGER_WEB_DATE_FIELD 1
#define LOGGER_WEB_TIME_FIELD 2
#define LOGGER_WEB_DATA_FIELD 3

typedef struct {
    char* name;
    size_t index;
} LoggerWebGraphSeries;

typedef struct {
    char* column;
    char* value;
    char* color;
    size_t column_index;
} LoggerWebVert;

typedef struct {
    char* column;
    char* start_value;
    char* end_value;
    char* color;
    size_t column_index;
} LoggerWebSpan;

typedef struct {
    char* name;
    size_t index;
} LoggerWebTodayColumn;

typedef struct {
    char* title;
    char* x_column;
    size_t x_index;
    LoggerWebGraphSeries* series;
    size_t series_count;
    LoggerWebVert* verts;
    size_t vert_count;
    size_t vert_capacity;
    LoggerWebSpan* spans;
    size_t span_count;
    size_t span_capacity;
} LoggerWebGraph;

typedef struct {
    char log_path[512];
    char root_directory[LOGGER_WEB_ROOT_DIRECTORY_SIZE];
    unsigned short port;
    int server_fd;
    char* title;
    char** column_headers;
    size_t column_header_count;
    LoggerWebGraph* graphs;
    size_t graph_count;
    size_t graph_capacity;
    int show_stats;
    int show_refresh_button;
    LoggerWebTodayColumn* today_columns;
    size_t today_column_count;
} LoggerWebServer;

typedef enum {
    LOGGER_WEB_GRAPH_RANGE_DAY,
    LOGGER_WEB_GRAPH_RANGE_THREE_DAYS,
    LOGGER_WEB_GRAPH_RANGE_WEEK
} LoggerWebGraphRange;

extern LoggerWebServer* active_server;
extern pthread_mutex_t active_server_mutex;

char* loggerWebCopyString(const char* value);
void loggerWebSendAll(int fd, const char* data);
void loggerWebSendJsonEscaped(int fd, const char* value);
size_t loggerWebTotalColumnCount(const LoggerWebServer* server);
int loggerWebSplitFields(char* line, char** fields, size_t column_count);
const char* loggerWebFieldForColumn(char** fields, size_t column_index);
int loggerWebRowHasSplitDateTime(char** fields);
int loggerWebParseDouble(const char* value, double* out);
int loggerWebParseUnixTime(const char* value, time_t* out);
int loggerWebLogLocaltime(const time_t* value, struct tm* out);
void loggerWebFormatUnixLabel(time_t value, char* buffer, size_t buffer_size);
void loggerWebFormatUnixTime(time_t value, char* buffer, size_t buffer_size);
void loggerWebFormatDuration(time_t seconds, char* buffer, size_t buffer_size);
int loggerWebResolveColumnIndex(const LoggerWebServer* server,
                                const char* column,
                                size_t* index);
int loggerWebStringEqualsIgnoreCase(const char* left, const char* right);

void loggerWebFreeGraphs(LoggerWebServer* server);
void loggerWebFreeTodayColumns(LoggerWebServer* server);
int loggerWebHasGraphs(const LoggerWebServer* server);
LoggerWebGraphRange loggerWebParseGraphRange(const char* request);
void loggerWebSendGraphData(int client_fd,
                            const LoggerWebServer* server,
                            LoggerWebGraphRange range);

#endif

#endif
