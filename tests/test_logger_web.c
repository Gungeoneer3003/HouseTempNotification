#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "loggerWeb.h"

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s LOG_PATH PORT\n", argv[0]);
        return EXIT_FAILURE;
    }

    //Assign a port number from the command line arguments
    unsigned long port = strtoul(argv[2], NULL, 10);
    if (port == 0 || port > 65535) {
        fprintf(stderr, "invalid port: %s\n", argv[2]);
        return EXIT_FAILURE;
    }

    //Set up the logger web server columns
    static const char* const logger_web_columns[] = {
        "House",
        "Outside",
        "Power",
        "Recommendation",
        "Event",
        "Detail"
    };

    //Start the logger web server
    if (!loggerWebStart(argv[1],
                        (unsigned short)port,
                        "Logger Web Test",
                        logger_web_columns,
                        sizeof(logger_web_columns) / sizeof(logger_web_columns[0]))) {
        return EXIT_FAILURE;
    }

#ifdef _WIN32
    Sleep(3000);
#else
    sleep(3);
#endif

    return EXIT_SUCCESS;
}
