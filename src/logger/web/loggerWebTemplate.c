#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L

#include "loggerWeb.h"
#include "loggerWebInternal.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void sendTemplateLine(int client_fd,
                             const char* line,
                             const LoggerWebServer* server,
                             int show_today_panel);
static void sendGraphDataPath(int client_fd, const LoggerWebServer* server);

//Send a template file to the client, replacing placeholders with dynamic content.
void loggerWebSendTemplate(int client_fd,
                           const char* path,
                           const LoggerWebServer* server,
                           int show_today_panel) {
    FILE* file = fopen(path, "r");
    if (!file) {
        loggerWebSendAll(client_fd, "<p>Missing page template.</p>");
        return;
    }

    char buffer[2048];
    while (fgets(buffer, sizeof(buffer), file)) {
        sendTemplateLine(client_fd, buffer, server, show_today_panel);
    }

    fclose(file);
}

//Send a single line of the template to the client, replacing placeholders with dynamic content.
static void sendTemplateLine(int client_fd,
                             const char* line,
                             const LoggerWebServer* server,
                             int show_today_panel) {
    static const char title_placeholder[] = "{{LOGGER_WEB_TITLE}}";
    static const char headers_placeholder[] = "{{LOGGER_WEB_HEADERS}}";
    static const char nav_placeholder[] = "{{LOGGER_WEB_NAV}}";
    static const char today_placeholder[] = "{{LOGGER_WEB_TODAY}}";
    static const char log_rows_placeholder[] = "{{LOGGER_WEB_LOG_ROWS}}";
    static const char raw_log_placeholder[] = "{{LOGGER_WEB_RAW_LOG}}";
    static const char graph_data_path_placeholder[] = "{{LOGGER_WEB_GRAPH_DATA_PATH}}";
    static const char refresh_button_placeholder[] = "{{LOGGER_WEB_SHOW_REFRESH_BUTTON}}";

    static const struct {
        const char* text;
        int type;
    } placeholders[] = {
        {title_placeholder, 1},
        {headers_placeholder, 2},
        {nav_placeholder, 3},
        {today_placeholder, 4},
        {log_rows_placeholder, 5},
        {raw_log_placeholder, 6},
        {graph_data_path_placeholder, 7},
        {refresh_button_placeholder, 8}
    };

    const char* cursor = line;

    for (;;) {
        const char* next = NULL;
        size_t placeholder_index = SIZE_MAX;

        for (size_t i = 0; i < sizeof(placeholders) / sizeof(placeholders[0]); i++) {
            const char* at = strstr(cursor, placeholders[i].text);
            if (at && (!next || at < next)) {
                next = at;
                placeholder_index = i;
            }
        }
        if (!next) {
            loggerWebSendAll(client_fd, cursor);
            return;
        }

        //Send the part of the line before the placeholder
        loggerWebSendBytes(client_fd, cursor, (size_t)(next - cursor));

        //Replace the placeholder with the appropriate dynamic content
        if (placeholders[placeholder_index].type == 1) {
            loggerWebSendEscaped(client_fd, server->title);
        } else if (placeholders[placeholder_index].type == 2) {
            loggerWebSendTableHeaders(client_fd, server);
        } else if (placeholders[placeholder_index].type == 3) {
            loggerWebSendNav(client_fd, server);
        } else if (placeholders[placeholder_index].type == 4) {
            if (show_today_panel) {
                loggerWebSendTodayPanel(client_fd, server);
            }
        } else if (placeholders[placeholder_index].type == 5) {
            loggerWebSendLogRows(client_fd, server);
        } else if (placeholders[placeholder_index].type == 6) {
            loggerWebSendRawLogContent(client_fd, server);
        } else if (placeholders[placeholder_index].type == 7) {
            sendGraphDataPath(client_fd, server);
        } else if (placeholders[placeholder_index].type == 8) {
            loggerWebSendAll(client_fd, server->show_refresh_button ? "1" : "0");
        }

        cursor = next + strlen(placeholders[placeholder_index].text);
    }
}

//Send navigation links for the available views.
void loggerWebSendNav(int client_fd, const LoggerWebServer* server) {
    const char* log_path = loggerWebRootDirectoryEquals(server, LOGGER_WEB_ROOT_LOG) ? "/" : "/log";
    const char* graphs_path = loggerWebRootDirectoryEquals(server, LOGGER_WEB_ROOT_GRAPHS)
        ? "/"
        : "/graphs";

    loggerWebSendAll(client_fd, "<p class=\"nav\"><a href=\"");
    loggerWebSendAll(client_fd, log_path);
    loggerWebSendAll(client_fd, "\">Log</a> <a href=\"/raw\">Raw log</a>");
    if (loggerWebHasGraphs(server)) {
        loggerWebSendAll(client_fd, " <a href=\"");
        loggerWebSendAll(client_fd, graphs_path);
        loggerWebSendAll(client_fd, "\">Graphs</a>");
    }
    loggerWebSendAll(client_fd, "</p>");
}

//Send the graph data endpoint path for the current root directory.
static void sendGraphDataPath(int client_fd, const LoggerWebServer* server) {
    loggerWebSendAll(client_fd,
            loggerWebRootDirectoryEquals(server, LOGGER_WEB_ROOT_GRAPHS) ? "/data" : "/graphs/data");
}

//Send the log table headers.
void loggerWebSendTableHeaders(int client_fd, const LoggerWebServer* server) {
    loggerWebSendAll(client_fd, "<th>Date</th>\n                <th>Time</th>");

    for (size_t i = 0; i < server->column_header_count; i++) {
        loggerWebSendAll(client_fd, "\n                <th>");
        loggerWebSendEscaped(client_fd, server->column_headers[i]);
        loggerWebSendAll(client_fd, "</th>");
    }
}


#endif
