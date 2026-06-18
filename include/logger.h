#ifndef LOGGER_H
#define LOGGER_H

#include "recommendation.h"

int log_append(const char* log_path,
               const char* event,
               int house,
               int outside_air,
               int power,
               Recommendation rec,
               const char* detail);
void log_trim(const char* log_path);

#endif
