#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <signal.h>
#endif

#include "loggerSettings.h"
#include "web/loggerWeb.h"

static void printUsage(const char* program_name) {
    fprintf(stderr,
            "usage: %s [LOG_PATH] [PORT] [--open|-o]\n"
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

static int writeSampleLogIfMissing(const char* log_path) {
    FILE* existing = fopen(log_path, "r");
    if (existing) {
        fclose(existing);
        return 1;
    }

    FILE* created = fopen(log_path, "w");
    if (!created) {
        fprintf(stderr,
                "warning: failed to create sample log at %s; continuing anyway\n",
                log_path);
        return 0;
    }

    fprintf(created,
            "{\"ts\":1751460000,\"fields\":[\"72\",\"69\",\"78\",\"on\",\"open\",\"open notif\",\"sample open\"]}\n");
    fprintf(created,
            "{\"ts\":1751460300,\"fields\":[\"71\",\"68\",\"77\",\"on\",\"none\",\"sensor\",\"holding\"]}\n");
    fprintf(created,
            "{\"ts\":1751460600,\"fields\":[\"70\",\"66\",\"76\",\"off\",\"close\",\"close notif\",\"sample close\"]}\n");

    fclose(created);
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
    int positional_count = 0;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (strcmp(arg, "--open") == 0 || strcmp(arg, "-o") == 0) {
            open_browser = 1;
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

    (void)writeSampleLogIfMissing(log_path);

    //Set up the logger web server columns
    static const char* const logger_web_columns[] = {
        "House",
        "Outside",
        "Attic",
        "Power",
        "Recommendation",
        "Event",
        "Detail"
    };
    static const char* const logger_web_temperature_graph_columns[] = {
        "House",
        "Attic",
        "Outside"
    };

    if (!loggerWebStart(log_path,
                        port,
                        "Logger Web Test",
                        logger_web_columns,
                        sizeof(logger_web_columns) / sizeof(logger_web_columns[0]))) {
        return EXIT_FAILURE;
    }
    loggerWebInsertGraph("House Over Time", "Time", "House");
    loggerWebInsertGraphSeries("Temperature Overlay",
                               "Time",
                               logger_web_temperature_graph_columns,
                               sizeof(logger_web_temperature_graph_columns) /
                                   sizeof(logger_web_temperature_graph_columns[0]));
    loggerWebShowSpan("Temperature Overlay", "Event", "open notif", "close notif", "#f59e0b");
    loggerWebSetRootDirectory("graphs");

    printf("Web viewer ready: http://127.0.0.1:%u/\n", (unsigned)port);
    printf("Using log file: %s\n", log_path);
    printf("Press Enter to stop the server.\n");

    if (open_browser) {
        tryOpenBrowser(port);
    }

    (void)getchar();
    loggerWebStop();

    return EXIT_SUCCESS;
}
