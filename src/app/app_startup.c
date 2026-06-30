#include "app_startup.h"

#include "logger.h"
#include "settings.h"
#include "web/loggerWeb.h"
#include "web_controls.h"

void appWriteStartupLog(const AppConfig* config)
{
    if (!config) {
        return;
    }

    // Keep startup log maintenance together so main does not need log policy details.
    logTrim(config->log_path);
    lprint(config->log_path, "-|-|-|-|-|startup|");
}

void appStartLoggerWeb(const AppConfig* config)
{
    if (!config) {
        return;
    }

#if LOGGER_WEB_PORT > 0
    static const char *const logger_web_columns[] = {
        "Inside",
        "Outside",
        "Attic",
        "Fan Speed",
        "Recommendation",
        "Event",
        "Detail"};
    static const char *const logger_web_temperature_graph_columns[] = {
        "Inside",
        "Attic",
        "Outside"};

    // The web server setup stays declarative here; web_controls owns button behavior.
    if (loggerWebStart(config->log_path,
                       LOGGER_WEB_PORT,
                       "Airscape Temperatures",
                       logger_web_columns,
                       sizeof(logger_web_columns) / sizeof(logger_web_columns[0]))) {
        loggerWebInsertGraphSeries("Temperature Overlay",
                                   "Time",
                                   logger_web_temperature_graph_columns,
                                   sizeof(logger_web_temperature_graph_columns) /
                                       sizeof(logger_web_temperature_graph_columns[0]));
        loggerWebShowStats(1);

        loggerWebShowVerts("Temperature Overlay", "Event", "open notif", "#1a1a8b");
        loggerWebShowVerts("Temperature Overlay", "Event", "close notif", "#8b1a1a");
        loggerWebShowSpan("Temperature Overlay", "Event", "open notif", "close notif", "#176e74");

        webControlsConfigureToday(config);
        loggerWebSetRootDirectory("graphs");

        loggerWebAddNavLink("Airscape", config->house_link);
    }
#else
    (void)config;
#endif
}
