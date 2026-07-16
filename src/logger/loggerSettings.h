#ifndef LOGGER_SETTINGS_H
#define LOGGER_SETTINGS_H

// Path to the log file.
#ifndef DEFAULT_LOG_FILE
#define DEFAULT_LOG_FILE "house_notify.log"
#endif

// Number of days to retain logs.
#ifndef LOG_RETENTION_DAYS
#define LOG_RETENTION_DAYS 30
#endif

// Size of the buffer for formatted log messages.
#ifndef MESSAGE_SIZE
#define MESSAGE_SIZE 512
#endif

// How often to trim old logs (in seconds).
#ifndef LOG_TRIM_INTERVAL_SECONDS
#define LOG_TRIM_INTERVAL_SECONDS 86400
#endif

// Port for the logger web viewer. Set to 0 to disable.
#ifndef LOGGER_WEB_PORT
#define LOGGER_WEB_PORT 8080
#endif

// Bind address for the logger web viewer. Use "0.0.0.0" to expose it beyond localhost.
// If this is not "0.0.0.0", then it is only accessible via localhost.
#ifndef LOGGER_WEB_BIND_ADDRESS
#define LOGGER_WEB_BIND_ADDRESS "0.0.0.0"
#endif

// Optional token required for logger web requests when non-empty.
// Basically a password.
#ifndef LOGGER_WEB_AUTH_TOKEN
#define LOGGER_WEB_AUTH_TOKEN ""
#endif

// The log view keeps only the newest rows in memory while rendering.
#ifndef LOGGER_WEB_DEFAULT_LOG_LIMIT
#define LOGGER_WEB_DEFAULT_LOG_LIMIT 500
#endif

// Fan control POST routes are disabled unless explicitly enabled.
#ifndef LOGGER_WEB_ENABLE_CONTROLS
#define LOGGER_WEB_ENABLE_CONTROLS 1
#endif

// How long to wait before reading back fan state after a web control command.
#ifndef LOGGER_WEB_FAN_SETTLE_SECONDS
#define LOGGER_WEB_FAN_SETTLE_SECONDS 1
#endif

// The fan shutters take longer to open than ordinary speed changes. Wait for
// that mechanical operation before deciding whether power-on succeeded.
#ifndef LOGGER_WEB_FAN_POWER_ON_SECONDS
#define LOGGER_WEB_FAN_POWER_ON_SECONDS 10
#endif

// When web page loads should poll and log a fresh sensor reading.
// 0 = don't update, 1 = update only if the root page is accessed, 2 = update if any page is accessed.
#ifndef LOGGER_WEB_POLL_ON_ACCESS
#define LOGGER_WEB_POLL_ON_ACCESS 1
#endif

#endif
