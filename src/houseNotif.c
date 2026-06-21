#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "config.h"
#include "houseApi.h"
#include "instanceLock.h"
#include "logger.h"
#include "loggerWeb.h"
#include "portable.h"
#include "rec.h"
#include "settings.h"

int main(void) {
    printf("Starting House Temperature Notification System\n");
    fflush(stdout);

    AppConfig config;
    if (!configLoad(&config, "keys.env")) {
        fprintf(stderr, "Failed to initialize configuration\n");
        return EXIT_FAILURE;
    }

    InstanceLock lock = INSTANCE_LOCK_INIT;
    if (!instanceLockAcquire(&lock, config.lock_path)) {
        return EXIT_FAILURE;
    }

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
        fprintf(stderr, "Failed to initialize libcurl\n");
        instanceLockRelease(&lock);
        return EXIT_FAILURE;
    }

    logTrim(config.log_path);
    logWrite(config.log_path, "startup");
#if LOGGER_WEB_PORT > 0
    loggerWebStart(config.log_path, LOGGER_WEB_PORT);
#endif

    Rec lastSent = REC_NONE;
    time_t lastLogTrimTime = time(NULL);

    for (;;) {
        SensorReading reading;
        time_t now = time(NULL);

        if (difftime(now, lastLogTrimTime) >= LOG_TRIM_INTERVAL_SECONDS) {
            logTrim(config.log_path);
            lastLogTrimTime = now;
        }

        if (!houseReadSensor(&config, &reading)) {
            logWrite(config.log_path, "sensor_fail");
            portableSleepSeconds(POLL_INTERVAL_SECONDS);
            continue;
        }

        Rec rec = getRec(reading.house, reading.outside_air, reading.power);

        if (rec == REC_NONE) {
            logFormat(config.log_path, "idle|%d|%d|%d", 
                      reading.house, reading.outside_air, reading.power);
            portableSleepSeconds(POLL_INTERVAL_SECONDS);
            continue;
        }

        if (shouldSend(rec, lastSent, now)) {
            char msg[256];
            int fanOffOk = 1;
            
            if (rec == REC_CLOSE) {
                fanOffOk = houseTurnOffFans(&config);
            }

            if (rec == REC_OPEN) {
                snprintf(msg, sizeof(msg), "Open the windows");
            } else if (fanOffOk) {
                snprintf(msg, sizeof(msg), "Close the windows");
            } else {
                snprintf(msg, sizeof(msg), "Close the windows (fan failed)");
            }

            if (pushoverSendMessage(&config, msg)) {
                lastSent = rec;
                logFormat(config.log_path, "notify_sent|%s|%d|%d|%d", 
                          msg, reading.house, reading.outside_air, reading.power);
                
                // Sleep until next time window
                long sleep_sec = secUntilWindow(rec, now);
                if (sleep_sec > 0) {
                    portableSleepSeconds((unsigned int)sleep_sec);
                }
                continue;
            } else {
                logFormat(config.log_path, "notify_failed|%d|%d|%d", 
                          reading.house, reading.outside_air, reading.power);
            }
        } else {
            if (!timeOk(rec, now)) {
                long wait_sec = secUntilWindow(rec, now);
                unsigned int sleep_time = (unsigned int)wait_sec < POLL_INTERVAL_SECONDS ?
                                         (unsigned int)wait_sec : POLL_INTERVAL_SECONDS;
                logFormat(config.log_path, "waiting_window|%s|%d|%d|%d", 
                          getRecName(rec), reading.house, reading.outside_air, reading.power);
                portableSleepSeconds(sleep_time);
                continue;
            }
            logFormat(config.log_path, "suppressed|%s|%d|%d|%d", 
                      getRecName(rec), reading.house, reading.outside_air, reading.power);
        }

        portableSleepSeconds(POLL_INTERVAL_SECONDS);
    }

    curl_global_cleanup();
    instanceLockRelease(&lock);
    return EXIT_SUCCESS;
}
