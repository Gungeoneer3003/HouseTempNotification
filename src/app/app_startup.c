#include "app_startup.h"

#include "logger.h"
#include "poller.h"
#include "settings.h"
#include "web/loggerWeb.h"
#include "web_controls.h"

#if LOGGER_WEB_PORT > 0
static int loggerWebPollNow(void* arg);
#endif

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
        "Outside",
        "Attic"
        };

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
        loggerWebSetAccessPoller(LOGGER_WEB_POLL_ON_ACCESS, loggerWebPollNow, (void*)config);

        // The Airscape controller lives outside this web app, so render it as a plain link.
        loggerWebAddNavLink("Airscape", config->house_link, 0);
    }
#else
    (void)config;
#endif
}

#if LOGGER_WEB_PORT > 0
static int loggerWebPollNow(void* arg)
{
    const AppConfig* config = (const AppConfig*)arg;
    return pollerLogCurrentReading(config, "web poll", "page access");
}
#endif
