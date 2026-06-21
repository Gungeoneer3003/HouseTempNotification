#include "rec.h"

#include "settings.h"

Rec getRec(int house, int outside_air, int power) {
    int diff = outside_air - house;

    if (!power && diff <= -OPEN_MARGIN) {
        return REC_OPEN;
    }

    if (power && diff >= CLOSE_MARGIN) {
        return REC_CLOSE;
    }

    return REC_NONE;
}

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

int timeOk(Rec rec, time_t now) {
    struct tm* local_time = localtime(&now);
    int hour = local_time->tm_hour;

    if (rec == REC_OPEN) {
        return hour >= ALLOW_OPEN_AFTER_HOUR;
    }
    
    if (rec == REC_CLOSE) {
        if (ALLOW_CLOSE_AFTER_HOUR < ALLOW_OPEN_AFTER_HOUR) {
            return hour >= ALLOW_CLOSE_AFTER_HOUR && hour < ALLOW_OPEN_AFTER_HOUR;
        }
        return hour >= ALLOW_CLOSE_AFTER_HOUR;
    }

    return 0;
}

long secUntilWindow(Rec rec, time_t now) {
    struct tm* local_time = localtime(&now);
    int hour = local_time->tm_hour;
    int min = local_time->tm_min;
    int sec = local_time->tm_sec;
    int target_hour = (rec == REC_OPEN) ? ALLOW_OPEN_AFTER_HOUR : ALLOW_CLOSE_AFTER_HOUR;
    int hours_until = 0;

    if (rec == REC_OPEN) {
        hours_until = (hour >= target_hour) ? (24 - hour) + target_hour : target_hour - hour;
    } else if (rec == REC_CLOSE) {
        if (ALLOW_CLOSE_AFTER_HOUR < ALLOW_OPEN_AFTER_HOUR) {
            if (hour >= target_hour && hour < ALLOW_OPEN_AFTER_HOUR) {
                hours_until = (24 - hour) + target_hour;
            } else if (hour < target_hour) {
                hours_until = target_hour - hour;
            } else {
                hours_until = (24 - hour) + target_hour;
            }
        } else {
            hours_until = (hour >= target_hour) ? (24 - hour) + target_hour : target_hour - hour;
        }
    }

    long result = (long)hours_until * 3600 - (long)min * 60 - (long)sec;
    return (result < 0) ? 0 : result;
}

int shouldSend(Rec rec, Rec last_sent, time_t now) {
    if (rec == REC_NONE) {
        return 0;
    }

    return (rec != last_sent) && timeOk(rec, now);
}
