#include <curl/curl.h>
#include <pthread.h>   // For the notify thread
#include <semaphore.h> // For the notify thread
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "config.h"
#include "houseApi.h"
#include "instanceLock.h"
#include "logger.h"
#include "loggerWeb.h"
#include "portable.h"
#include "rec.h"
#include "settings.h"

// Function prototypes
static void *notifyThread(void *arg);
static int queueNotif(const SensorReading *newReading, time_t newNow, Rec newRec);
static void setNotifReady(void);

// Globals for the pending notification handoff.
static SensorReading reading;
static time_t now;
static Rec rec;
static AppConfig config;
static int notifBusy;
static sem_t dataSem;
static pthread_mutex_t dataMutex = PTHREAD_MUTEX_INITIALIZER;

int main(void)
{
    printf("Starting House Temperature Notification System\n");
    fflush(stdout);

    // Load configuration
    if (!configLoad(&config, "keys.env"))
    {
        fprintf(stderr, "Failed to initialize configuration\n");
        return EXIT_FAILURE;
    }

    // Acquire instance lock to ensure only one instance is running
    InstanceLock lock = INSTANCE_LOCK_INIT;
    if (!instanceLockAcquire(&lock, config.lock_path))
    {
        return EXIT_FAILURE;
    }

    // Initialize libcurl for sending notifications
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0)
    {
        fprintf(stderr, "Failed to initialize libcurl\n");
        instanceLockRelease(&lock);
        return EXIT_FAILURE;
    }

    // Trim the log on startup
    logTrim(config.log_path);

    // Write a startup entry
    lprint(config.log_path, "-|-|-|-|-|startup|");

    // Start the logger web server if configured
#if LOGGER_WEB_PORT > 0
    static const char *const logger_web_columns[] = {
        "House",
        "Outside",
        "Attic",
        "Power",
        "Recommendation",
        "Event",
        "Detail"};
    static const char *const logger_web_temperature_graph_columns[] = {
        "House",
        "Attic",
        "Outside"};
    static const char *const logger_web_today_columns[] = {
        "House",
        "Outside",
        "Attic"};
    if (loggerWebStart(config.log_path,
                       LOGGER_WEB_PORT,
                       "House Notification Log",
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

        loggerWebShowToday(logger_web_today_columns,
                           sizeof(logger_web_today_columns) /
                               sizeof(logger_web_today_columns[0]));
        loggerWebSetRootDirectory("graphs");
    }
#endif

    pthread_t notifid;
    time_t lastLogTrimTime = time(NULL);
    int res;

    // Initialize the semaphore
    if (sem_init(&dataSem, 0, 0) == -1)
    {
        perror("sem_init for update");
        curl_global_cleanup();
        instanceLockRelease(&lock);
        return EXIT_FAILURE;
    }

    res = pthread_create(
        &notifid,
        NULL,
        notifyThread,
        NULL);

    // Check if it worked
    if (res != 0)
    {
        fprintf(stderr, "Notif Thread: %s\n", strerror(res));
        curl_global_cleanup();
        instanceLockRelease(&lock);
        return EXIT_FAILURE;
    }

    // Main loop
    for (;;)
    {
        time_t currentNow = time(NULL);
        SensorReading currentReading;
        Rec currentRec;

        // Trim the log periodically to prevent it from growing indefinitely
        if (difftime(currentNow, lastLogTrimTime) >= LOG_TRIM_INTERVAL_SECONDS)
        {
            logTrim(config.log_path);
            lastLogTrimTime = currentNow;
        }

        // Read the sensor values
        if (!houseReadSensor(&config, &currentReading))
        {
            lprint(config.log_path, "-|-|-|-|-|sensor fail|");
            portableSleepSeconds(POLL_INTERVAL_SECONDS);
            continue;
        }

        // Determine the recommendation based on the sensor readings
        currentRec = getRec(currentReading.house, currentReading.outside_air, currentReading.power);

        // Use the recommendation and see if a notification is in order
        if (currentRec != REC_NONE)
        {
            // Check if the recommendation is at the right time
            // This should not happen since the notify thread sleeps until the
            // next window, but still check for safety.
            if (!withinWindow(currentRec, currentNow))
            {
                //This is an error event, so let's specify what happens here
                char errorEvent[32];
                if(currentRec == REC_CLOSE) 
                    snprintf(errorEvent, sizeof(errorEvent), "%s", "Ignoring close");
                else
                    snprintf(errorEvent, sizeof(errorEvent), "%s", "Ignoring open");


                lprintf(config.log_path, "%d|%d|%d|%d|%s|%s|",
                        currentReading.house, currentReading.outside_air, 
                        currentReading.attic, currentReading.power, 
                        getRecName(currentRec), errorEvent);

                portableSleepSeconds(POLL_INTERVAL_SECONDS);
                continue;
            }
        }

        int notificationQueued = 0;
        if (currentRec != REC_NONE)
        {
            notificationQueued = queueNotif(&currentReading, currentNow, currentRec);
        }

        // The main thread owns the regular 5-minute sensor log.
        if (currentRec == REC_NONE)
        {
            lprintf(config.log_path, "%d|%d|%d|%d|%s|idle|",
                    currentReading.house, currentReading.outside_air, 
                    currentReading.attic, currentReading.power, 
                    getRecName(currentRec));
        }
        else if (notificationQueued)
        {
            lprintf(config.log_path, "%d|%d|%d|%d|%s|notify queued|",
                    currentReading.house, currentReading.outside_air, 
                    currentReading.attic, currentReading.power, 
                    getRecName(currentRec));
        }
        else
        {
            lprintf(config.log_path, "%d|%d|%d|%d|%s|recording|",
                    currentReading.house, currentReading.outside_air, 
                    currentReading.attic, currentReading.power, 
                    getRecName(currentRec));
        }

        portableSleepSeconds(POLL_INTERVAL_SECONDS);
    }

    // Clean up (though we'll never actually get here)
    curl_global_cleanup();
    instanceLockRelease(&lock);
    return EXIT_SUCCESS;
}

//Enqueue a notification to be sent from the sense thread
static int queueNotif(const SensorReading *newReading, time_t newNow, Rec newRec)
{
    int queued = 0;

    //Set up a mutex for the current state
    pthread_mutex_lock(&dataMutex);
    if (!notifBusy)
    {
        reading = *newReading;
        now = newNow;
        rec = newRec;
        notifBusy = 1;
        queued = 1;
    }
    pthread_mutex_unlock(&dataMutex);

    //Up the semaphore to wake the notif thread
    if (queued && sem_post(&dataSem) == -1)
    {
        perror("sem_post in sense thread");
        exit(EXIT_FAILURE);
    }

    return queued;
}

//The main function through which the notification thread will work
static void *notifyThread(void *arg)
{
    //There's no arguments so void the arg
    (void)arg;

    for (;;)
    {
        //Wait for the data to be ready
        if (sem_wait(&dataSem) == -1)
        {
            perror("sem_wait in notif thread");
            exit(EXIT_FAILURE);
        }

        //Copy over the data
        pthread_mutex_lock(&dataMutex);
        SensorReading localReading = reading;
        time_t localNow = now;
        Rec localRec = rec;
        pthread_mutex_unlock(&dataMutex);

        char msg[256];
        int fanOffOk = 1;

        // If closing the windows, turn off the fans
        if (localRec == REC_CLOSE)
        {
            fanOffOk = houseTurnOffFans(&config);
        }

        // Send the notification
        if (localRec == REC_OPEN)
        {
            snprintf(msg, sizeof(msg), "Open the windows (Out:%2d In:%2d)",
                     localReading.outside_air, localReading.house);
        }
        else if (fanOffOk)
        {
            snprintf(msg, sizeof(msg), "Close the windows (Out:%2d In:%2d)",
                     localReading.outside_air, localReading.house);
        }
        else
        {
            snprintf(msg, sizeof(msg), "Close the windows (fan failed)");
        }

        int msgResult = pushoverSendMessage(&config, msg);

        // Log the notification attempt and result
        if (msgResult)
        {
            //Mark the specific event
            char notifEvent[32];
            if(localRec == REC_CLOSE) 
                snprintf(notifEvent, sizeof(notifEvent), "%s", "close notif");
            else
                snprintf(notifEvent, sizeof(notifEvent), "%s", "open notif");

            lprintf(config.log_path, "%d|%d|%d|%d|%s|%s|%s",
                    localReading.house, localReading.outside_air,
                    localReading.attic, localReading.power,
                    getRecName(localRec), notifEvent, msg);

            // Sleep until next time window
            long sleep_sec = secUntilWindow(localRec, localNow);
            lprintf(config.log_path, "-|-|-|-|-|Sleeping|sleep(%u)", (unsigned int)sleep_sec);
            if (sleep_sec > 0)
            {
                portableSleepSeconds((unsigned int)sleep_sec);
            }
        }
        else
        {
            lprintf(config.log_path, "%d|%d|%d|%d|%s|notify failed|",
                    localReading.house, localReading.outside_air,
                    localReading.attic, localReading.power, 
                    getRecName(localRec));
        }

        //Clean up
        setNotifReady();
    }

    return NULL;
}

static void setNotifReady(void)
{
    pthread_mutex_lock(&dataMutex);
    notifBusy = 0;
    pthread_mutex_unlock(&dataMutex);
}
