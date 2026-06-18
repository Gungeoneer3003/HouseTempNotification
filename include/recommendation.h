#ifndef RECOMMENDATION_H
#define RECOMMENDATION_H

#include <time.h>

typedef enum {
    REC_NONE = 0,
    REC_OPEN,
    REC_CLOSE
} Recommendation;

Recommendation recommendation_get(int house, int outside_air, int power);
const char* recommendation_name(Recommendation rec);
int recommendation_should_send(Recommendation rec,
                               Recommendation last_sent,
                               time_t last_sent_time,
                               time_t now);

#endif
