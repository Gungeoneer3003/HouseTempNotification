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

#endif
