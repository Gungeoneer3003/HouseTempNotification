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

int determineRec(Rec rec,
                             Rec last_sent,
                             time_t last_sent_time,
                             time_t now) {
    if (rec != last_sent) {
        return 1;
    }

#if SAME_ALERT_REMINDER_SECONDS > 0
    if (last_sent_time == 0 || difftime(now, last_sent_time) >= SAME_ALERT_REMINDER_SECONDS) {
        return 1;
    }
#else
    (void)last_sent_time;
    (void)now;
#endif

    return 0;
}
