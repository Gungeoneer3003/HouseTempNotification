#ifndef RECOMMENDATION_H
#define RECOMMENDATION_H

#include <time.h>

typedef enum {
    REC_NONE = 0,
    REC_OPEN,
    REC_CLOSE
} Rec;

Rec getRec(int house, int outside_air, int speed);
Rec getRecForTime(int house, int outside_air, time_t now);
const char* getRecName(Rec rec);
const char* getCurrentStatus(int house, int outside_air, int speed);
const char* getCurrentStatusForTime(int house, int outside_air, time_t now);
long secUntilWindow(Rec rec, time_t now);
int withinWindow(Rec rec, time_t now);

#endif
