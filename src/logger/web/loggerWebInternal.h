//Statement of purpose:
 /*
  * The purpose of this file is to provide internal declarations 
  * for the logger web server.
  */
#ifndef LOGGER_WEB_INTERNAL_H
#define LOGGER_WEB_INTERNAL_H

#include <stddef.h>
#include <time.h>

#include "logger.h"
#include "loggerSettings.h"
#include "portable_socket.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef SRWLOCK LoggerWebMutex;
typedef CONDITION_VARIABLE LoggerWebCondition;
typedef HANDLE LoggerWebThread;
#define LOGGER_WEB_MUTEX_INITIALIZER SRWLOCK_INIT
static inline int loggerWebMutexInit(LoggerWebMutex* mutex) {
    InitializeSRWLock(mutex);
    return 1;
}
static inline void loggerWebMutexDestroy(LoggerWebMutex* mutex) {
    (void)mutex;
}
static inline void loggerWebMutexLock(LoggerWebMutex* mutex) {
    AcquireSRWLockExclusive(mutex);
}
static inline void loggerWebMutexUnlock(LoggerWebMutex* mutex) {
    ReleaseSRWLockExclusive(mutex);
}
static inline int loggerWebConditionInit(LoggerWebCondition* condition) {
    InitializeConditionVariable(condition);
    return 1;
}
static inline void loggerWebConditionDestroy(LoggerWebCondition* condition) {
    (void)condition;
}
static inline void loggerWebConditionWait(LoggerWebCondition* condition,
                                          LoggerWebMutex* mutex) {
    SleepConditionVariableSRW(condition, mutex, INFINITE, 0);
}
static inline void loggerWebConditionWakeAll(LoggerWebCondition* condition) {
    WakeAllConditionVariable(condition);
}
#else
#include <pthread.h>
typedef pthread_mutex_t LoggerWebMutex;
typedef pthread_cond_t LoggerWebCondition;
typedef pthread_t LoggerWebThread;
#define LOGGER_WEB_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
static inline int loggerWebMutexInit(LoggerWebMutex* mutex) {
    return pthread_mutex_init(mutex, NULL) == 0;
}
static inline void loggerWebMutexDestroy(LoggerWebMutex* mutex) {
    pthread_mutex_destroy(mutex);
}
static inline void loggerWebMutexLock(LoggerWebMutex* mutex) {
    pthread_mutex_lock(mutex);
}
static inline void loggerWebMutexUnlock(LoggerWebMutex* mutex) {
    pthread_mutex_unlock(mutex);
}
static inline int loggerWebConditionInit(LoggerWebCondition* condition) {
    return pthread_cond_init(condition, NULL) == 0;
}
static inline void loggerWebConditionDestroy(LoggerWebCondition* condition) {
    pthread_cond_destroy(condition);
}
static inline void loggerWebConditionWait(LoggerWebCondition* condition,
                                          LoggerWebMutex* mutex) {
    pthread_cond_wait(condition, mutex);
}
static inline void loggerWebConditionWakeAll(LoggerWebCondition* condition) {
    pthread_cond_broadcast(condition);
}
#endif

#ifndef LOGGER_WEB_BACKLOG
#define LOGGER_WEB_BACKLOG 8
#endif

#ifndef LOGGER_WEB_MAX_LINE
#define LOGGER_WEB_MAX_LINE 2048
#endif

#ifndef LOGGER_WEB_MAX_AUTH_TOKEN
#define LOGGER_WEB_MAX_AUTH_TOKEN 128
#endif

#ifndef LOGGER_WEB_DEFAULT_LOG_LIMIT
#define LOGGER_WEB_DEFAULT_LOG_LIMIT 500
#endif

#ifndef LOGGER_WEB_MAX_LOG_LIMIT
#define LOGGER_WEB_MAX_LOG_LIMIT 5000
#endif

//Forward declarations for the logger web server structures

#define LOGGER_WEB_ROOT_LOG "log"
#define LOGGER_WEB_ROOT_GRAPHS "graphs"
#define LOGGER_WEB_ROOT_DIRECTORY_SIZE 32
#define LOGGER_WEB_MAX_PATH 256
#define LOGGER_WEB_UNIX_FIELD 0
#define LOGGER_WEB_DATE_FIELD 1
#define LOGGER_WEB_TIME_FIELD 2
#define LOGGER_WEB_DATA_FIELD 3

#define LOGGER_WEB_LOG_TEMPLATE "src/logger/web/html/log.html"
#define LOGGER_WEB_GRAPHS_TEMPLATE "src/logger/web/html/graphs.html"
#define LOGGER_WEB_RAW_TEMPLATE "src/logger/web/html/raw.html"
#define LOGGER_WEB_CSS_FILE "src/logger/web/css/loggerWeb.css"
#define LOGGER_WEB_GRAPH_SCRIPT_FILE "src/logger/web/js/loggerWebGraph.js"
#define LOGGER_WEB_TODAY_SCRIPT_FILE "src/logger/web/js/loggerWebToday.js"
#define LOGGER_WEB_FAVICON_FILE "src/logger/web/assets/AirscapeFavicon.png"

typedef struct {
    char* name;
    char* color;
    size_t index;
} LoggerWebGraphSeries;

typedef struct {
    char* column;
    char* value;
    char* label;
    char* color;
    char* marker_shape;
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
    char* label;
    char* href;
    int root_relative;
} LoggerWebNavLink;

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
    char bind_address[64];
    char auth_token[LOGGER_WEB_MAX_AUTH_TOKEN];
    char root_directory[LOGGER_WEB_ROOT_DIRECTORY_SIZE];
    unsigned short port;
    PortableSocket server_socket;
    LoggerWebThread thread;
    int thread_started;
    volatile int stop_requested;
    LoggerWebMutex client_mutex;
    LoggerWebCondition clients_done;
    LoggerWebMutex today_control_mutex;
    size_t active_client_count;
    int request_sync_initialized;
    size_t log_row_limit;
    char* title;
    char** column_headers;
    size_t column_header_count;
    LoggerWebGraph* graphs;
    size_t graph_count;
    size_t graph_capacity;
    int show_stats;
    int show_refresh_button;
    int show_today_on_other_pages;
    int show_today_controls;
    size_t today_status_inside_index;
    size_t today_status_outside_index;
    LoggerWebTodayStatusProvider today_status_provider;
    void* today_status_user;
    LoggerWebTodayControls today_controls;
    LoggerWebTodayColumn* today_columns;
    size_t today_column_count;
    LoggerWebNavLink* nav_links;
    size_t nav_link_count;
    size_t nav_link_capacity;
    int access_poll_mode;
    LoggerWebAccessPoller access_poller;
    void* access_poller_user;
} LoggerWebServer;

typedef enum {
    LOGGER_WEB_GRAPH_RANGE_DAY,
    LOGGER_WEB_GRAPH_RANGE_THREE_DAYS,
    LOGGER_WEB_GRAPH_RANGE_WEEK
} LoggerWebGraphRange;

typedef enum {
    LOGGER_WEB_PAGE_LOG,
    LOGGER_WEB_PAGE_GRAPHS,
    LOGGER_WEB_PAGE_RAW
} LoggerWebPage;

//Shared server state.
extern LoggerWebServer* active_server;
extern LoggerWebMutex active_server_mutex;

// Server lifecycle and routing.
int loggerWebStartServer(LoggerWebServer* server,
                         const char* bind_address,
                         unsigned short port);
void loggerWebStopServer(LoggerWebServer* server);
void loggerWebHandleClient(PortableSocket client_fd, LoggerWebServer* server);
int loggerWebRootDirectoryEquals(const LoggerWebServer* server, const char* subdirectory);

// Response and escaping helpers.
char* loggerWebCopyString(const char* value);
void loggerWebSendHtmlHeader(PortableSocket client_fd);
void loggerWebSendNotFound(PortableSocket client_fd);
void loggerWebSendUnauthorized(PortableSocket client_fd);
void loggerWebSendNoContent(PortableSocket client_fd);
void loggerWebSendPlainStatus(PortableSocket client_fd,
                              int status_code,
                              const char* reason,
                              const char* body);
void loggerWebSendBytes(PortableSocket fd, const char* data, size_t length);
void loggerWebSendAll(PortableSocket fd, const char* data);
void loggerWebSendEscaped(PortableSocket fd, const char* value);
void loggerWebSendJsonEscaped(PortableSocket fd, const char* value);
void loggerWebSendStaticFile(PortableSocket client_fd,
                             const char* content_type,
                             const char* path);
void loggerWebSendCss(PortableSocket client_fd);
void loggerWebSendGraphScript(PortableSocket client_fd);
void loggerWebSendTodayScript(PortableSocket client_fd);
void loggerWebSendFavicon(PortableSocket client_fd);

// HTML pages and templates.
void loggerWebSendIndex(PortableSocket client_fd,
                        const LoggerWebServer* server,
                        int is_root,
                        size_t log_limit);
void loggerWebSendGraphs(PortableSocket client_fd, const LoggerWebServer* server, int is_root);
void loggerWebSendRawLog(PortableSocket client_fd, const LoggerWebServer* server, int is_root);
void loggerWebSendTemplate(PortableSocket client_fd,
                           const char* path,
                           const LoggerWebServer* server,
                           int show_today_panel,
                           LoggerWebPage current_page,
                           size_t log_limit);
void loggerWebSendNav(PortableSocket client_fd,
                      const LoggerWebServer* server,
                      LoggerWebPage current_page);
void loggerWebSendTableHeaders(PortableSocket client_fd, const LoggerWebServer* server);

// Log rows and parsing helpers.
void loggerWebSendLogRows(PortableSocket client_fd,
                          const LoggerWebServer* server,
                          size_t limit);
void loggerWebSendRawLogContent(PortableSocket client_fd, const LoggerWebServer* server);
size_t loggerWebTotalColumnCount(const LoggerWebServer* server);
const char* loggerWebFieldForColumn(const LogRecord* record, size_t column_index);
int loggerWebParseDouble(const char* value, double* out);
int loggerWebParseUnixTime(const char* value, time_t* out);
int loggerWebLogLocaltime(const time_t* value, struct tm* out);
void loggerWebFormatUnixLabel(time_t value, char* buffer, size_t buffer_size);
void loggerWebFormatUnixDate(time_t value, char* buffer, size_t buffer_size);
void loggerWebFormatUnixTime(time_t value, char* buffer, size_t buffer_size);
void loggerWebFormatDuration(time_t seconds, char* buffer, size_t buffer_size);
int loggerWebResolveColumnIndex(const LoggerWebServer* server,
                                const char* column,
                                size_t* index);
int loggerWebStringEqualsIgnoreCase(const char* left, const char* right);

// Graph configuration, data, and Today panel helpers.
void loggerWebFreeGraphs(LoggerWebServer* server);
void loggerWebFreeTodayColumns(LoggerWebServer* server);
int loggerWebHasGraphs(const LoggerWebServer* server);
LoggerWebGraphRange loggerWebParseGraphRange(const char* request);
const char* loggerWebGraphRangeName(LoggerWebGraphRange range);
int loggerWebGraphRangeWindow(LoggerWebGraphRange range,
                              time_t now,
                              time_t* range_start,
                              time_t* range_end);
int loggerWebGraphStatsWindow(LoggerWebGraphRange range,
                              time_t now,
                              time_t* window_start,
                              time_t* window_end);
void loggerWebSendGraphData(PortableSocket client_fd,
                            const LoggerWebServer* server,
                            LoggerWebGraphRange range,
                            const char* request);
int loggerWebShouldShowTodayPanel(const LoggerWebServer* server, int is_root);
void loggerWebSendTodayPanel(PortableSocket client_fd, const LoggerWebServer* server);
void loggerWebHandleTodayControl(PortableSocket client_fd,
                                 LoggerWebServer* server,
                                 const char* action);
void loggerWebWriteTodayJson(PortableSocket client_fd, const LoggerWebServer* server);

#endif
