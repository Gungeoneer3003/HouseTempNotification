#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_startup.h"
#include "config.h"
#include "instanceLock.h"
#include "notification_worker.h"
#include "poller.h"

int main(void)
{
    printf("Starting House Temperature Notification System\n");
    fflush(stdout);

    // Load configuration before any subsystem needs endpoints, paths, or secrets.
    AppConfig config;
    if (!configLoad(&config, "keys.env"))
    {
        fprintf(stderr, "Failed to initialize configuration\n");
        return EXIT_FAILURE;
    }

    // Hold the process lock for the full lifetime of the application.
    InstanceLock lock = INSTANCE_LOCK_INIT;
    if (!instanceLockAcquire(&lock, config.lock_path))
    {
        return EXIT_FAILURE;
    }

    // Initialize libcurl once because polling, fan commands, and notifications share it.
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0)
    {
        fprintf(stderr, "Failed to initialize libcurl\n");
        instanceLockRelease(&lock);
        return EXIT_FAILURE;
    }

    appWriteStartupLog(&config);
    appStartLoggerWeb(&config);

    if (!notificationWorkerStart(&config))
    {
        curl_global_cleanup();
        instanceLockRelease(&lock);
        return EXIT_FAILURE;
    }

    // pollerRun owns the long-lived sensor loop and only returns on future shutdown support.
    pollerRun(&config);

    curl_global_cleanup();
    instanceLockRelease(&lock);
    return EXIT_SUCCESS;
}
