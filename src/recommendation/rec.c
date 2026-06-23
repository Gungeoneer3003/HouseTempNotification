#include "rec.h"
#include "settings.h"

//Get the recommendation based on house, outside air, and power status
Rec getRec(int house, int outside_air, int power) {
    int diff = outside_air - house;

    if (!power && diff <= -MARGIN) {
        return REC_OPEN;
    }

    if (power && diff >= MARGIN) {
        return REC_CLOSE;
    }

    return REC_NONE;
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