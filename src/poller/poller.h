#ifndef POLLER_H
#define POLLER_H

#include "config.h"

void pollerRun(const AppConfig* config);
int pollerLogCurrentReading(const AppConfig* config, const char* event, const char* detail);

#endif
