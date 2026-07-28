//Statement of Purpose:
/*
* This file contains the implementation for handling graph range requests
* in the logger web server. It includes functions to parse the range from
* the request, calculate the corresponding time window, and provide the
* start and end times for the specified range.
*/
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "../loggerWeb.h"
#include "../loggerWebInternal.h"
#include <string.h>
#include <time.h>

//Function prototypes for internal functions used in the logger web server
static int graphDayWindow(time_t now, time_t* range_start, time_t* range_end);
static int localDayStart(time_t value, int day_offset, time_t* out);

//Parse the graph range from the request string 
//Return the corresponding enum value
LoggerWebGraphRange loggerWebParseGraphRange(const char* request) {
    //If the request is NULL, return the default range (day)
    const char* query = request ? strchr(request, '?') : NULL;
    if (!query) {
        return LOGGER_WEB_GRAPH_RANGE_DAY;
    }

    //Find the end of the query string (space or end of string)
    const char* query_end = strchr(query, ' ');
    if (!query_end) {
        query_end = query + strlen(query);
    }

    //Define the prefix for the range parameter and its length
    const char range_prefix[] = "range=";
    const size_t range_prefix_length = sizeof(range_prefix) - 1;
    const char* cursor = query + 1;

    //Iterate through the query parameters to find the range parameter
    while (cursor < query_end) {
        const char* param_end = cursor;
        while (param_end < query_end && *param_end != '&') {
            param_end++;
        }

        size_t param_length = (size_t)(param_end - cursor);

        //Check if the current parameter starts with "range="
        if (param_length >= range_prefix_length &&
            strncmp(cursor, range_prefix, range_prefix_length) == 0) {
            
            const char* value = cursor + range_prefix_length;
            size_t value_length = param_length - range_prefix_length;

            if (value_length == 10 && strncmp(value, "three-days", 10) == 0) {
                return LOGGER_WEB_GRAPH_RANGE_THREE_DAYS;
            }

            if (value_length == 4 && strncmp(value, "week", 4) == 0) {
                return LOGGER_WEB_GRAPH_RANGE_WEEK;
            }

            return LOGGER_WEB_GRAPH_RANGE_DAY;
        }

        //Move the cursor to the next parameter, skipping the '&' if present
        cursor = param_end;
        if (cursor < query_end && *cursor == '&') {
            cursor++;
        }
    }

    return LOGGER_WEB_GRAPH_RANGE_DAY;
}

//Get the name of the graph range as a string
const char* loggerWebGraphRangeName(LoggerWebGraphRange range) {
    if (range == LOGGER_WEB_GRAPH_RANGE_THREE_DAYS) {
        return "three-days";
    }

    return range == LOGGER_WEB_GRAPH_RANGE_WEEK ? "week" : "day";
}

//Calculate the start and end times for the specified graph range
//Return 1 if successful, 0 if the range is invalid
int loggerWebGraphRangeWindow(LoggerWebGraphRange range,
                            time_t now,
                            time_t* range_start,
                            time_t* range_end) {
    //Check if the range_start and range_end pointers are valid
    if (!range_start || !range_end) {
        return 0;
    }

    //Determine the start and end times based on the specified range
    if (range == LOGGER_WEB_GRAPH_RANGE_DAY) {
        return graphDayWindow(now, range_start, range_end);
    }

    //For the three-days range, calculate the start and end 
    //times for the last three days
    if (range == LOGGER_WEB_GRAPH_RANGE_THREE_DAYS) {
        if (!localDayStart(now, -2, range_start) ||
            !localDayStart(now, 1, range_end)) {
            return 0;
        }

        return 1;
    }

    //For the week range, calculate the start and end times for the last week
    if (!localDayStart(now, -6, range_start) ||
        !localDayStart(now, 1, range_end)) {
        return 0;
    }

    return 1;
}

//Calculate the start and end times for the last 24 hours, 
//aligned to 2-hour intervals
static int graphDayWindow(time_t now, time_t* range_start, time_t* range_end) {
    //Check if the range_start and range_end pointers are valid
    if (!range_start || !range_end) {
        return 0;
    }

    //Convert the current time to local time and calculate the start 
    //and end of the 24-hour window
    struct tm local;
    if (!loggerWebLogLocaltime(&now, &local)) {
        return 0;
    }

    //Calculate the start time as the previous 2-hour interval
    //and the end time as the current 2-hour interval
    struct tm start_local = local;
    start_local.tm_mday -= 1;
    start_local.tm_hour = (local.tm_hour / 2) * 2;
    start_local.tm_min = 0;
    start_local.tm_sec = 0;
    start_local.tm_isdst = -1;

    //Calculate the end time as the next 2-hour interval
    struct tm end_local = local;
    int end_hour = (local.tm_hour / 2) * 2;
    if ((local.tm_hour % 2) != 0 || local.tm_min > 0 || local.tm_sec > 0) {
        end_hour += 2;
    }
    end_local.tm_hour = end_hour;
    end_local.tm_min = 0;
    end_local.tm_sec = 0;
    end_local.tm_isdst = -1;

    //Convert the start and end local times to time_t values
    time_t start = mktime(&start_local);
    time_t end = mktime(&end_local);
    if (start == (time_t)-1 || end == (time_t)-1 || end <= start) {
        return 0;
    }

    //Set the output range_start and range_end values
    *range_start = start;
    *range_end = end;
    return 1;
}

//Calculate the start of the day for the given value and day offset
//Return 1 if successful, 0 if there was an error
static int localDayStart(time_t value, int day_offset, time_t* out) {
    //Check if the output pointer is valid
    if (!out) {
        return 0;
    }

    //Convert the given time value to local time and adjust for the day offset
    struct tm local;
    if (!loggerWebLogLocaltime(&value, &local)) {
        return 0;
    }

    //Adjust the day of the month by the specified day offset 
    //and set the time to midnight
    local.tm_mday += day_offset;
    local.tm_hour = 0;
    local.tm_min = 0;
    local.tm_sec = 0;
    local.tm_isdst = -1;

    //Convert the adjusted local time back to time_t and check for errors
    time_t start = mktime(&local);
    if (start == (time_t)-1) {
        return 0;
    }

    //Set the output value to the calculated start of the day
    *out = start;
    return 1;
}

// Calculate the stats window for the selected graph range
// Day keeps its existing today-or-last-24-hours behavior unchanged
int loggerWebGraphStatsWindow(LoggerWebGraphRange range,
                              time_t now,
                              time_t* window_start,
                              time_t* window_end) {
    //Check if the window_start and window_end pointers are valid
    if (!window_start || !window_end) {
        return 0;
    }

    if (range == LOGGER_WEB_GRAPH_RANGE_THREE_DAYS) {
        if (!localDayStart(now, -2, window_start)) {
            return 0;
        }
        *window_end = now;
        return 1;
    }

    if (range == LOGGER_WEB_GRAPH_RANGE_WEEK) {
        if (!localDayStart(now, -6, window_start)) {
            return 0;
        }
        *window_end = now;
        return 1;
    }

    //Convert the current time to local time and calculate the start of today
    struct tm local_now;
    if (!loggerWebLogLocaltime(&now, &local_now)) {
        return 0;
    }

    //Set the time to midnight to get the start of today
    local_now.tm_hour = 0;
    local_now.tm_min = 0;
    local_now.tm_sec = 0;
    local_now.tm_isdst = -1;

    //Convert the local time back to time_t to get the start of today
    time_t today_start = mktime(&local_now);
    if (today_start == (time_t)-1) {
        return 0;
    }

    //Calculate the start of the last 24 hours and set the output values
    time_t last_24_hours = now - (time_t)24 * 60 * 60;
    *window_start = last_24_hours > today_start ? last_24_hours : today_start;
    *window_end = now;
    return 1;
}

