#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "config.h"
#include "house_api.h"
#include "instance_lock.h"
#include "logger.h"
#include "portable.h"
#include "recommendation.h"
#include "settings.h"

int main(void) {
    printf("Starting House Temperature Notification System\n");
    fflush(stdout);

    AppConfig config;
    if (!config_load(&config, "keys.env")) {
        fprintf(stderr, "Failed to initialize configuration\n");
        return EXIT_FAILURE;
    }

    InstanceLock lock = INSTANCE_LOCK_INIT;
    if (!instance_lock_acquire(&lock, config.lock_path)) {
        return EXIT_FAILURE;
    }

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
        fprintf(stderr, "Failed to initialize libcurl\n");
        instance_lock_release(&lock);
        return EXIT_FAILURE;
    }

    log_trim(config.log_path);
    log_append(config.log_path, "startup", -1, -1, -1, REC_NONE, "program started");

    Recommendation lastSent = REC_NONE;
    Recommendation pending = REC_NONE;
    int pendingCount = 0;
    time_t lastSentTime = 0;
    time_t lastLogTrimTime = time(NULL);

    for (;;) {
        SensorReading reading;
        time_t now = time(NULL);

        if (difftime(now, lastLogTrimTime) >= LOG_TRIM_INTERVAL_SECONDS) {
            log_trim(config.log_path);
            lastLogTrimTime = now;
        }

        if (!house_api_read_sensor(&config, &reading)) {
            fprintf(stderr, "Skipping this poll because the sensor read failed\n");
            log_append(config.log_path, "sensor_fail", -1, -1, -1, REC_NONE, "sensor read failed");
            portable_sleep_seconds(POLL_INTERVAL_SECONDS);
            continue;
        }

        Recommendation rec = recommendation_get(reading.house, reading.outside_air, reading.power);

        if (rec == REC_NONE) {
            if (pending != REC_NONE) {
                printf("Recommendation cleared before debounce completed In:%d Out:%d Power:%d\n",
                       reading.house, reading.outside_air, reading.power);
                log_append(config.log_path, "cleared", reading.house, reading.outside_air,
                           reading.power, rec, "recommendation cleared before debounce completed");
            } else {
                printf("No action needed In:%d Out:%d Power:%d\n",
                       reading.house, reading.outside_air, reading.power);
                log_append(config.log_path, "idle", reading.house, reading.outside_air,
                           reading.power, rec, "no action needed");
            }

            pending = REC_NONE;
            pendingCount = 0;
            lastSent = REC_NONE;
            lastSentTime = 0;
            portable_sleep_seconds(POLL_INTERVAL_SECONDS);
            continue;
        }

        if (rec == pending) {
            pendingCount++;
        } else {
            pending = rec;
            pendingCount = 1;
        }

        printf("Pending recommendation: %s (%d/%d) In:%d Out:%d Power:%d\n",
               recommendation_name(rec), pendingCount, REQUIRED_STABLE_POLLS,
               reading.house, reading.outside_air, reading.power);
        log_append(config.log_path, "pending", reading.house, reading.outside_air,
                   reading.power, rec, "recommendation waiting for debounce");

        int fanOffOk = 1;
        if (pendingCount >= REQUIRED_STABLE_POLLS && rec == REC_CLOSE) {
            fanOffOk = house_api_turn_off_fans(&config);
            log_append(config.log_path, fanOffOk ? "fan_off" : "fan_fail", -1, -1, -1,
                       REC_CLOSE, fanOffOk ? "fan shutoff request succeeded"
                                           : "fan shutoff request failed after retries");
        }

        if (pendingCount >= REQUIRED_STABLE_POLLS &&
            recommendation_should_send(rec, lastSent, lastSentTime, now)) {
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

            if (pushover_send_message(&config, message)) {
                lastSent = rec;
                lastSentTime = time(NULL);
                printf("Sent notification: %s\n", message);
                log_append(config.log_path, "notify_sent", reading.house, reading.outside_air,
                           reading.power, rec, message);
            } else {
                log_append(config.log_path, "notify_failed", reading.house, reading.outside_air,
                           reading.power, rec, message);
            }
        } else if (pendingCount >= REQUIRED_STABLE_POLLS) {
            printf("Already sent %s; suppressing repeat notification\n", recommendation_name(rec));
            log_append(config.log_path, "suppressed", reading.house, reading.outside_air,
                       reading.power, rec, "repeat notification suppressed");
        }

        portable_sleep_seconds(POLL_INTERVAL_SECONDS);
    }

    curl_global_cleanup();
    instance_lock_release(&lock);
    return EXIT_SUCCESS;
}
