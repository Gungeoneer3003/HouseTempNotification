#ifndef NOTIFICATION_WORKER_H
#define NOTIFICATION_WORKER_H

#include <time.h>
#include "config.h"
#include "houseApi.h"
#include "rec.h"

int notificationWorkerStart(const AppConfig* config);
int notificationQueueReading(const SensorReading* reading, time_t now, Rec rec);

#endif
