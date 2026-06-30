//Statement of Purpose:
/*
 * This file sends HTML page templates with per-route page state.
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L

#include "loggerWeb.h"
#include "loggerWebInternal.h"


static void sendPageTemplate(int client_fd,
                             const LoggerWebServer* server,
                             const char* template_path,
                             int is_root) {
    loggerWebSendHtmlHeader(client_fd);
    loggerWebSendTemplate(client_fd,
                          template_path,
                          server,
                          loggerWebShouldShowTodayPanel(server, is_root));
}

void loggerWebSendIndex(int client_fd, const LoggerWebServer* server, int is_root) {
    sendPageTemplate(client_fd, server, LOGGER_WEB_LOG_TEMPLATE, is_root);
}

void loggerWebSendGraphs(int client_fd, const LoggerWebServer* server, int is_root) {
    sendPageTemplate(client_fd, server, LOGGER_WEB_GRAPHS_TEMPLATE, is_root);
}

void loggerWebSendRawLog(int client_fd, const LoggerWebServer* server, int is_root) {
    sendPageTemplate(client_fd, server, LOGGER_WEB_RAW_TEMPLATE, is_root);
}


#endif
