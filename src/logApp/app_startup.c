#include "app_startup.h"

#include "logger.h"
#include "loggerSettings.h"
#include "poller.h"
#include "web/loggerWeb.h"
#include "web_controls.h"

#if LOGGER_WEB_PORT > 0 && LOGGER_WEB_POLL_ON_ACCESS > 0
static int loggerWebPollNow(void* arg);
#endif

void appWriteStartupLog(const AppConfig* config)
{
    if (!config) {
        return;
    }

    // Keep startup log maintenance together so main does not need log policy details.
    logger_trim(config->logger);

    const char* fields[] = {"-", "-", "-", "-", "-", "startup", ""};
    logger_log_fields(config->logger, fields, sizeof(fields) / sizeof(fields[0]));
}

void appStartLoggerWeb(const AppConfig* config)
{
    appStartLoggerWebOnPort(config, LOGGER_WEB_PORT);
}

void appStartLoggerWebOnPort(const AppConfig* config, unsigned short port)
{
    if (!config || port == 0) {
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
    LoggerWebConfig logger_web_config = {
        .logger = config->logger,
        .port = port,
        .bind_address = LOGGER_WEB_BIND_ADDRESS,
        .auth_token = LOGGER_WEB_AUTH_TOKEN,
        .title = "Airscape Temperatures",
        .column_headers = logger_web_columns,
        .column_header_count = sizeof(logger_web_columns) / sizeof(logger_web_columns[0]),
        .log_row_limit = LOGGER_WEB_DEFAULT_LOG_LIMIT
    };

    if (loggerWebStartWithConfig(&logger_web_config)) {
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
#if LOGGER_WEB_POLL_ON_ACCESS > 0
        loggerWebSetAccessPoller(LOGGER_WEB_POLL_ON_ACCESS, loggerWebPollNow, (void*)config);
#else
        loggerWebSetAccessPoller(0, NULL, NULL);
#endif

        // The Airscape controller lives outside this web app, so render it as a plain link.
        loggerWebAddNavLink("Airscape", config->house_link, 0);
        //loggerWebAddNavLink("Surprise Me", "https://www.youtube.com/watch?v=dQw4w9WgXcQ", 0);
    }
#else
    (void)config;
#endif
}

#if LOGGER_WEB_PORT > 0 && LOGGER_WEB_POLL_ON_ACCESS > 0
static int loggerWebPollNow(void* arg)
{
    const AppConfig* config = (const AppConfig*)arg;
    return pollerLogCurrentReading(config, "web poll", "page access");
}
#endif
