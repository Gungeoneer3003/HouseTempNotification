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

    //Load configuration
    AppConfig config;
    if (!configLoad(&config, "keys.env")) {
        fprintf(stderr, "Failed to initialize configuration\n");
        return EXIT_FAILURE;
    }

    //Acquire instance lock to ensure only one instance is running
    InstanceLock lock = INSTANCE_LOCK_INIT;
    if (!instanceLockAcquire(&lock, config.lock_path)) {
        return EXIT_FAILURE;
    }

    //Initialize libcurl for sending notifications
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
        fprintf(stderr, "Failed to initialize libcurl\n");
        instanceLockRelease(&lock);
        return EXIT_FAILURE;
    }

    //Trim the log on startup 
    logTrim(config.log_path);

    //Write a startup entry
    lprint(config.log_path, "-|-|-|-|startup|");

    //Start the logger web server if configured
#if LOGGER_WEB_PORT > 0
    static const char* const logger_web_columns[] = {
        "House",
        "Outside",
        "Power",
        "Recommendation",
        "Event",
        "Detail"
    };
    loggerWebStart(config.log_path,
                   LOGGER_WEB_PORT,
                   "House Notification Log",
                   logger_web_columns,
                   sizeof(logger_web_columns) / sizeof(logger_web_columns[0]));
#endif

    time_t lastLogTrimTime = time(NULL);

    //Main loop
    for (;;) {
        SensorReading reading;
        time_t now = time(NULL);

        //Trim the log periodically to prevent it from growing indefinitely
        if (difftime(now, lastLogTrimTime) >= LOG_TRIM_INTERVAL_SECONDS) {
            logTrim(config.log_path);
            lastLogTrimTime = now;
        }

        //Read the sensor values
        if (!houseReadSensor(&config, &reading)) {
            lprint(config.log_path, "-|-|-|-|sensor fail|");
            portableSleepSeconds(POLL_INTERVAL_SECONDS);
            continue;
        }
        
        //Determine the recommendation based on the sensor readings
        Rec rec = getRec(reading.house, reading.outside_air, reading.power);

        //Use the recommendation and see if a notification is in order
        if (rec != REC_NONE) {
            //Check if the recommendation is at the right time
            //This shouldn't happen since it'll sleep till the correct window, 
            //but still check for safety
            if(!withinWindow(rec, now)) {
                lprintf(config.log_path, "%d|%d|%d|%s|wrong time|",
                          reading.house, reading.outside_air, 
                          reading.power, getRecName(rec));

                // Sleep until next time window
                portableSleepSeconds(POLL_INTERVAL_SECONDS);
                continue;
            }

            char msg[256];
            int fanOffOk;

            //If closing the windows, turn off the fans
            if (rec == REC_CLOSE) {
                fanOffOk = houseTurnOffFans(&config);
            }

            //Send the notification
            if (rec == REC_OPEN) {
                snprintf(msg, sizeof(msg), "Open the windows (Out:%2d In:%2d)", reading.outside_air, reading.house);
            } else if (fanOffOk) {
                snprintf(msg, sizeof(msg), "Close the windows (Out:%2d In:%2d)", reading.outside_air, reading.house);
            } else {
                snprintf(msg, sizeof(msg), "Close the windows (fan failed)");
            }

            int msgResult = pushoverSendMessage(&config, msg);

            //Log the notification attempt and result
            if (msgResult) {
                lprintf(config.log_path, "%d|%d|%d|%s|notify sent|%s",
                          reading.house, reading.outside_air, 
                          reading.power, getRecName(rec), msg);

                // Sleep until next time window
                long sleep_sec = secUntilWindow(rec, now);
                lprintf(config.log_path, "-|-|-|-|Sleeping|sleep(%u)", (unsigned int)sleep_sec);
                if (sleep_sec > 0) {
                    portableSleepSeconds((unsigned int)sleep_sec);
                }
                
                continue;
            } 
            else {
                lprintf(config.log_path, "%d|%d|%d|%s|notify failed|",
                          reading.house, reading.outside_air, 
                          reading.power, getRecName(rec));
            }
        }

        //Log the fact that there wasn't anything to do
        lprintf(config.log_path, "%d|%d|%d|%s|idle|",
                      reading.house, reading.outside_air, 
                      reading.power, getRecName(rec));
        portableSleepSeconds(POLL_INTERVAL_SECONDS);
    }

    //Clean up (though we'll never actually get here)
    curl_global_cleanup();
    instanceLockRelease(&lock);
    return EXIT_SUCCESS;
}
