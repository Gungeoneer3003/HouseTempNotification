#ifndef RECOMMENDATION_H
#define RECOMMENDATION_H

#include <time.h>

typedef enum {
    REC_NONE = 0,
    REC_OPEN,
    REC_CLOSE
} Rec;

Rec getRec(int house, int outside_air, int power);
const char* getRecName(Rec rec);
int determineRec(Rec rec,
                             Rec last_sent,
                             time_t last_sent_time,
                             time_t now);

#endif
