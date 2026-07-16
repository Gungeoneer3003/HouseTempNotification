#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "loggerWeb.h"
#include "loggerWebInternal.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void sendTemplateLine(PortableSocket client_fd,
                             const char* line,
                             const LoggerWebServer* server,
                             int show_today_panel,
                             LoggerWebPage current_page,
                             size_t log_limit);
static void sendGraphDataPath(PortableSocket client_fd, const LoggerWebServer* server);
static void sendNavButton(PortableSocket client_fd,
                          const char* href,
                          const char* label,
                          const char* icon_class,
                          int is_active,
                          int open_in_new_tab);
static void sendCustomNavButton(PortableSocket client_fd,
                                const LoggerWebServer* server,
                                const LoggerWebNavLink* link);
static void formatRootRelativeHref(const LoggerWebServer* server,
                                   const char* subdirectory,
                                   char* output,
                                   size_t output_size);
static void formatPlainHref(const char* href, char* output, size_t output_size);
static int hasUriScheme(const char* href);

//Send a template file to the client, replacing placeholders with dynamic content.
void loggerWebSendTemplate(PortableSocket client_fd,
                           const char* path,
                           const LoggerWebServer* server,
                           int show_today_panel,
                           LoggerWebPage current_page,
                           size_t log_limit) {
    FILE* file = fopen(path, "r");
    if (!file) {
        loggerWebSendAll(client_fd, "<p>Missing page template.</p>");
        return;
    }

    char buffer[2048];
    while (fgets(buffer, sizeof(buffer), file)) {
        sendTemplateLine(client_fd,
                         buffer,
                         server,
                         show_today_panel,
                         current_page,
                         log_limit);
    }

    fclose(file);
}

//Send a single line of the template to the client, replacing placeholders with dynamic content.
static void sendTemplateLine(PortableSocket client_fd,
                             const char* line,
                             const LoggerWebServer* server,
                             int show_today_panel,
                             LoggerWebPage current_page,
                             size_t log_limit) {
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
            loggerWebSendNav(client_fd, server, current_page);
        } else if (placeholders[placeholder_index].type == 4) {
            if (show_today_panel) {
                loggerWebSendTodayPanel(client_fd, server);
            }
        } else if (placeholders[placeholder_index].type == 5) {
            loggerWebSendLogRows(client_fd, server, log_limit);
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
void loggerWebSendNav(PortableSocket client_fd,
                      const LoggerWebServer* server,
                      LoggerWebPage current_page) {
    const char* log_path = loggerWebRootDirectoryEquals(server, LOGGER_WEB_ROOT_LOG) ? "/" : "/log";
    const char* graphs_path = loggerWebRootDirectoryEquals(server, LOGGER_WEB_ROOT_GRAPHS)
        ? "/"
        : "/graphs";

    loggerWebSendAll(client_fd, "<nav class=\"nav\" aria-label=\"Views\">");
    sendNavButton(client_fd, log_path, "Log", "nav-icon--log", current_page == LOGGER_WEB_PAGE_LOG, 0);
    sendNavButton(client_fd, "/raw", "Raw log", "nav-icon--raw", current_page == LOGGER_WEB_PAGE_RAW, 0);
    if (loggerWebHasGraphs(server)) {
        sendNavButton(client_fd, graphs_path, "Graphs", "nav-icon--graphs", current_page == LOGGER_WEB_PAGE_GRAPHS, 0);
    }
    for (size_t i = 0; i < server->nav_link_count; i++) {
        sendCustomNavButton(client_fd, server, &server->nav_links[i]);
    }
    loggerWebSendAll(client_fd, "</nav>");
}

static void sendCustomNavButton(PortableSocket client_fd,
                                const LoggerWebServer* server,
                                const LoggerWebNavLink* link) {
    char href[LOGGER_WEB_MAX_PATH];
    const char* rendered_href = href;

    // Root-relative custom links follow whichever built-in page owns "/".
    if (link->root_relative) {
        formatRootRelativeHref(server, link->href, href, sizeof(href));
    } else {
        formatPlainHref(link->href, href, sizeof(href));
    }

    sendNavButton(client_fd,
                  rendered_href,
                  link->label,
                  "nav-icon--link",
                  0,
                  hasUriScheme(rendered_href));
}

static void formatRootRelativeHref(const LoggerWebServer* server,
                                   const char* subdirectory,
                                   char* output,
                                   size_t output_size) {
    while (subdirectory && *subdirectory == '/') {
        subdirectory++;
    }

    if (loggerWebRootDirectoryEquals(server, LOGGER_WEB_ROOT_GRAPHS)) {
        snprintf(output, output_size, "/graphs/%s", subdirectory ? subdirectory : "");
    } else {
        snprintf(output, output_size, "/%s", subdirectory ? subdirectory : "");
    }
}

static void formatPlainHref(const char* href, char* output, size_t output_size) {
    if (!href || !output || output_size == 0) {
        return;
    }

    // Browser hrefs without a scheme are relative, so host/IP links need http://.
    if (hasUriScheme(href) || href[0] == '/' || href[0] == '#' || href[0] == '?') {
        snprintf(output, output_size, "%s", href);
    } else {
        snprintf(output, output_size, "http://%s", href);
    }
}

static int hasUriScheme(const char* href) {
    if (!href || !isalpha((unsigned char)href[0])) {
        return 0;
    }

    for (const char* p = href + 1; *p; p++) {
        if (*p == ':') {
            return 1;
        }
        if (!(isalnum((unsigned char)*p) || *p == '+' || *p == '-' || *p == '.')) {
            return 0;
        }
    }

    return 0;
}

static void sendNavButton(PortableSocket client_fd,
                          const char* href,
                          const char* label,
                          const char* icon_class,
                          int is_active,
                          int open_in_new_tab) {
    loggerWebSendAll(client_fd, "<a class=\"nav-button");
    if (is_active) {
        loggerWebSendAll(client_fd, " is-active");
    }
    loggerWebSendAll(client_fd, "\" href=\"");
    loggerWebSendEscaped(client_fd, href);
    loggerWebSendAll(client_fd, "\"");
    if (is_active) {
        loggerWebSendAll(client_fd, " aria-current=\"page\"");
    }
    if (open_in_new_tab) {
        loggerWebSendAll(client_fd, " target=\"_blank\" rel=\"noopener noreferrer\"");
    }
    loggerWebSendAll(client_fd, "><span class=\"nav-icon ");
    loggerWebSendEscaped(client_fd, icon_class);
    loggerWebSendAll(client_fd, "\" aria-hidden=\"true\"></span><span>");
    loggerWebSendEscaped(client_fd, label);
    loggerWebSendAll(client_fd, "</span></a>");
}

//Send the graph data endpoint path for the current root directory.
static void sendGraphDataPath(PortableSocket client_fd, const LoggerWebServer* server) {
    loggerWebSendAll(client_fd,
            loggerWebRootDirectoryEquals(server, LOGGER_WEB_ROOT_GRAPHS) ? "/data" : "/graphs/data");
}

//Send the log table headers.
void loggerWebSendTableHeaders(PortableSocket client_fd, const LoggerWebServer* server) {
    loggerWebSendAll(client_fd, "<th>Date</th>\n                <th>Time</th>");

    for (size_t i = 0; i < server->column_header_count; i++) {
        loggerWebSendAll(client_fd, "\n                <th>");
        loggerWebSendEscaped(client_fd, server->column_headers[i]);
        loggerWebSendAll(client_fd, "</th>");
    }
}


