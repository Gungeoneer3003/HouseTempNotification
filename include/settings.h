#ifndef SETTINGS_H
#define SETTINGS_H

#include <stddef.h>

#ifndef POLL_INTERVAL_SECONDS
#define POLL_INTERVAL_SECONDS 300
#endif

#ifndef SENSOR_RETRY_COUNT
#define SENSOR_RETRY_COUNT 10
#endif

#ifndef CURL_CONNECT_TIMEOUT_SECONDS
#define CURL_CONNECT_TIMEOUT_SECONDS 10L
#endif

#ifndef CURL_TOTAL_TIMEOUT_SECONDS
#define CURL_TOTAL_TIMEOUT_SECONDS 20L
#endif

#ifndef MAX_RESPONSE_BYTES
#define MAX_RESPONSE_BYTES ((size_t)64 * 1024)
#endif

#ifndef OPEN_MARGIN
#define OPEN_MARGIN 2
#endif

#ifndef CLOSE_MARGIN
#define CLOSE_MARGIN 2
#endif

#ifndef REQUIRED_STABLE_POLLS
#define REQUIRED_STABLE_POLLS 2
#endif

#ifndef SAME_ALERT_REMINDER_SECONDS
#define SAME_ALERT_REMINDER_SECONDS 0
#endif

#ifndef DEFAULT_LOG_FILE
#define DEFAULT_LOG_FILE "house_notify.log"
#endif

#ifndef DEFAULT_LOCK_FILE
#define DEFAULT_LOCK_FILE "house_notify.lock"
#endif

#ifndef LOG_RETENTION_DAYS
#define LOG_RETENTION_DAYS 30
#endif

#ifndef LOG_TRIM_INTERVAL_SECONDS
#define LOG_TRIM_INTERVAL_SECONDS 86400
#endif

#endif
