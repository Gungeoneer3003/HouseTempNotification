//Statement of Purpose:
 /*
  * The purpose of this file is to provide the implementation for time-related 
  * functions used in the logger web server. It includes functions to parse 
  * strings as doubles or Unix timestamps, format Unix timestamps into 
  * human-readable strings, and format durations into a human-readable format.
  */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L

#include "loggerWeb.h"
#include "loggerWebInternal.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

//Parse a string as a double, returning 1 on success and 0 on failure
int loggerWebParseDouble(const char* value, double* out) {
    if (!value || !*value || !out) {
        return 0;
    }

    errno = 0;
    char* end = NULL;
    double parsed = strtod(value, &end);
    if (end == value || errno == ERANGE) {
        return 0;
    }

    while (end && *end && isspace((unsigned char)*end)) {
        end++;
    }
    if (end && *end) {
        return 0;
    }

    *out = parsed;
    return 1;
}

//Parse a string as a Unix timestamp (seconds since epoch), 
//returning 1 on success and 0 on failure
int loggerWebParseUnixTime(const char* value, time_t* out) {
    if (!value || !*value || !out) {
        return 0;
    }

    errno = 0;
    char* end = NULL;
    long long parsed = strtoll(value, &end, 10);
    if (end == value || errno == ERANGE) {
        return 0;
    }

    while (end && *end && isspace((unsigned char)*end)) {
        end++;
    }
    if (end && *end) {
        return 0;
    }

    *out = (time_t)parsed;
    return 1;
}

//Format a Unix timestamp into a human-readable string 
//in the format YYYY-MM-DD HH:MM:SS
int loggerWebLogLocaltime(const time_t* value, struct tm* out) {
    if (!value || !out) {
        return 0;
    }

    return localtime_r(value, out) != NULL;
}

//Format a Unix timestamp into a human-readable label 
//in the format YYYY-MM-DD HH:MM:SS AM/PM
void loggerWebFormatUnixLabel(time_t value, char* buffer, size_t buffer_size) {
    struct tm local;

    if (loggerWebLogLocaltime(&value, &local)) {
        strftime(buffer, buffer_size, "%Y-%m-%d %I:%M:%S %p", &local);
    } else if (buffer_size > 0) {
        buffer[0] = '\0';
    }
}

//Format a Unix timestamp into a human-readable date 
//in the format YYYY-MM-DD
void loggerWebFormatUnixDate(time_t value, char* buffer, size_t buffer_size) {
    struct tm local;

    if (loggerWebLogLocaltime(&value, &local)) {
        strftime(buffer, buffer_size, "%Y-%m-%d", &local);
    } else if (buffer_size > 0) {
        buffer[0] = '\0';
    }
}

//Format a Unix timestamp into a human-readable time 
//in the format HH:MM:SS AM/PM
void loggerWebFormatUnixTime(time_t value, char* buffer, size_t buffer_size) {
    struct tm local;

    if (loggerWebLogLocaltime(&value, &local)) {
        strftime(buffer, buffer_size, "%I:%M:%S %p", &local);
    } else if (buffer_size > 0) {
        buffer[0] = '\0';
    }
}

//Format a duration in seconds into a human-readable format, 
//e.g., "1d 2h 3m" or "45s"
void loggerWebFormatDuration(time_t seconds, char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return;
    }

    if (seconds < 0) {
        seconds = 0;
    }

    //Calculate days, hours, minutes, and remaining seconds
    long long total_seconds = (long long)seconds;
    long long days = total_seconds / (24LL * 60LL * 60LL);
    long long hours = (total_seconds / (60LL * 60LL)) % 24LL;
    long long minutes = (total_seconds / 60LL) % 60LL;
    long long remaining_seconds = total_seconds % 60LL;

    if (days > 0) {
        snprintf(buffer, buffer_size, "%lldd %lldh %lldm", days, hours, minutes);
    } else if (hours > 0) {
        snprintf(buffer, buffer_size, "%lldh %lldm", hours, minutes);
    } else if (minutes > 0) {
        snprintf(buffer, buffer_size, "%lldm", minutes);
    } else {
        snprintf(buffer, buffer_size, "%llds", remaining_seconds);
    }
}


#endif
