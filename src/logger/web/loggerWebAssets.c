//Statement of Purpose:
/*
The purpose of this file is to provide the static assets (CSS and JavaScript) 
for the logger web interface. These assets are embedded in the binary and 
served to clients when requested.
*/

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L

#include "loggerWeb.h"
#include "loggerWebInternal.h"

void loggerWebSendCss(int client_fd) {
    loggerWebSendStaticFile(client_fd, "text/css; charset=utf-8", LOGGER_WEB_CSS_FILE);
}

void loggerWebSendGraphScript(int client_fd) {
    loggerWebSendStaticFile(client_fd,
                   "application/javascript; charset=utf-8",
                   LOGGER_WEB_GRAPH_SCRIPT_FILE);
}


#endif
