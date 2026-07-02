//Statement of Purpose:
/*
 * This file sends HTML page templates with per-route page state.
 */

#define _POSIX_C_SOURCE 200809L

#include "loggerWeb.h"
#include "loggerWebInternal.h"


static void sendPageTemplate(PortableSocket client_fd,
                             const LoggerWebServer* server,
                             const char* template_path,
                             int is_root,
                             LoggerWebPage current_page,
                             size_t log_limit) {
    loggerWebSendHtmlHeader(client_fd);
    loggerWebSendTemplate(client_fd,
                          template_path,
                          server,
                          loggerWebShouldShowTodayPanel(server, is_root),
                          current_page,
                          log_limit);
}

void loggerWebSendIndex(PortableSocket client_fd,
                        const LoggerWebServer* server,
                        int is_root,
                        size_t log_limit) {
    sendPageTemplate(client_fd,
                     server,
                     LOGGER_WEB_LOG_TEMPLATE,
                     is_root,
                     LOGGER_WEB_PAGE_LOG,
                     log_limit);
}

void loggerWebSendGraphs(PortableSocket client_fd, const LoggerWebServer* server, int is_root) {
    sendPageTemplate(client_fd,
                     server,
                     LOGGER_WEB_GRAPHS_TEMPLATE,
                     is_root,
                     LOGGER_WEB_PAGE_GRAPHS,
                     server ? server->log_row_limit : LOGGER_WEB_DEFAULT_LOG_LIMIT);
}

void loggerWebSendRawLog(PortableSocket client_fd, const LoggerWebServer* server, int is_root) {
    sendPageTemplate(client_fd,
                     server,
                     LOGGER_WEB_RAW_TEMPLATE,
                     is_root,
                     LOGGER_WEB_PAGE_RAW,
                     server ? server->log_row_limit : LOGGER_WEB_DEFAULT_LOG_LIMIT);
}
