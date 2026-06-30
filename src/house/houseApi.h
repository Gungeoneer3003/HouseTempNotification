#ifndef HOUSE_API_H
#define HOUSE_API_H

#include "config.h"

typedef struct {
    int house;
    int outside_air;
    int attic;
    int speed;
} SensorReading;

int houseReadSensor(const AppConfig* config, SensorReading* reading);
int houseTurnOffFans(const AppConfig* config);
int houseSpeedUpFans(const AppConfig* config);
int houseSlowDownFans(const AppConfig* config);
int pushoverSendMessage(const AppConfig* config, const char* message);

#endif
