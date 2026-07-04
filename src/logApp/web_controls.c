#include "web_controls.h"

#include <stdio.h>

#include "houseApi.h"
#include "logger.h"
#include "loggerSettings.h"
#include "portable.h"
#include "rec.h"
#include "settings.h"
#include "web/loggerWeb.h"

#if LOGGER_WEB_PORT > 0
typedef int (*LoggerWebFanCommand)(const AppConfig* config);

static int loggerWebFanSpeedUp(void* arg);
static int loggerWebFanSlowDown(void* arg);
static int loggerWebFanPowerToggle(void* arg);
static int loggerWebRunFanCommand(void* arg,
                                  LoggerWebFanCommand command,
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
                       LOGGER_WEB_ENABLE_CONTROLS);

    if (!LOGGER_WEB_ENABLE_CONTROLS) {
        return;
    }

    // Wire each web fan button to its matching house API endpoint.
    LoggerWebTodayControls logger_web_today_controls = {
        .speed_up = loggerWebFanSpeedUp,
        .speed_down = loggerWebFanSlowDown,
        .power_toggle = loggerWebFanPowerToggle,
        .power_on_speed = DEF_FAN_SPEED,
        .user = (void*)config
    };
    loggerWebSetTodayControls(&logger_web_today_controls);
#else
    (void)config;
#endif
}

#if LOGGER_WEB_PORT > 0
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

    // When the fan is off, use the actual power link once instead of flooding speed-up.
    if (!houseToggleFanPower(web_config)) {
        return 0;
    }

    return loggerWebLogFanReading(web_config, "power on");
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
    if (!web_config) {
        return 0;
    }

    // The Airscape controller can acknowledge a command before CGI reports the new speed.
    if (LOGGER_WEB_FAN_SETTLE_SECONDS > 0) {
        portableSleepSeconds(LOGGER_WEB_FAN_SETTLE_SECONDS);
    }

    if (!houseReadSensor(web_config, &updated_reading)) {
        return 0;
    }

    // The fresh log row is what the Today panel reads after the control request reloads.
    Rec updated_rec = getRec(updated_reading.house,
                             updated_reading.outside_air,
                             updated_reading.speed);
    char house[16];
    char outside_air[16];
    char attic[16];
    char speed[16];

    snprintf(house, sizeof(house), "%d", updated_reading.house);
    snprintf(outside_air, sizeof(outside_air), "%d", updated_reading.outside_air);
    snprintf(attic, sizeof(attic), "%d", updated_reading.attic);
    snprintf(speed, sizeof(speed), "%d", updated_reading.speed);

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
