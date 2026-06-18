#ifndef HOUSE_API_H
#define HOUSE_API_H

#include "config.h"

typedef struct {
    int house;
    int outside_air;
    int power;
} SensorReading;

int readSensor(const AppConfig* config, SensorReading* reading);
int turnOffFans(const AppConfig* config);
int pushoverMessage(const AppConfig* config, const char* message);

#endif
