#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L

#include "../loggerWeb.h"
#include "../loggerWebInternal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOGGER_WEB_TODAY_MAX_COLUMNS 16
#define LOGGER_WEB_TODAY_NAME_SIZE 128

typedef struct {
    char names[LOGGER_WEB_TODAY_MAX_COLUMNS][LOGGER_WEB_TODAY_NAME_SIZE];
    double values[LOGGER_WEB_TODAY_MAX_COLUMNS];
    int has_value[LOGGER_WEB_TODAY_MAX_COLUMNS];
    double fan_speed;
    int has_fan_speed;
    int fan_power_on;
    int has_fan_power;
    char latest_time[128];
} LoggerWebTodaySnapshot;

static void freeTodayColumns(LoggerWebTodayColumn* columns, size_t column_count);
static int readTodaySnapshot(const LoggerWebServer* server, LoggerWebTodaySnapshot* snapshot);
static void sendTodayReadings(int client_fd,
                              const LoggerWebTodaySnapshot* snapshot,
                              size_t column_count);
static void sendTodayControls(int client_fd,
                              const LoggerWebTodaySnapshot* snapshot,
                              const LoggerWebTodayControls* controls);
static void sendTodayActionButton(int client_fd,
                                  const char* class_name,
                                  const char* endpoint,
                                  const char* label,
                                  int enabled,
                                  const char* icon_class);
static void sendTodayValue(int client_fd, double value);
static void writeTodayControlsJson(int client_fd,
                                   const LoggerWebServer* server,
                                   const LoggerWebTodaySnapshot* snapshot);

int loggerWebShowToday(const char* const* columns,
                       size_t column_count,
                       int show_on_other_pages,
                       int show_controls) {
    if (column_count > 0 && !columns) {
        return 0;
    }

    pthread_mutex_lock(&active_server_mutex);
    LoggerWebServer* server = active_server;
    if (!server) {
        pthread_mutex_unlock(&active_server_mutex);
        return 0;
    }

    LoggerWebTodayColumn* next_columns = NULL;
    if (column_count > 0) {
        next_columns = calloc(column_count, sizeof(*next_columns));
        if (!next_columns) {
            pthread_mutex_unlock(&active_server_mutex);
            return 0;
        }

        for (size_t i = 0; i < column_count; i++) {
            if (!columns[i] || !*columns[i] ||
                !loggerWebResolveColumnIndex(server, columns[i], &next_columns[i].index)) {
                freeTodayColumns(next_columns, i);
                pthread_mutex_unlock(&active_server_mutex);
                return 0;
            }

            next_columns[i].name = loggerWebCopyString(columns[i]);
            if (!next_columns[i].name) {
                freeTodayColumns(next_columns, i);
                pthread_mutex_unlock(&active_server_mutex);
                return 0;
            }
        }
    }

    loggerWebFreeTodayColumns(server);
    server->today_columns = next_columns;
    server->today_column_count = column_count;
    server->show_today_on_other_pages = show_on_other_pages != 0;
    server->show_today_controls = show_controls != 0;
    pthread_mutex_unlock(&active_server_mutex);
    return 1;
}

int loggerWebSetTodayControls(const LoggerWebTodayControls* controls) {
    pthread_mutex_lock(&active_server_mutex);
    LoggerWebServer* server = active_server;
    if (!server) {
        pthread_mutex_unlock(&active_server_mutex);
        return 0;
    }

    //Copy optional fan control callbacks; a NULL struct disables all buttons.
    if (controls) {
        server->today_controls = *controls;
    } else {
        memset(&server->today_controls, 0, sizeof(server->today_controls));
    }

    pthread_mutex_unlock(&active_server_mutex);
    return 1;
}

void loggerWebFreeTodayColumns(LoggerWebServer* server) {
    if (!server) {
        return;
    }

    freeTodayColumns(server->today_columns, server->today_column_count);
    server->today_columns = NULL;
    server->today_column_count = 0;
    server->show_today_controls = 0;
}

int loggerWebShouldShowTodayPanel(const LoggerWebServer* server, int is_root) {
    int should_show = 0;

    pthread_mutex_lock(&active_server_mutex);
    should_show = server &&
                  server->today_column_count > 0 &&
                  (is_root || server->show_today_on_other_pages);
    pthread_mutex_unlock(&active_server_mutex);

    return should_show;
}

void loggerWebSendTodayPanel(int client_fd, const LoggerWebServer* server) {
    if (!server) {
        return;
    }

    pthread_mutex_lock(&active_server_mutex);
    LoggerWebTodaySnapshot snapshot;
    if (!readTodaySnapshot(server, &snapshot)) {
        loggerWebSendAll(client_fd, "<section id=\"today\" class=\"today-panel is-hidden\"></section>");
        pthread_mutex_unlock(&active_server_mutex);
        return;
    }

    if (server->show_today_controls) {
        loggerWebSendAll(client_fd, "<section id=\"today\" class=\"today-panel today-panel--controls\">");
        loggerWebSendAll(client_fd, "<div class=\"today-main\">");
        sendTodayReadings(client_fd, &snapshot, server->today_column_count);
        loggerWebSendAll(client_fd, "</div>");
        sendTodayControls(client_fd, &snapshot, &server->today_controls);
    } else {
        loggerWebSendAll(client_fd, "<section id=\"today\" class=\"today-panel\">");
        sendTodayReadings(client_fd, &snapshot, server->today_column_count);
    }
    loggerWebSendAll(client_fd, "</section>");
    pthread_mutex_unlock(&active_server_mutex);
}

static void freeTodayColumns(LoggerWebTodayColumn* columns, size_t column_count) {
    if (!columns) {
        return;
    }

    for (size_t i = 0; i < column_count; i++) {
        free(columns[i].name);
    }

    free(columns);
}

static int readTodaySnapshot(const LoggerWebServer* server, LoggerWebTodaySnapshot* snapshot) {
    if (!server || !snapshot ||
        server->today_column_count == 0 ||
        server->today_column_count > LOGGER_WEB_TODAY_MAX_COLUMNS) {
        return 0;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    for (size_t i = 0; i < LOGGER_WEB_TODAY_MAX_COLUMNS; i++) {
        snapshot->values[i] = 0.0;
        snapshot->has_value[i] = 0;
    }

    for (size_t i = 0; i < server->today_column_count; i++) {
        snprintf(snapshot->names[i],
                 sizeof(snapshot->names[i]),
                 "%s",
                 server->today_columns[i].name ? server->today_columns[i].name : "");
    }

    size_t column_count = loggerWebTotalColumnCount(server);
    size_t fan_power_index = 0;
    int has_fan_power_column = loggerWebResolveColumnIndex(server, "Power", &fan_power_index);
    FILE* file = fopen(server->log_path, "r");
    if (file) {
        char** fields = calloc(column_count, sizeof(*fields));
        char line[LOGGER_WEB_MAX_LINE];
        while (fields && fgets(line, sizeof(line), file)) {
            char* newline = strpbrk(line, "\r\n");
            if (newline) {
                *newline = '\0';
            }

            loggerWebSplitFields(line, fields, column_count);
            time_t logged_at = 0;
            int has_logged_at = loggerWebParseUnixTime(fields[LOGGER_WEB_UNIX_FIELD], &logged_at);
            int any_value = 0;
            double row_values[LOGGER_WEB_TODAY_MAX_COLUMNS] = {0};
            int row_has_value[LOGGER_WEB_TODAY_MAX_COLUMNS] = {0};
            double row_fan_power = 0.0;
            int row_has_fan_power = 0;

            for (size_t i = 0; i < server->today_column_count; i++) {
                const char* field = loggerWebFieldForColumn(
                    fields,
                    server->today_columns[i].index);
                if (loggerWebParseDouble(field, &row_values[i])) {
                    row_has_value[i] = 1;
                    any_value = 1;
                }
            }

            if (has_fan_power_column &&
                loggerWebParseDouble(loggerWebFieldForColumn(fields, fan_power_index),
                                     &row_fan_power)) {
                row_has_fan_power = 1;
            }

            if (!any_value && !row_has_fan_power) {
                continue;
            }

            if (any_value) {
                for (size_t i = 0; i < server->today_column_count; i++) {
                    snapshot->values[i] = row_values[i];
                    snapshot->has_value[i] = row_has_value[i];
                }
            }

            //Use the existing Power log column as the fan state source for the control boxes.
            if (row_has_fan_power) {
                snapshot->fan_speed = row_fan_power;
                snapshot->has_fan_speed = 1;
                snapshot->fan_power_on = row_fan_power != 0.0;
                snapshot->has_fan_power = 1;
            }

            if (has_logged_at) {
                loggerWebFormatUnixTime(logged_at,
                                        snapshot->latest_time,
                                        sizeof(snapshot->latest_time));
            } else if (loggerWebRowHasSplitDateTime(fields)) {
                snprintf(snapshot->latest_time,
                         sizeof(snapshot->latest_time),
                         "%s",
                         fields[LOGGER_WEB_TIME_FIELD]);
            } else {
                snprintf(snapshot->latest_time,
                         sizeof(snapshot->latest_time),
                         "%s",
                         fields[LOGGER_WEB_DATE_FIELD] ? fields[LOGGER_WEB_DATE_FIELD] : "");
            }
        }

        free(fields);
        fclose(file);
    }

    return 1;
}

static void sendTodayReadings(int client_fd,
                              const LoggerWebTodaySnapshot* snapshot,
                              size_t column_count) {
    loggerWebSendAll(client_fd, "<div class=\"today-heading\">Current readings</div>");
    loggerWebSendAll(client_fd, "<div class=\"today-readings\">");
    for (size_t i = 0; i < column_count; i++) {
        loggerWebSendAll(client_fd, "<div class=\"today-reading\"><span class=\"today-label\">");
        loggerWebSendEscaped(client_fd, snapshot->names[i]);
        loggerWebSendAll(client_fd, "</span><span class=\"today-value\">");
        if (snapshot->has_value[i]) {
            sendTodayValue(client_fd, snapshot->values[i]);
        } else {
            loggerWebSendAll(client_fd, "--");
        }
        loggerWebSendAll(client_fd, "</span></div>");
    }
    loggerWebSendAll(client_fd, "</div>");
    if (snapshot->latest_time[0]) {
        loggerWebSendAll(client_fd, "<div class=\"today-updated\">Updated ");
        loggerWebSendEscaped(client_fd, snapshot->latest_time);
        loggerWebSendAll(client_fd, "</div>");
    }
}

static void sendTodayControls(int client_fd,
                              const LoggerWebTodaySnapshot* snapshot,
                              const LoggerWebTodayControls* controls) {
    int can_speed_up = controls && controls->speed_up != NULL;
    int can_speed_down = controls && controls->speed_down != NULL;
    int can_power_toggle = controls && controls->power_toggle != NULL;
    int power_on = snapshot->has_fan_power && snapshot->fan_power_on;

    loggerWebSendAll(client_fd, "<div class=\"today-controls\" aria-label=\"Fan controls\">");
    loggerWebSendAll(client_fd, "<div class=\"today-control-box today-speed-box\">");
    loggerWebSendAll(client_fd, "<div class=\"today-speed-readout\"><span class=\"today-label\">Fan speed</span>");
    loggerWebSendAll(client_fd, "<span class=\"today-value\">");
    if (snapshot->has_fan_speed) {
        sendTodayValue(client_fd, snapshot->fan_speed);
    } else {
        loggerWebSendAll(client_fd, "--");
    }
    loggerWebSendAll(client_fd, "</span></div>");
    loggerWebSendAll(client_fd, "<div class=\"today-step-buttons\">");
    sendTodayActionButton(client_fd,
                          "today-triangle-button today-triangle-button--up",
                          "/today/fan/speed/up",
                          "Increase fan speed",
                          can_speed_up,
                          "today-triangle");
    sendTodayActionButton(client_fd,
                          "today-triangle-button today-triangle-button--down",
                          "/today/fan/speed/down",
                          "Decrease fan speed",
                          can_speed_down,
                          "today-triangle");
    loggerWebSendAll(client_fd, "</div></div>");

    loggerWebSendAll(client_fd, "<button type=\"button\" class=\"today-control-box today-power-control");
    loggerWebSendAll(client_fd, power_on ? " is-on" : " is-off");
    loggerWebSendAll(client_fd, "\" data-today-endpoint=\"/today/fan/power/toggle\" aria-label=\"Toggle fan power\" aria-pressed=\"");
    loggerWebSendAll(client_fd, power_on ? "true" : "false");
    loggerWebSendAll(client_fd, "\"");
    if (!can_power_toggle) {
        loggerWebSendAll(client_fd, " disabled aria-disabled=\"true\"");
    }
    loggerWebSendAll(client_fd, "><span class=\"today-power-icon\" aria-hidden=\"true\"></span></button>");
    loggerWebSendAll(client_fd, "</div>");
}

static void sendTodayActionButton(int client_fd,
                                  const char* class_name,
                                  const char* endpoint,
                                  const char* label,
                                  int enabled,
                                  const char* icon_class) {
    loggerWebSendAll(client_fd, "<button type=\"button\" class=\"");
    loggerWebSendEscaped(client_fd, class_name);
    loggerWebSendAll(client_fd, "\" data-today-endpoint=\"");
    loggerWebSendEscaped(client_fd, endpoint);
    loggerWebSendAll(client_fd, "\" aria-label=\"");
    loggerWebSendEscaped(client_fd, label);
    loggerWebSendAll(client_fd, "\"");
    if (!enabled) {
        loggerWebSendAll(client_fd, " disabled aria-disabled=\"true\"");
    }
    loggerWebSendAll(client_fd, "><span class=\"");
    loggerWebSendEscaped(client_fd, icon_class);
    loggerWebSendAll(client_fd, "\" aria-hidden=\"true\"></span></button>");
}

static void sendTodayValue(int client_fd, double value) {
    char number[64];
    double absolute_value = value < 0.0 ? -value : value;
    snprintf(number,
             sizeof(number),
             absolute_value >= 100.0 ? "%.0f" : "%.1f",
             value);
    loggerWebSendEscaped(client_fd, number);
}

void loggerWebHandleTodayControl(int client_fd,
                                 const LoggerWebServer* server,
                                 const char* action) {
    LoggerWebTodayControls controls;
    memset(&controls, 0, sizeof(controls));
    int show_controls = 0;

    //Copy the callback pointers under the lock, then run callbacks without holding it.
    pthread_mutex_lock(&active_server_mutex);
    if (server && server->show_today_controls) {
        controls = server->today_controls;
        show_controls = 1;
    }
    pthread_mutex_unlock(&active_server_mutex);

    int (*handler)(void*) = NULL;
    if (show_controls && action) {
        if (strcmp(action, "speed-up") == 0) {
            handler = controls.speed_up;
        } else if (strcmp(action, "speed-down") == 0) {
            handler = controls.speed_down;
        } else if (strcmp(action, "power-toggle") == 0) {
            handler = controls.power_toggle;
        }
    }

    if (!handler) {
        loggerWebSendPlainStatus(client_fd,
                                 503,
                                 "Service Unavailable",
                                 "Fan control is not configured.\n");
        return;
    }

    if (!handler(controls.user)) {
        loggerWebSendPlainStatus(client_fd,
                                 500,
                                 "Internal Server Error",
                                 "Fan control request failed.\n");
        return;
    }

    loggerWebSendNoContent(client_fd);
}

void loggerWebWriteTodayJson(int client_fd, const LoggerWebServer* server) {
    LoggerWebTodaySnapshot snapshot;
    if (!readTodaySnapshot(server, &snapshot)) {
        loggerWebSendAll(client_fd, "null");
        return;
    }

    loggerWebSendAll(client_fd, "{\"time\":\"");
    loggerWebSendJsonEscaped(client_fd, snapshot.latest_time);
    loggerWebSendAll(client_fd, "\",\"columns\":[");
    for (size_t i = 0; i < server->today_column_count; i++) {
        if (i > 0) {
            loggerWebSendAll(client_fd, ",");
        }

        loggerWebSendAll(client_fd, "{\"name\":\"");
        loggerWebSendJsonEscaped(client_fd, snapshot.names[i]);
        loggerWebSendAll(client_fd, "\",\"value\":");
        if (snapshot.has_value[i]) {
            char number[64];
            snprintf(number, sizeof(number), "%.17g", snapshot.values[i]);
            loggerWebSendAll(client_fd, number);
        } else {
            loggerWebSendAll(client_fd, "null");
        }
        loggerWebSendAll(client_fd, "}");
    }
    loggerWebSendAll(client_fd, "],\"controls\":");
    writeTodayControlsJson(client_fd, server, &snapshot);
    loggerWebSendAll(client_fd, "}");
}

static void writeTodayControlsJson(int client_fd,
                                   const LoggerWebServer* server,
                                   const LoggerWebTodaySnapshot* snapshot) {
    if (!server->show_today_controls) {
        loggerWebSendAll(client_fd, "null");
        return;
    }

    loggerWebSendAll(client_fd, "{\"enabled\":true,\"fanSpeed\":");
    if (snapshot->has_fan_speed) {
        char number[64];
        snprintf(number, sizeof(number), "%.17g", snapshot->fan_speed);
        loggerWebSendAll(client_fd, number);
    } else {
        loggerWebSendAll(client_fd, "null");
    }

    loggerWebSendAll(client_fd, ",\"fanPowerOn\":");
    if (snapshot->has_fan_power) {
        loggerWebSendAll(client_fd, snapshot->fan_power_on ? "true" : "false");
    } else {
        loggerWebSendAll(client_fd, "null");
    }

    loggerWebSendAll(client_fd, ",\"canSpeedUp\":");
    loggerWebSendAll(client_fd, server->today_controls.speed_up ? "true" : "false");
    loggerWebSendAll(client_fd, ",\"canSpeedDown\":");
    loggerWebSendAll(client_fd, server->today_controls.speed_down ? "true" : "false");
    loggerWebSendAll(client_fd, ",\"canPowerToggle\":");
    loggerWebSendAll(client_fd, server->today_controls.power_toggle ? "true" : "false");
    loggerWebSendAll(client_fd, "}");
}


#endif
