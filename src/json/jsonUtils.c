#include "jsonUtils.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int jsonParseInt(const char* json, const char* key, int* out) {
    if (!json || !key || !out) {
        return 0;
    }

    char pattern[64];
    int n = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (n < 0 || (size_t)n >= sizeof(pattern)) {
        return 0;
    }

    const char* start = strstr(json, pattern);
    if (!start) {
        return 0;
    }

    start += strlen(pattern);
    while (isspace((unsigned char)*start)) {
        start++;
    }

    if (*start != ':') {
        return 0;
    }

    start++;
    while (isspace((unsigned char)*start)) {
        start++;
    }

    char* end = NULL;
    errno = 0;

    long value = strtol(start, &end, 10);
    if (end == start || errno == ERANGE || value < INT_MIN || value > INT_MAX) {
        return 0;
    }

    *out = (int)value;
    return 1;
}
