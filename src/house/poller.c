#include "poller.h"

#include <stdio.h>
#include <time.h>
#include "houseApi.h"
#include "logger.h"
#include "loggerSettings.h"
#include "notification_worker.h"
#include "portable.h"
#include "rec.h"
#include "settings.h"

static void logSensorFailure(const AppConfig* config, const char* detail);
static void logReadingEvent(const AppConfig* config,
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
            logSensorFailure(config, "web poll");
        }
        return 0;
    }

    // Web-triggered polls refresh Today data without running notification policy.
    Rec currentRec = getRec(currentReading.house, currentReading.outside_air, currentReading.speed);
    logReadingEvent(config,
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
            logger_trim(config->logger);
            lastLogTrimTime = currentNow;
        }

        // A failed sensor read is logged as a row-shaped event so the web log remains parseable.
        if (!houseReadSensor(config, &currentReading))
        {
            logSensorFailure(config, "");
            portableSleepSeconds(POLL_INTERVAL_SECONDS);
            continue;
        }

        Rec currentRec = getRec(currentReading.house, currentReading.outside_air, currentReading.speed);

        // Recommendations outside their allowed window are recorded but not queued.
        if (currentRec != REC_NONE && !withinWindow(currentRec, currentNow))
        {
            logReadingEvent(config,
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
            logReadingEvent(config, &currentReading, currentRec, "idle", "");
        }
        else if (notificationQueued)
        {
            logReadingEvent(config, &currentReading, currentRec, "notify queued", "");
        }
        else
        {
            logReadingEvent(config, &currentReading, currentRec, "recording", "");
        }

        portableSleepSeconds(POLL_INTERVAL_SECONDS);
    }
}

static void logSensorFailure(const AppConfig* config, const char* detail)
{
    const char* fields[] = {
        "-",
        "-",
        "-",
        "-",
        "-",
        "sensor fail",
        detail ? detail : ""
    };
    logger_log_fields(config ? config->logger : NULL,
                      fields,
                      sizeof(fields) / sizeof(fields[0]));
}

static void logReadingEvent(const AppConfig* config,
                            const SensorReading* reading,
                            Rec rec,
                            const char* event,
                            const char* detail)
{
    char house[16];
    char outside_air[16];
    char attic[16];
    char speed[16];

    snprintf(house, sizeof(house), "%d", reading->house);
    snprintf(outside_air, sizeof(outside_air), "%d", reading->outside_air);
    snprintf(attic, sizeof(attic), "%d", reading->attic);
    snprintf(speed, sizeof(speed), "%d", reading->speed);

    const char* fields[] = {
        house,
        outside_air,
        attic,
        speed,
        getRecName(rec),
        event ? event : "",
        detail ? detail : ""
    };
    logger_log_fields(config ? config->logger : NULL,
                      fields,
                      sizeof(fields) / sizeof(fields[0]));
}

static const char* ignoredWindowEvent(Rec rec)
{
    // Preserve the existing event names so old log filters continue to match.
    return rec == REC_CLOSE ? "Ignoring close" : "Ignoring open";
}
