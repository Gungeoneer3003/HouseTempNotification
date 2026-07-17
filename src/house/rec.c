#include "rec.h"
#include "settings.h"

// Recommendation policy is based only on temperature difference and the local
// time window. Fan speed is intentionally not part of the decision so the web
// status and logged recommendation describe what should happen, not what the fan
// is currently doing.
Rec getRecForTime(int house, int outside_air, time_t now) {
    int diff = outside_air - house;

    if (diff <= -MARGIN && withinWindow(REC_OPEN, now)) {
        return REC_OPEN;
    }

    if (diff >= MARGIN && withinWindow(REC_CLOSE, now)) {
        return REC_CLOSE;
    }

    return REC_NONE;
}

// Backward-compatible wrapper for older call sites. The speed parameter is
// ignored by design; new code that already has a timestamp should call
// getRecForTime so one polling cycle uses one consistent time value.
Rec getRec(int house, int outside_air, int speed) {
    (void)speed;
    return getRecForTime(house, outside_air, time(NULL));
}

//Get the string representation of a recommendation
const char* getRecName(Rec rec) {
    switch (rec) {
        case REC_OPEN:
            return "open";
        case REC_CLOSE:
            return "close";
        default:
            return "none";
    }
}

//Globals that shouldn't be calculated everytime
static int dayTimeSeconds = 86400U;
static int closeWindowSeconds = ALLOW_CLOSE_AFTER_HOUR * 3600;
static int openWindowSeconds = ALLOW_OPEN_AFTER_HOUR * 3600;

//Get the number of seconds until the next window for sending a recommendation
//This helps determine how long the polling thread should sleep for
long secUntilWindow(Rec rec, time_t now) {
    if (rec == REC_NONE)
        return 0;
    
    // Get the current local time
    struct tm* local_time = localtime(&now);
    if (!local_time) {
        return 0;
    }

    int hour = local_time->tm_hour;
    int min = local_time->tm_min;
    int sec = local_time->tm_sec;
    int localTotal = hour * 3600 + min * 60 + sec;
    
    int ans;
    if (rec == REC_OPEN) {
        //Figure out how much time is left today and add the next time
        int remainder = dayTimeSeconds - localTotal;
        ans = remainder + closeWindowSeconds; 
    }
    else {
        ans = openWindowSeconds - localTotal;
    }

    return ans;
}

//Check whether the given recommendation is at the right time
//This should prevent an early notification 
int withinWindow(Rec rec, time_t now) {
    struct tm* local_time = localtime(&now);
    if (!local_time) {
        return 0;
    }

    int hour = local_time->tm_hour;

    //Check early cases
    if (rec == REC_CLOSE && hour < ALLOW_CLOSE_AFTER_HOUR)
        return 0;
    if (rec == REC_OPEN && hour < ALLOW_OPEN_AFTER_HOUR)
        return 0;
    
    //Check late cases
    if (rec == REC_CLOSE && hour >= ALLOW_OPEN_AFTER_HOUR)
        return 0;
    
    return 1;
}

//Describe the current recommendation using the same time-aware policy as the
//polling loop, without applying notification dispatch state.
const char* getCurrentStatusForTime(int house, int outside_air, time_t now) {
    switch (getRecForTime(house, outside_air, now)) {
        case REC_OPEN:
            return "Cooler out than in - Open windows";
        case REC_CLOSE:
            return "Hotter out than in - Close windows";
        default:
            if (outside_air - house < -MARGIN) {
                return "Cooler out than in - waiting for open window";
            }
            if (outside_air - house > MARGIN) {
                return "Hotter out than in - waiting for close window";
            }
            return "All clear - no action needed";
    }
}

const char* getCurrentStatus(int house, int outside_air, int speed) {
    (void)speed;
    return getCurrentStatusForTime(house, outside_air, time(NULL));
}
