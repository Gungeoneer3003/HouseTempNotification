#include "web_controls.h"

#include "houseApi.h"
#include "logger.h"
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
static int loggerWebWakeFan(const AppConfig* web_config);
static int loggerWebLogFanReading(const AppConfig* web_config, const char* detail);
#endif

void webControlsConfigureToday(const AppConfig* config)
{
#if LOGGER_WEB_PORT > 0
    static const char *const logger_web_today_columns[] = {
        "Inside",
        "Outside",
        "Attic",
        "Fan Speed"};

    if (!config) {
        return;
    }

    // The custom Today layout exposes fan controls beside the current readings.
    loggerWebShowToday(logger_web_today_columns,
                       sizeof(logger_web_today_columns) / sizeof(logger_web_today_columns[0]),
                       1,
                       1);

    // Wire each web fan button to its matching house API endpoint.
    LoggerWebTodayControls logger_web_today_controls = {
        .speed_up = loggerWebFanSpeedUp,
        .speed_down = loggerWebFanSlowDown,
        .power_toggle = loggerWebFanPowerToggle,
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
        // When the fan is on, the power button maps to the existing shutoff endpoint.
        if (!houseTurnOffFans(web_config)) {
            return 0;
        }

        return loggerWebLogFanReading(web_config, "power off");
    }

    // When the fan is off, wake it with normal sequential speed-up commands.
    if (!loggerWebWakeFan(web_config)) {
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

static int loggerWebWakeFan(const AppConfig* web_config)
{
    if (!web_config) {
        return 0;
    }

    // When powered off, the fan speed starts at 0; repeated speed-up calls reach DEF_FAN_SPEED.
    for (int i = 0; i < DEF_FAN_SPEED; i++) {
        if (!houseSpeedUpFans(web_config)) {
            return 0;
        }
    }

    return 1;
}

static int loggerWebLogFanReading(const AppConfig* web_config, const char* detail)
{
    SensorReading updated_reading;
    if (!web_config || !houseReadSensor(web_config, &updated_reading)) {
        return 0;
    }

    // The fresh log row is what the Today panel reads after the control request reloads.
    Rec updated_rec = getRec(updated_reading.house,
                             updated_reading.outside_air,
                             updated_reading.speed);
    return lprintf(web_config->log_path,
                   "%d|%d|%d|%d|%s|web fan|%s",
                   updated_reading.house,
                   updated_reading.outside_air,
                   updated_reading.attic,
                   updated_reading.speed,
                   getRecName(updated_rec),
                   detail ? detail : "");
}
#endif
