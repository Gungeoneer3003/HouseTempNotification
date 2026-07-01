#include "poller.h"

#include <stdio.h>
#include <time.h>
#include "houseApi.h"
#include "logger.h"
#include "notification_worker.h"
#include "portable.h"
#include "rec.h"
#include "settings.h"

static void logReadingEvent(const char* log_path,
                            const SensorReading* reading,
                            Rec rec,
                            const char* event,
                            const char* detail);
static const char* ignoredWindowEvent(Rec rec);

int pollerLogCurrentReading(const AppConfig* config, const char* event, const char* detail)
{
    SensorReading currentReading;
    if (!config || !houseReadSensor(config, &currentReading))
    {
        if (config) {
            lprint(config->log_path, "-|-|-|-|-|sensor fail|web poll");
        }
        return 0;
    }

    // Web-triggered polls refresh Today data without running notification policy.
    Rec currentRec = getRec(currentReading.house, currentReading.outside_air, currentReading.speed);
    logReadingEvent(config->log_path,
                    &currentReading,
                    currentRec,
                    event ? event : "web poll",
                    detail ? detail : "");
    return 1;
}

void pollerRun(const AppConfig* config)
{
    time_t lastLogTrimTime = time(NULL);

    for (;;)
    {
        time_t currentNow = time(NULL);
        SensorReading currentReading;

        // Trim periodically from the polling loop because this loop defines log cadence.
        if (difftime(currentNow, lastLogTrimTime) >= LOG_TRIM_INTERVAL_SECONDS)
        {
            logTrim(config->log_path);
            lastLogTrimTime = currentNow;
        }

        // A failed sensor read is logged as a row-shaped event so the web log remains parseable.
        if (!houseReadSensor(config, &currentReading))
        {
            lprint(config->log_path, "-|-|-|-|-|sensor fail|");
            portableSleepSeconds(POLL_INTERVAL_SECONDS);
            continue;
        }

        Rec currentRec = getRec(currentReading.house, currentReading.outside_air, currentReading.speed);

        // Recommendations outside their allowed window are recorded but not queued.
        if (currentRec != REC_NONE && !withinWindow(currentRec, currentNow))
        {
            logReadingEvent(config->log_path,
                            &currentReading,
                            currentRec,
                            ignoredWindowEvent(currentRec),
                            "outside notification window");
            portableSleepSeconds(POLL_INTERVAL_SECONDS);
            continue;
        }

        int notificationQueued = 0;
        if (currentRec != REC_NONE)
        {
            notificationQueued = notificationQueueReading(&currentReading, currentNow, currentRec);
        }

        // Keep the regular row format in one helper so future fields are added once.
        if (currentRec == REC_NONE)
        {
            logReadingEvent(config->log_path, &currentReading, currentRec, "idle", "");
        }
        else if (notificationQueued)
        {
            logReadingEvent(config->log_path, &currentReading, currentRec, "notify queued", "");
        }
        else
        {
            logReadingEvent(config->log_path, &currentReading, currentRec, "recording", "");
        }

        portableSleepSeconds(POLL_INTERVAL_SECONDS);
    }
}

static void logReadingEvent(const char* log_path,
                            const SensorReading* reading,
                            Rec rec,
                            const char* event,
                            const char* detail)
{
    lprintf(log_path,
            "%d|%d|%d|%d|%s|%s|%s",
            reading->house,
            reading->outside_air,
            reading->attic,
            reading->speed,
            getRecName(rec),
            event ? event : "",
            detail ? detail : "");
}

static const char* ignoredWindowEvent(Rec rec)
{
    // Preserve the existing event names so old log filters continue to match.
    return rec == REC_CLOSE ? "Ignoring close" : "Ignoring open";
}
