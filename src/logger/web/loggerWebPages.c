//Statement of Purpose:
/*
 * This file contains the implementation for sending different web pages to the client.
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L

#include "loggerWeb.h"
#include "loggerWebInternal.h"


void loggerWebSendIndex(int client_fd, const LoggerWebServer* server) {
    loggerWebSendHtmlHeader(client_fd);
    loggerWebSendTemplate(client_fd, LOGGER_WEB_LOG_TEMPLATE, server);
}

void loggerWebSendGraphs(int client_fd, const LoggerWebServer* server) {
    loggerWebSendHtmlHeader(client_fd);
    loggerWebSendTemplate(client_fd, LOGGER_WEB_GRAPHS_TEMPLATE, server);
}

void loggerWebSendRawLog(int client_fd, const LoggerWebServer* server) {
    loggerWebSendHtmlHeader(client_fd);
    loggerWebSendTemplate(client_fd, LOGGER_WEB_RAW_TEMPLATE, server);
}


#endif
