#include "web_controls.h"

#include <stdio.h>

#include "houseApi.h"
#include "logger.h"
#include "loggerSettings.h"
#include "portable.h"
#include "rec.h"
#include "settings.h"
#include "web/loggerWeb.h"

#ifndef LOGGER_WEB_SHOW_CONTROLS
#define LOGGER_WEB_SHOW_CONTROLS LOGGER_WEB_ENABLE_CONTROLS
#endif

#if LOGGER_WEB_PORT > 0
static const char* loggerWebTodayStatus(int inside,
                                        int outside,
                                        int fan_speed,
                                        void* user);
#endif

#if LOGGER_WEB_PORT > 0 && LOGGER_WEB_ENABLE_CONTROLS
typedef int (*LoggerWebFanCommand)(const AppConfig* config);

static int loggerWebFanSpeedUp(void* arg);
static int loggerWebFanSlowDown(void* arg);
static int loggerWebFanPowerToggle(void* arg);
static int loggerWebRunFanCommand(void* arg,
                                  LoggerWebFanCommand command,
                                  const char* detail);
static int loggerWebReadSettledFan(const AppConfig* web_config,
                                   SensorReading* reading);
static int loggerWebLogFanSnapshot(const AppConfig* web_config,
                                   const SensorReading* reading,
                                   const char* detail);
static int loggerWebLogFanReading(const AppConfig* web_config, const char* detail);
#endif

void webControlsConfigureToday(const AppConfig* config)
{
#if LOGGER_WEB_PORT > 0
    static const char *const logger_web_today_columns[] = {
        "Inside",
        "Outside",
        "Attic"};

    if (!config) {
        return;
    }

    // The custom Today layout exposes fan controls beside the current readings.
    loggerWebShowToday(logger_web_today_columns,
                       sizeof(logger_web_today_columns) / sizeof(logger_web_today_columns[0]),
                       1,
                       LOGGER_WEB_SHOW_CONTROLS);
    loggerWebSetTodayStatus("Inside", "Outside", loggerWebTodayStatus, NULL);

#if LOGGER_WEB_ENABLE_CONTROLS
    // Wire each web fan button to its matching house API endpoint.
    LoggerWebTodayControls logger_web_today_controls = {
        .speed_up = loggerWebFanSpeedUp,
        .speed_down = loggerWebFanSlowDown,
        .power_toggle = loggerWebFanPowerToggle,
        .power_on_speed = DEF_FAN_SPEED,
        .user = (void*)config
    };
    loggerWebSetTodayControls(&logger_web_today_controls);
#endif
#else
    (void)config;
#endif
}

#if LOGGER_WEB_PORT > 0
static const char* loggerWebTodayStatus(int inside,
                                        int outside,
                                        int fan_speed,
                                        void* user)
{
    (void)user;
    return getCurrentStatus(inside, outside, fan_speed);
}
#endif

#if LOGGER_WEB_PORT > 0 && LOGGER_WEB_ENABLE_CONTROLS
static int loggerWebFanSpeedUp(void* arg)
{
    return loggerWebRunFanCommand(arg, houseSpeedUpFans, "speed up");
}

static int loggerWebFanSlowDown(void* arg)
{
    return loggerWebRunFanCommand(arg, houseSlowDownFans, "slow down");
}

static int loggerWebFanPowerToggle(void* arg)
{
    AppConfig* web_config = (AppConfig*)arg;
    if (!web_config) {
        return 0;
    }

    SensorReading current_reading;
    if (!houseReadSensor(web_config, &current_reading)) {
        return 0;
    }

    if (current_reading.speed) {
        // When the fan is on, preserve the shutoff semantics used by close notifications.
        if (!houseTurnOffFans(web_config)) {
            return 0;
        }

        return loggerWebLogFanReading(web_config, "power off");
    }

    // Wake the fan once, then send only the speed changes still needed to reach the
    // configured default. Keeping these requests sequential avoids overloading the
    // controller while preserving the intended power-on speed.
    if (!houseToggleFanPower(web_config)) {
        return 0;
    }

    SensorReading updated_reading;
    if (!loggerWebReadSettledFan(web_config, &updated_reading) ||
        updated_reading.speed <= 0) {
        return 0;
    }

    int speed_up_count = 0;
    while (updated_reading.speed < DEF_FAN_SPEED &&
           speed_up_count < DEF_FAN_SPEED) {
        if (!houseSpeedUpFans(web_config)) {
            return 0;
        }

        speed_up_count++;
        if (!loggerWebReadSettledFan(web_config, &updated_reading)) {
            return 0;
        }
    }

    return loggerWebLogFanSnapshot(web_config, &updated_reading, "power on");
}

static int loggerWebRunFanCommand(void* arg,
                                  LoggerWebFanCommand command,
                                  const char* detail)
{
    AppConfig* web_config = (AppConfig*)arg;
    if (!web_config || !command) {
        return 0;
    }

    if (!command(web_config)) {
        return 0;
    }

    // Append the post-command sensor reading so the Today panel refreshes immediately.
    return loggerWebLogFanReading(web_config, detail);
}

static int loggerWebLogFanReading(const AppConfig* web_config, const char* detail)
{
    SensorReading updated_reading;
    if (!loggerWebReadSettledFan(web_config, &updated_reading)) {
        return 0;
    }

    return loggerWebLogFanSnapshot(web_config, &updated_reading, detail);
}

static int loggerWebReadSettledFan(const AppConfig* web_config,
                                   SensorReading* reading)
{
    if (!web_config || !reading) {
        return 0;
    }

    // The Airscape controller can acknowledge a command before CGI reports the new speed.
    if (LOGGER_WEB_FAN_SETTLE_SECONDS > 0) {
        portableSleepSeconds(LOGGER_WEB_FAN_SETTLE_SECONDS);
    }

    return houseReadSensor(web_config, reading);
}

static int loggerWebLogFanSnapshot(const AppConfig* web_config,
                                   const SensorReading* reading,
                                   const char* detail)
{
    if (!web_config || !reading) {
        return 0;
    }

    // The control response reads this fresh row and returns the final state to the browser.
    Rec updated_rec = getRec(reading->house,
                             reading->outside_air,
                             reading->speed);
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
        getRecName(updated_rec),
        "web fan",
        detail ? detail : ""
    };
    return logger_log_fields(web_config->logger,
                             fields,
                             sizeof(fields) / sizeof(fields[0]));
}
#endif
