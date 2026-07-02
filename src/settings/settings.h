#ifndef SETTINGS_H
#define SETTINGS_H
#include <stddef.h>

//How long to wait before polling the sensor again (in seconds)
#ifndef POLL_INTERVAL_SECONDS
#define POLL_INTERVAL_SECONDS 300
#endif

//How many times to retry the sensor
#ifndef SENSOR_RETRY_COUNT
#define SENSOR_RETRY_COUNT 10
#endif

//How long to wait between sensor retries (in seconds)
#ifndef CURL_CONNECT_TIMEOUT_SECONDS
#define CURL_CONNECT_TIMEOUT_SECONDS 10L
#endif

//How long to wait for the entire HTTP request to complete (in seconds)
#ifndef CURL_TOTAL_TIMEOUT_SECONDS
#define CURL_TOTAL_TIMEOUT_SECONDS 20L
#endif

//How long to wait between retries of failed HTTP requests (in seconds)
#ifndef MAX_RESPONSE_BYTES
#define MAX_RESPONSE_BYTES ((size_t)64 * 1024)
#endif

//Margin for difference in outside and inside (in degrees Fahrenheit)
#ifndef MARGIN
#define MARGIN 0
#endif

//Hour of day (0-23) after which Open messages are allowed (e.g., 15 = 3pm)
#ifndef ALLOW_OPEN_AFTER_HOUR
#define ALLOW_OPEN_AFTER_HOUR 15
#endif

//Hour of day (0-23) after which Close messages are allowed (e.g., 3 = 3am)
#ifndef ALLOW_CLOSE_AFTER_HOUR
#define ALLOW_CLOSE_AFTER_HOUR 3
#endif

//Path to the log file
#ifndef DEFAULT_LOG_FILE
#define DEFAULT_LOG_FILE "house_notify.log"
#endif

//Path to the lock file
#ifndef DEFAULT_LOCK_FILE
#define DEFAULT_LOCK_FILE "house_notify.lock"
#endif

//Number of days to retain logs
#ifndef LOG_RETENTION_DAYS
#define LOG_RETENTION_DAYS 30
#endif

//Size of the buffer for formatted log messages
#ifndef MESSAGE_SIZE
#define MESSAGE_SIZE 512
#endif

//How often to trim old logs (in seconds)
#ifndef LOG_TRIM_INTERVAL_SECONDS
#define LOG_TRIM_INTERVAL_SECONDS 86400
#endif

//Port for the logger web viewer. Set to 0 to disable.
#ifndef LOGGER_WEB_PORT
#define LOGGER_WEB_PORT 8080
#endif

//Bind address for the logger web viewer. Use "0.0.0.0" to expose it beyond localhost.
//If this is not "0.0.0.0", then it's only accessable via localhost
#ifndef LOGGER_WEB_BIND_ADDRESS
#define LOGGER_WEB_BIND_ADDRESS "0.0.0.0"
#endif

//Optional token required for logger web requests when non-empty.
//Basically a password
#ifndef LOGGER_WEB_AUTH_TOKEN
#define LOGGER_WEB_AUTH_TOKEN ""
#endif

//The log view keeps only the newest rows in memory while rendering.
#ifndef LOGGER_WEB_DEFAULT_LOG_LIMIT
#define LOGGER_WEB_DEFAULT_LOG_LIMIT 500
#endif

//Fan control POST routes are disabled unless explicitly enabled.
#ifndef LOGGER_WEB_ENABLE_CONTROLS
#define LOGGER_WEB_ENABLE_CONTROLS 1
#endif

//The default speed for the fan after being awoken from power off
#ifndef DEF_FAN_SPEED
#define DEF_FAN_SPEED 4
#endif

//When web page loads should poll and log a fresh sensor reading.
//0 = don't update, 1 = update only if the root page is accessed, 2 = update if any page is accessed.
#ifndef LOGGER_WEB_POLL_ON_ACCESS
#define LOGGER_WEB_POLL_ON_ACCESS 1
#endif

#endif
