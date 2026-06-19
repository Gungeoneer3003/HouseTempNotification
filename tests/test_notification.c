#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "config.h"
#include "houseApi.h"

static const char* getMessage(int argc, char** argv, char* buffer, size_t buffer_size) {
    const char* env_message = getenv("TEST_NOTIFICATION_MESSAGE");
    if (env_message && *env_message) {
        return env_message;
    }

    if (argc > 1 && argv[1] && *argv[1]) {
        return argv[1];
    }

    snprintf(buffer,
             buffer_size,
             "House notifier test notification (%lld)",
             (long long)time(NULL));
    return buffer;
}

int main(int argc, char** argv) {
    AppConfig config;
    char default_message[160];
    const char* message = getMessage(argc, argv, default_message, sizeof(default_message));

    if (!configLoad(&config, "keys.env")) {
        fprintf(stderr, "Failed to load notification config\n");
        return EXIT_FAILURE;
    }

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
        fprintf(stderr, "Failed to initialize libcurl\n");
        return EXIT_FAILURE;
    }

    int ok = pushoverSendMessage(&config, message);
    curl_global_cleanup();

    if (!ok) {
        fprintf(stderr, "Failed to send test notification\n");
        return EXIT_FAILURE;
    }

    printf("Sent test notification: %s\n", message);
    return EXIT_SUCCESS;
}
