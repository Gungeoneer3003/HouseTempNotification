#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <signal.h>
#endif

#include "app_startup.h"
#include "config.h"
#include "logger.h"
#include "loggerSettings.h"
#include "settings.h"
#include "web/loggerWeb.h"

static void printUsage(const char* program_name) {
    fprintf(stderr,
            "usage: %s [LOG_PATH] [PORT] [--open|-o] [--smoke]\n"
            "  LOG_PATH defaults to %s\n"
            "  PORT defaults to %d\n",
            program_name,
            DEFAULT_LOG_FILE,
            LOGGER_WEB_PORT > 0 ? LOGGER_WEB_PORT : 8080);
}

static int parsePort(const char* value, unsigned short* out_port) {
    char* end = NULL;
    unsigned long port = strtoul(value, &end, 10);
    if (value[0] == '\0' || (end && *end != '\0') || port == 0 || port > 65535) {
        return 0;
    }
    *out_port = (unsigned short)port;
    return 1;
}

static int logHasRecentGraphData(const char* log_path) {
    FILE* file = fopen(log_path, "r");
    if (!file) {
        return 0;
    }

    time_t cutoff = time(NULL) - (time_t)24 * 60 * 60;
    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        char* newline = strpbrk(line, "\r\n");
        if (newline) {
            *newline = '\0';
        }

        LogRecord record;
        if (logger_record_parse_line(line, &record) &&
            record.has_logged_at &&
            record.logged_at >= cutoff &&
            record.field_count >= 4) {
            fclose(file);
            return 1;
        }
    }

    fclose(file);
    return 0;
}

static int appendRecentSampleLogIfNeeded(const char* log_path) {
    if (logHasRecentGraphData(log_path)) {
        return 1;
    }

    FILE* file = fopen(log_path, "a");
    if (!file) {
        fprintf(stderr,
                "warning: failed to append sample log data at %s; continuing anyway\n",
                log_path);
        return 0;
    }

    time_t now = time(NULL);
    fprintf(file,
            "{\"ts\":%lld,\"fields\":[\"72\",\"69\",\"78\",\"1\",\"open\",\"open notif\",\"local viewer sample open\"]}\n",
            (long long)(now - 3600));
    fprintf(file,
            "{\"ts\":%lld,\"fields\":[\"71\",\"68\",\"77\",\"1\",\"none\",\"sensor\",\"local viewer sample holding\"]}\n",
            (long long)(now - 1800));
    fprintf(file,
            "{\"ts\":%lld,\"fields\":[\"70\",\"66\",\"76\",\"0\",\"close\",\"close notif\",\"local viewer sample close\"]}\n",
            (long long)now);

    fclose(file);
    return 1;
}

static int localViewerFanCommand(void* user) {
    (void)user;
    return 1;
}

static void tryOpenBrowser(unsigned short port) {
    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%u/", (unsigned)port);

#ifdef _WIN32
    HINSTANCE result = ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32) {
        fprintf(stderr, "warning: failed to open browser automatically; open %s\n", url);
    }
#else
    char command[128];
    snprintf(command, sizeof(command), "xdg-open '%s' >/dev/null 2>&1", url);
    if (system(command) != 0) {
        fprintf(stderr, "warning: failed to open browser automatically; open %s\n", url);
    }
#endif
}

int main(int argc, char** argv) {
    const char* log_path = DEFAULT_LOG_FILE;
    unsigned short port = (unsigned short)(LOGGER_WEB_PORT > 0 ? LOGGER_WEB_PORT : 8080);
    int open_browser = 0;
    int smoke_mode = 0;
    int positional_count = 0;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (strcmp(arg, "--open") == 0 || strcmp(arg, "-o") == 0) {
            open_browser = 1;
            continue;
        }
        if (strcmp(arg, "--smoke") == 0) {
            smoke_mode = 1;
            continue;
        }
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            printUsage(argv[0]);
            return EXIT_SUCCESS;
        }

        if (positional_count == 0) {
            log_path = arg;
            positional_count++;
            continue;
        }
        if (positional_count == 1) {
            if (!parsePort(arg, &port)) {
                fprintf(stderr, "invalid port: %s\n", arg);
                return EXIT_FAILURE;
            }
            positional_count++;
            continue;
        }

        fprintf(stderr, "too many arguments\n");
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }

    (void)appendRecentSampleLogIfNeeded(log_path);

    Logger logger;
    if (!logger_init_file(&logger, log_path)) {
        fprintf(stderr, "failed to initialize logger for %s\n", log_path);
        return EXIT_FAILURE;
    }

    AppConfig config;
    if (!configLoad(&config, "keys.env")) {
        configInitDefaults(&config);
        config.house_link = "http://127.0.0.1/";
    }
    config.logger = &logger;
    config.log_path = log_path;

    appStartLoggerWebOnPort(&config, port);

    LoggerWebTodayControls local_controls = {
        .speed_up = localViewerFanCommand,
        .speed_down = localViewerFanCommand,
        .power_toggle = localViewerFanCommand,
        .power_on_speed = DEF_FAN_SPEED,
        .user = NULL
    };
    loggerWebSetTodayControls(&local_controls);

    printf("Web viewer ready: http://127.0.0.1:%u/\n", (unsigned)port);
    printf("Using log file: %s\n", log_path);
    if (!smoke_mode) {
        printf("Press Enter to stop the server.\n");
    }

    if (open_browser) {
        tryOpenBrowser(port);
    }

    if (!smoke_mode) {
        (void)getchar();
    }
    loggerWebStop();
    logger_destroy(&logger);

    return EXIT_SUCCESS;
}
