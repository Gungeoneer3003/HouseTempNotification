#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

//Includes
#include "config.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "portable.h"
#include "settings.h"

//Prototypes
void configInitDefaults(AppConfig* config);
int configLoad(AppConfig* config, const char* env_file);
static char* trimWhitespace(char* str);
static void loadDotenv(const char* filename);
static int buildUrls(AppConfig* config);
static int buildUrl(char* output,
                    size_t output_size,
                    const char* house_link,
                    const char* path_part,
                    const char* label);

//For the default config values
void configInitDefaults(AppConfig* config) {
    if (!config) {
        return;
    }

    config->api_token = NULL;
    config->user_key = NULL;
    config->house_link = NULL;
    config->cgi_part = NULL;
    config->power_part = NULL;
    config->speed_up_part = NULL;
    config->slow_down_part = NULL;
    config->log_path = DEFAULT_LOG_FILE;
    config->lock_path = DEFAULT_LOCK_FILE;
    config->logger = NULL;
    config->cgi_url[0] = '\0';
    config->shutoff_url[0] = '\0';
    config->speed_up_url[0] = '\0';
    config->slow_down_url[0] = '\0';
}

//Load the configuration from the environment or a .env file
int configLoad(AppConfig* config, const char* env_file) {
    if (!config) {
        return 0;
    }

    //Set up a default configuration first
    configInitDefaults(config);

    //Load the .env file if specified
    if (env_file && *env_file) {
        loadDotenv(env_file);
    }

    //Get each part of the configuration from the environment
    config->api_token = getenv("API");
    config->user_key = getenv("USER_KEY");
    config->house_link = getenv("HOUSE_LINK");
    config->cgi_part = getenv("CGI_PART");
    config->power_part = getenv("POWER_PART");
    config->speed_up_part = getenv("SPEED_UP_PART");
    config->slow_down_part = getenv("SLOW_DOWN_PART");

    //Get the log and lock file paths from the environment, if set
    const char* log_path = getenv("LOG_FILE");
    if (log_path && *log_path) {
        config->log_path = log_path;
    }

    const char* lock_path = getenv("LOCK_FILE");
    if (lock_path && *lock_path) {
        config->lock_path = lock_path;
    }

    //Check if any required configuration is missing
    if (!config->api_token || !*config->api_token ||
        !config->user_key || !*config->user_key ||
        !config->house_link || !*config->house_link ||
        !config->cgi_part || !*config->cgi_part ||
        !config->power_part || !*config->power_part ||
        !config->speed_up_part || !*config->speed_up_part ||
        !config->slow_down_part || !*config->slow_down_part) {
        fprintf(stderr,
                "Missing configuration in %s or environment: API=%s USER_KEY=%s "
                "HOUSE_LINK=%s CGI_PART=%s POWER_PART=%s "
                "SPEED_UP_PART=%s SLOW_DOWN_PART=%s\n",
                env_file ? env_file : "environment",
                (config->api_token && *config->api_token) ? "ok" : "missing",
                (config->user_key && *config->user_key) ? "ok" : "missing",
                (config->house_link && *config->house_link) ? "ok" : "missing",
                (config->cgi_part && *config->cgi_part) ? "ok" : "missing",
                (config->power_part && *config->power_part) ? "ok" : "missing",
                (config->speed_up_part && *config->speed_up_part) ? "ok" : "missing",
                (config->slow_down_part && *config->slow_down_part) ? "ok" : "missing");
        return 0;
    }

    //Build the full URLs for CGI and fan control endpoints
    return buildUrls(config);
}

//Build the full URLs for CGI and fan control endpoints.
static int buildUrls(AppConfig* config) {
    if (!buildUrl(config->cgi_url,
                  sizeof(config->cgi_url),
                  config->house_link,
                  config->cgi_part,
                  "CGI")) {
        return 0;
    }

    if (!buildUrl(config->shutoff_url,
                  sizeof(config->shutoff_url),
                  config->house_link,
                  config->power_part,
                  "Power")) {
        return 0;
    }

    if (!buildUrl(config->speed_up_url,
                  sizeof(config->speed_up_url),
                  config->house_link,
                  config->speed_up_part,
                  "Speed up")) {
        return 0;
    }

    if (!buildUrl(config->slow_down_url,
                  sizeof(config->slow_down_url),
                  config->house_link,
                  config->slow_down_part,
                  "Slow down")) {
        return 0;
    }

    return 1;
}

//Join the base house URL with one endpoint path and check for truncation.
static int buildUrl(char* output,
                    size_t output_size,
                    const char* house_link,
                    const char* path_part,
                    const char* label) {
    int n = snprintf(output, output_size, "%s%s", house_link, path_part);
    if (n < 0 || (size_t)n >= output_size) {
        fprintf(stderr, "%s link is too long\n", label);
        return 0;
    }

    return 1;
}

//Trim leading and trailing whitespace from a string
static char* trimWhitespace(char* str) {
    if (!str) {
        return NULL;
    }

    //Loop to find the first non-whitespace character
    while (isspace((unsigned char)*str)) {
        str++;
    }

    //If the string is all whitespace, return an empty string
    if (*str == '\0') {
        return str;
    }

    //Loop to find the last non-whitespace character
    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        end--;
    }

    //Return the trimmed string
    *(end + 1) = '\0';
    return str;
}

//Load environment variables from a .env file
static void loadDotenv(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        return;
    }

    //Read the file line by line
    char line[512];
    while (fgets(line, sizeof(line), file)) {
        char* trimmed = trimWhitespace(line);

        //Skip empty lines and comments
        if (!trimmed || *trimmed == '\0' || *trimmed == '#') {
            continue;
        }

        //Find the equals sign that separates the key and value
        char* equals = strchr(trimmed, '=');
        if (!equals) {
            continue;
        }

        //Split the line into key and value parts
        *equals = '\0';
        char* key = trimWhitespace(trimmed);
        char* value = trimWhitespace(equals + 1);

        //Remove surrounding quotes from the value if present
        size_t len = strlen(value);
        if (len >= 2 && *value == '"' && value[len - 1] == '"') {
            value[len - 1] = '\0';
            value++;
        }

        //Set the environment variable
        if (*key && *value) {
            portableSetenv(key, value, 1);
        }
    }

    fclose(file);
}
