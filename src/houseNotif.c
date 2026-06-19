#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "config.h"
#include "houseApi.h"
#include "instanceLock.h"
#include "logger.h"
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

    loggerTrim(config.log_path);
    loggerAppend(config.log_path, "startup", -1, -1, -1, getRecName(REC_NONE), "program started");

    Rec lastSent = REC_NONE;
    Rec pending = REC_NONE;
    int pendingCount = 0;
    time_t lastSentTime = 0;
    time_t lastLogTrimTime = time(NULL);

    for (;;) {
        SensorReading reading;
        time_t now = time(NULL);

        if (difftime(now, lastLogTrimTime) >= LOG_TRIM_INTERVAL_SECONDS) {
            loggerTrim(config.log_path);
            lastLogTrimTime = now;
        }

        if (!houseReadSensor(&config, &reading)) {
            fprintf(stderr, "Skipping this poll because the sensor read failed\n");
            loggerAppend(config.log_path, "sensor_fail", -1, -1, -1, getRecName(REC_NONE), "sensor read failed");
            portableSleepSeconds(POLL_INTERVAL_SECONDS);
            continue;
        }

        Rec rec = getRec(reading.house, reading.outside_air, reading.power);

        if (rec == REC_NONE) {
            if (pending != REC_NONE) {
                printf("Recommendation cleared before debounce completed In:%d Out:%d Power:%d\n",
                       reading.house, reading.outside_air, reading.power);
                loggerAppend(config.log_path, "cleared", reading.house, reading.outside_air,
                             reading.power, getRecName(rec),
                             "recommendation cleared before debounce completed");
            } else {
                printf("No action needed In:%d Out:%d Power:%d\n",
                       reading.house, reading.outside_air, reading.power);
                loggerAppend(config.log_path, "idle", reading.house, reading.outside_air,
                             reading.power, getRecName(rec), "no action needed");
            }

            pending = REC_NONE;
            pendingCount = 0;
            lastSent = REC_NONE;
            lastSentTime = 0;
            portableSleepSeconds(POLL_INTERVAL_SECONDS);
            continue;
        }

        if (rec == pending) {
            pendingCount++;
        } else {
            pending = rec;
            pendingCount = 1;
        }

        printf("Pending recommendation: %s (%d/%d) In:%d Out:%d Power:%d\n",
               getRecName(rec), pendingCount, REQUIRED_STABLE_POLLS,
               reading.house, reading.outside_air, reading.power);
        loggerAppend(config.log_path, "pending", reading.house, reading.outside_air,
                     reading.power, getRecName(rec), "recommendation waiting for debounce");

        int fanOffOk = 1;
        if (pendingCount >= REQUIRED_STABLE_POLLS && rec == REC_CLOSE) {
            fanOffOk = houseTurnOffFans(&config);
            loggerAppend(config.log_path, fanOffOk ? "fan_off" : "fan_fail", -1, -1, -1,
                         getRecName(REC_CLOSE), fanOffOk ? "fan shutoff request succeeded"
                                                         : "fan shutoff request failed after retries");
        }

        if (pendingCount >= REQUIRED_STABLE_POLLS &&
            determineRec(rec, lastSent, lastSentTime, now)) {
            char message[160];

            if (rec == REC_OPEN) {
                snprintf(message, sizeof(message), "Open the windows (Out:%d In:%d)",
                         reading.outside_air, reading.house);
            } else if (fanOffOk) {
                snprintf(message, sizeof(message), "Close the windows (Out:%d In:%d)",
                         reading.outside_air, reading.house);
            } else {
                snprintf(message, sizeof(message),
                         "Close the windows (fan shutoff failed, Out:%d In:%d)",
                         reading.outside_air, reading.house);
            }

            if (pushoverSendMessage(&config, message)) {
                lastSent = rec;
                lastSentTime = time(NULL);
                printf("Sent notification: %s\n", message);
                loggerAppend(config.log_path, "notify_sent", reading.house, reading.outside_air,
                             reading.power, getRecName(rec), message);
            } else {
                loggerAppend(config.log_path, "notify_failed", reading.house, reading.outside_air,
                             reading.power, getRecName(rec), message);
            }
        } else if (pendingCount >= REQUIRED_STABLE_POLLS) {
            printf("Already sent %s; suppressing repeat notification\n", getRecName(rec));
            loggerAppend(config.log_path, "suppressed", reading.house, reading.outside_air,
                         reading.power, getRecName(rec), "repeat notification suppressed");
        }

        portableSleepSeconds(POLL_INTERVAL_SECONDS);
    }

    curl_global_cleanup();
    instanceLockRelease(&lock);
    return EXIT_SUCCESS;
}
